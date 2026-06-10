// SPDX-License-Identifier: MIT
// Copyright (c) 2026 RenoSeven
//
// SshService implementation.
//
// Purpose: expose the USB serial port over SSH. A dedicated FreeRTOS task
// binds port 22, accepts one client at a time, authenticates it, then pumps
// bytes between the SSH channel and the serial ring buffers until the peer
// disconnects.
//
// Per-session flow (handleSession): key exchange -> authentication -> channel
// open -> shell/pty request -> bidirectional data pump. Each blocking wait is
// non-blocking + poll so the task can observe taskRunning and shut down promptly.
//
// Host key: loaded from NVS; on first run an Ed25519 key is generated and
// persisted so the server identity stays stable across reboots.
//
// Threading: the listener runs entirely on its own task (core SSH_TASK_CORE);
// begin()/end() are called from the loop task and hand off via taskRunning.

#include "ssh.h"

#include <Preferences.h>
#include <mutex>
#include <string_view>

#include <libssh/callbacks.h>
#include <libssh/libssh.h>
#include <libssh/server.h>
#include <libssh_esp32.h>

#include "firmware.h"
#include "serial.h"

namespace {
    // --- NVS constants ---

    std::mutex sshNvsMutex;

    constexpr const char* NVS_NAMESPACE = "ssh";
    constexpr const char* NVS_KEY_PRIVATE_KEY = "private_key";
    constexpr const char* NVS_KEY_USERNAME = "username";
    constexpr const char* NVS_KEY_PASSWORD = "password";
    constexpr const char* NVS_KEY_AUTHORIZED_KEY = "authorized_key";
    constexpr const char* NVS_KEY_ALLOW_NO_AUTH = "allow_no_auth";

    // --- SSH constants ---

    // OpenSSH public-key line prefix for Ed25519. The trailing space is intentional:
    // it separates the type name from the base64 body in "ssh-ed25519 <base64>".
    constexpr const char* OPENSSH_KEY_ED25519_NAME = "ssh-ed25519 ";

    // Value for the "blocking" argument of ssh_set_blocking()/ssh_bind_set_blocking():
    // 0 selects non-blocking mode so each stage can poll taskRunning instead of
    // blocking the task indefinitely.
    constexpr int SSH_NON_BLOCKING = 0;

    // SSH_BIND_OPTIONS_PROCESS_CONFIG value: false skips reading the system-wide
    // libssh server config, which does not exist on the device.
    constexpr bool SSH_PROCESS_CONFIG_ENABLED = false;

    // Message-of-the-day written to the channel right after login, before bridging
    // the serial port. Uses CRLF line endings for raw (non-PTY) terminals.
    constexpr std::string_view SSH_MOTD = "This SSH session is bridged to the device serial port.\r\n"
                                          "Escape sequence is '~' '.' (at line start)\r\n";

    // --- Key management ---

    // Load the saved private key, or generate a fresh Ed25519 one when none is stored / it fails to parse.
    // On success sets key to an owned ssh_key (caller frees) and created to true when a new key was generated.
    // Returns SSH_OK on success, SSH_ERROR on failure.
    int loadOrCreateHostKey(const String& privateKey, ssh_key& key, bool& created) {
        key = nullptr;
        created = false;

        // Try to import the saved private key.
        if (ssh_pki_import_privkey_base64(privateKey.c_str(), nullptr, nullptr, nullptr, &key) == SSH_OK) {
            return SSH_OK;
        }

        // Generate a new key if the saved one failed to import.
        if (ssh_pki_generate(SSH_KEYTYPE_ED25519, 0, &key) == SSH_OK) {
            created = true;
            return SSH_OK;
        }

        return SSH_ERROR;
    }

    // Ensure the server host key exists, persist it to NVS if it was freshly generated,
    // and publish its OpenSSH-format public key into publicKey for the status page.
    // On success sets key to the owned ssh_key for the caller to install on the bind.
    // Returns SSH_OK on success, SSH_ERROR on failure.
    int resolveHostKey(String& privateKey, String& publicKey, ssh_key& key) {
        bool created = false;

        if (loadOrCreateHostKey(privateKey, key, created) != SSH_OK) {
            return SSH_ERROR;
        }

        // Persist a freshly generated key to NVS so the server identity is stable across reboots.
        if (created) {
            char* newKey = nullptr;
            if (ssh_pki_export_privkey_base64(key, nullptr, nullptr, nullptr, &newKey) != SSH_OK) {
                ssh_key_free(key);
                key = nullptr;
                return SSH_ERROR;
            }

            privateKey = newKey;
            ssh_string_free_char(newKey);

            SshConfig cfg = SshConfig::read();
            cfg.privateKey = privateKey;
            cfg.write();
        }

        char* exported = nullptr;
        if (ssh_pki_export_pubkey_base64(key, &exported) == SSH_OK) {
            publicKey = OPENSSH_KEY_ED25519_NAME;
            publicKey += exported;
            ssh_string_free_char(exported);
        }

        return SSH_OK;
    }

    // Parse one OpenSSH authorized_keys line ("type base64 [comment]") into an ssh_key.
    // On success sets key to the owned ssh_key (caller must ssh_key_free()).
    // Returns SSH_OK on success, SSH_ERROR on any malformed/oversized/unknown-type input (key left as nullptr).
    int importOpenSshPublicKey(const String& str, ssh_key& key) {
        key = nullptr;

        String trimmed = str;
        trimmed.trim();

        if (trimmed.isEmpty()) {
            return SSH_ERROR;
        }

        const int typeEnd = trimmed.indexOf(' ');
        if (typeEnd <= 0) {
            return SSH_ERROR;
        }

        const String type = trimmed.substring(0, typeEnd);
        if (type.length() >= 64) {
            return SSH_ERROR;
        }

        String base64 = trimmed.substring(typeEnd + 1);
        base64.trim();

        const int commentStart = base64.indexOf(' ');
        if (commentStart >= 0) {
            base64 = base64.substring(0, commentStart);
        }

        if (base64.isEmpty() || base64.length() > Firmware::LIMIT_SSH_AUTHORIZED_KEY_MAX) {
            return SSH_ERROR;
        }

        const enum ssh_keytypes_e keyType = ssh_key_type_from_name(type.c_str());
        if (keyType == SSH_KEYTYPE_UNKNOWN) {
            return SSH_ERROR;
        }

        if (ssh_pki_import_pubkey_base64(base64.c_str(), keyType, &key) != SSH_OK) {
            key = nullptr;
            return SSH_ERROR;
        }

        return SSH_OK;
    }

    // Return true if clientPublicKey matches the configured authorizedKey line.
    // Parses authorizedKey into a temporary key and compares public parts only;
    // clientPublicKey is owned by libssh, the parsed key is freed here.
    bool matchesAuthorizedKey(const String& authorizedKey, ssh_key publicKey) {
        if (publicKey == nullptr) {
            return false;
        }

        ssh_key allowedPublicKey = nullptr;
        if (importOpenSshPublicKey(authorizedKey, allowedPublicKey) != SSH_OK) {
            return false;
        }

        const bool matches = (ssh_key_cmp(publicKey, allowedPublicKey, SSH_KEY_CMP_PUBLIC) == 0);
        ssh_key_free(allowedPublicKey);

        return matches;
    }

    // --- Session management ---

    // Negotiation stages, advanced by libssh callbacks.
    enum class SessionStage : uint8_t {
        Authenticating, // Waiting for client to pass auth.
        OpeningChannel, // Auth done, waiting for channel open.
        WaitingShell,   // Channel open, waiting for shell request.
        Ready,          // All negotiation complete, ready to bridge.
    };

    // Per-session state passed to the libssh server/channel callbacks via their
    // userdata pointer. Lives on handleSession()'s stack for the whole session.
    //
    // The config fields are borrowed by reference from the owning SshService;
    // they stay valid because end() clears them only after the task has stopped.
    // Callbacks advance `stage` through the negotiation sequence. The callback
    // structs are stored (not copied) by libssh, so they must outlive the session.
    struct SessionContext {
        // --- Borrowed config & shutdown signal ---

        const String& username;           // Expected login name.
        const String& password;           // Expected password.
        const String& authorizedKey;      // Accepted client public key.
        const bool allowNoAuth;           // Accept any client without verifying credentials.
        const std::atomic<bool>& running; // Cooperative shutdown flag.

        // --- Negotiation state (advanced by callbacks) ---

        SessionStage stage = SessionStage::Authenticating;
        ssh_channel channel = nullptr;

        // --- libssh callback storage (must outlive the session) ---

        ssh_server_callbacks_struct serverCallbacks;   // Auth + channel-open callbacks.
        ssh_channel_callbacks_struct channelCallbacks; // Pty + shell-request callbacks.
    };

    // Return the per-stage timeout for the current negotiation phase.
    // Drive the libssh message loop through all negotiation stages until Ready.
    // Returns true if the session is fully negotiated, false on shutdown, peer
    // disconnect, or timeout.
    bool negotiate(ssh_session session, SessionContext& ctx) {
        const uint32_t negotiateStartedAt = millis();

        while (ctx.stage != SessionStage::Ready) {
            if (!ctx.running.load(std::memory_order_relaxed)) {
                return false;
            }

            if (!ssh_is_connected(session)) {
                return false;
            }

            if (millis() - negotiateStartedAt >= Firmware::SSH_NEGOTIATE_TIMEOUT_MS) {
                return false;
            }

            ssh_execute_message_callbacks(session);
            vTaskDelay(pdMS_TO_TICKS(Firmware::BRIDGE_POLL_INTERVAL_MS));
        }

        return true;
    }

    // Accept the pty request so the OpenSSH client switches its local terminal into
    // raw mode: keystrokes (incl. Ctrl-C) are forwarded as raw bytes with no local
    // echo or line buffering — the transparent passthrough a serial console needs.
    // No real terminal is allocated; the client-side mode switch is the whole point,
    // and it is also what enables the "~." disconnect escape.
    int channelPtyRequest(ssh_session, ssh_channel, const char*, int, int, int, int, void*) {
        return SSH_OK;
    }

    int channelShellRequest(ssh_session, ssh_channel, void* userdata) {
        static_cast<SessionContext*>(userdata)->stage = SessionStage::Ready;
        return SSH_OK;
    }

    ssh_channel channelOpenSession(ssh_session session, void* userdata) {
        SessionContext* ctx = static_cast<SessionContext*>(userdata);

        ssh_channel channel = ssh_channel_new(session);
        if (channel == nullptr) {
            return nullptr;
        }

        ssh_callbacks_init(&ctx->channelCallbacks);
        ctx->channelCallbacks.userdata = userdata;
        ctx->channelCallbacks.channel_pty_request_function = channelPtyRequest;
        ctx->channelCallbacks.channel_shell_request_function = channelShellRequest;
        ssh_set_channel_callbacks(channel, &ctx->channelCallbacks);

        ctx->channel = channel;
        ctx->stage = SessionStage::WaitingShell;

        return channel;
    }

    int authPassword(ssh_session, const char* user, const char* password, void* userdata) {
        SessionContext* ctx = static_cast<SessionContext*>(userdata);

        if (!user || !password || ctx->username != user || ctx->password != password) {
            return SSH_AUTH_DENIED;
        }

        ctx->stage = SessionStage::OpeningChannel;
        return SSH_AUTH_SUCCESS;
    }

    int authPubkey(ssh_session, const char* user, struct ssh_key_struct* pubkey, char signature_state, void* userdata) {
        SessionContext* ctx = static_cast<SessionContext*>(userdata);

        if (!user || ctx->username != user || !matchesAuthorizedKey(ctx->authorizedKey, pubkey)) {
            return SSH_AUTH_DENIED;
        }

        // Two-phase pubkey auth: STATE_NONE is only a "would this key be accepted?"
        // probe (no signature yet), so grant access only once a signed request arrives.
        if (signature_state != SSH_PUBLICKEY_STATE_NONE) {
            ctx->stage = SessionStage::OpeningChannel;
        }
        return SSH_AUTH_SUCCESS;
    }

    int authNone(ssh_session, const char*, void* userdata) {
        SessionContext* ctx = static_cast<SessionContext*>(userdata);

        if (!ctx->allowNoAuth) {
            return SSH_AUTH_DENIED;
        }

        ctx->stage = SessionStage::OpeningChannel;
        return SSH_AUTH_SUCCESS;
    }

} // namespace

// ============================================================
//                         SSH Config
// ============================================================

SshConfig SshConfig::read() {
    std::lock_guard<std::mutex> lock(sshNvsMutex);

    Preferences prefs;
    prefs.begin(NVS_NAMESPACE, true);
    SshConfig cfg{
        .privateKey = prefs.getString(NVS_KEY_PRIVATE_KEY, Firmware::DEFAULT_SSH_PRIVATE_KEY),
        .username = prefs.getString(NVS_KEY_USERNAME, Firmware::DEFAULT_SSH_USERNAME),
        .password = prefs.getString(NVS_KEY_PASSWORD, Firmware::DEFAULT_SSH_PASSWORD),
        .authorizedKey = prefs.getString(NVS_KEY_AUTHORIZED_KEY, Firmware::DEFAULT_SSH_AUTHORIZED_KEY),
        .allowNoAuth = prefs.getBool(NVS_KEY_ALLOW_NO_AUTH, Firmware::DEFAULT_SSH_ALLOW_NO_AUTH),
    };
    prefs.end();

    return cfg;
}

void SshConfig::write() const {
    std::lock_guard<std::mutex> lock(sshNvsMutex);

    Preferences prefs;
    prefs.begin(NVS_NAMESPACE, false);
    prefs.putString(NVS_KEY_PRIVATE_KEY, this->privateKey);
    prefs.putString(NVS_KEY_USERNAME, this->username);
    prefs.putString(NVS_KEY_PASSWORD, this->password);
    prefs.putString(NVS_KEY_AUTHORIZED_KEY, this->authorizedKey);
    prefs.putBool(NVS_KEY_ALLOW_NO_AUTH, this->allowNoAuth);
    prefs.end();
}

void SshConfig::clear() {
    std::lock_guard<std::mutex> lock(sshNvsMutex);

    Preferences prefs;
    prefs.begin(NVS_NAMESPACE, false);
    prefs.clear();
    prefs.end();
}

// ============================================================
//                         SSH Service
// ============================================================

// Stages: key exchange -> authentication -> channel/shell negotiation -> data
// pump. The socket is switched to non-blocking so every stage can poll taskRunning
// and bail out promptly on shutdown.
void SshService::handleSession(ssh_session session) {
    if (ssh_handle_key_exchange(session) != SSH_OK) {
        return;
    }

    ssh_set_blocking(session, SSH_NON_BLOCKING);

    const int authMethods = this->allowNoAuth
                                ? (SSH_AUTH_METHOD_NONE | SSH_AUTH_METHOD_PASSWORD | SSH_AUTH_METHOD_PUBLICKEY)
                                : (SSH_AUTH_METHOD_PASSWORD | SSH_AUTH_METHOD_PUBLICKEY);
    ssh_set_auth_methods(session, authMethods);

    // --- Negotiation ---

    // Callbacks advance the session state machine (auth → channel → shell → ready).
    SessionContext ctx{this->username, this->password, this->authorizedKey, this->allowNoAuth, this->taskRunning};

    ssh_callbacks_init(&ctx.serverCallbacks);
    ctx.serverCallbacks.userdata = &ctx;
    ctx.serverCallbacks.auth_password_function = authPassword;
    ctx.serverCallbacks.auth_pubkey_function = authPubkey;
    ctx.serverCallbacks.auth_none_function = authNone;
    ctx.serverCallbacks.channel_open_request_session_function = channelOpenSession;
    ssh_set_server_callbacks(session, &ctx.serverCallbacks);

    if (!negotiate(session, ctx)) {
        if (ctx.channel != nullptr) {
            ssh_channel_close(ctx.channel);
            ssh_channel_free(ctx.channel);
        }
        return;
    }

    // --- Data bridge ---

    this->connected.store(true, std::memory_order_relaxed);
    this->bridgeChannel(ctx.channel);
    this->connected.store(false, std::memory_order_relaxed);

    ssh_channel_close(ctx.channel);
    ssh_channel_free(ctx.channel);
}

// Bridge the channel to the serial ring buffers until either side closes:
//   channel -> usbTx (bytes destined for the USB host)
//   usbRx   -> channel (bytes the USB host produced)
//
// Both directions are serviced each iteration so neither starves the other when
// one side's buffer is temporarily full. When no data moves, the task sleeps for
// up to BRIDGE_POLL_INTERVAL_MS, yielding the CPU to lower-priority work.
void SshService::bridgeChannel(ssh_channel channel) {
    ssh_channel_write(channel, SSH_MOTD.data(), SSH_MOTD.size());

    while (this->isRunning() && ssh_channel_is_open(channel) && !ssh_channel_is_eof(channel)) {
        bool didWork = false;

        // SSH channel -> usbTx: drain available channel data into the ring buffer.
        for (;;) {
            const size_t chunk = this->serial.usbTx.writeContiguous();
            if (chunk == 0)
                break;

            const int n = ssh_channel_read_nonblocking(channel, this->serial.usbTx.writeBuf(), chunk, 0);
            if (n < 0)
                return;
            if (n == 0)
                break;

            this->serial.usbTx.writeCommit(static_cast<size_t>(n));
            didWork = true;
        }

        // usbRx -> SSH channel: forward buffered serial data to the client.
        for (;;) {
            const size_t chunk = this->serial.usbRx.readContiguous();
            if (chunk == 0)
                break;

            const int w = ssh_channel_write(channel, this->serial.usbRx.readBuf(), chunk);
            if (w < 0)
                return;
            if (w == 0)
                break;

            this->serial.usbRx.readCommit(static_cast<size_t>(w));
            didWork = true;
        }

        if (!didWork) {
            ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(Firmware::BRIDGE_POLL_INTERVAL_MS));
        }
    }
}

// --- Key management ---

bool SshService::isValidOpenSshPublicKey(const String& str) {
    ssh_key key = nullptr;

    if (importOpenSshPublicKey(str, key) != SSH_OK) {
        return false;
    }

    ssh_key_free(key);
    return true;
}

// --- Lifecycle ---

void SshService::begin() {
    libssh_begin();

    const SshConfig cfg = SshConfig::read();

    this->privateKey = cfg.privateKey;
    this->username = cfg.username;
    this->password = cfg.password;
    this->authorizedKey = cfg.authorizedKey;
    this->allowNoAuth = cfg.allowNoAuth;
    this->taskRunning.store(true, std::memory_order_relaxed);

    xTaskCreatePinnedToCore(SshService::task, "ssh", Firmware::SSH_TASK_STACK, this, Firmware::SSH_TASK_PRIO,
                            &this->taskHandle, Firmware::SSH_TASK_CORE);
}

void SshService::end() {
    // Cooperative shutdown: clear taskRunning, then keep nudging the task (it may
    // be blocked on a notification) until it self-deletes and clears taskHandle.
    // Only then is it safe to wipe the credentials below.
    if (this->taskHandle != nullptr) {
        this->taskRunning.store(false, std::memory_order_relaxed);
        while (this->taskHandle != nullptr) {
            xTaskNotifyGive(this->taskHandle);
            vTaskDelay(pdMS_TO_TICKS(Firmware::BRIDGE_POLL_INTERVAL_MS));
        }
    }
    this->taskRunning.store(false, std::memory_order_relaxed);
    this->connected.store(false, std::memory_order_relaxed);
    this->privateKey.clear();
    this->publicKey.clear();
    this->username.clear();
    this->password.clear();
    this->authorizedKey.clear();
    this->allowNoAuth = false;
}

void SshService::restart() {
    this->end();
    this->begin();
}

// --- Task ---

// Resolve the host key, bind port 22 (retrying until the network is up), then
// accept and serve sessions until taskRunning is cleared. On exit it releases libssh
// resources, clears taskHandle (the flag end() waits on), and self-deletes.
void SshService::task(void* arg) {
    SshService& svc = *static_cast<SshService*>(arg);

    // Initialize service state.
    ssh_key hostKey = nullptr;
    if (resolveHostKey(svc.privateKey, svc.publicKey, hostKey) != SSH_OK) {
        svc.taskHandle = nullptr;
        vTaskDelete(NULL);
        return;
    }

    ssh_bind bind = ssh_bind_new();
    if (!bind) {
        ssh_key_free(hostKey);
        svc.taskHandle = nullptr;
        vTaskDelete(NULL);
        return;
    }

    // Configure the bind.
    ssh_bind_options_set(bind, SSH_BIND_OPTIONS_PROCESS_CONFIG, &SSH_PROCESS_CONFIG_ENABLED);
    ssh_bind_options_set(bind, SSH_BIND_OPTIONS_BINDPORT, &Firmware::NET_PORT_SSH);
    ssh_bind_options_set(bind, SSH_BIND_OPTIONS_IMPORT_KEY, hostKey);
    ssh_bind_set_blocking(bind, SSH_NON_BLOCKING);

    // Poll until the bind is listening.
    while (ssh_bind_listen(bind) < 0) {
        if (!svc.isRunning()) {
            goto cleanup;
        }
        vTaskDelay(pdMS_TO_TICKS(Firmware::SSH_BIND_RETRY_INTERVAL_MS));
    }

    // Accept one client at a time until shutdown is requested. Only one
    // concurrent session is supported; the next accept blocks (non-blocking poll)
    // until the current session finishes.
    while (svc.isRunning()) {
        ssh_session session = ssh_new();
        if (!session) {
            vTaskDelay(pdMS_TO_TICKS(Firmware::SSH_NEW_RETRY_INTERVAL_MS));
            continue;
        }

        if (ssh_bind_accept(bind, session) == SSH_OK) {
            if (svc.isRunning()) {
                svc.handleSession(session);
            }
        }

        ssh_disconnect(session);
        ssh_free(session);

        // Brief yield between sessions so the accept() poll doesn't spin.
        vTaskDelay(pdMS_TO_TICKS(Firmware::SSH_ACCEPT_POLL_INTERVAL_MS));
    }

cleanup:
    ssh_bind_free(bind);
    svc.taskHandle = nullptr;
    vTaskDelete(NULL);
}
