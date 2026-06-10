// SPDX-License-Identifier: MIT
// Copyright (c) 2026 RenoSeven
//
// SSH service: serves the USB serial port over SSH.
//
// Owns the server host key, authenticates clients (password and/or public key,
// or optional no-auth), and bridges an accepted session to the SerialService
// ring buffers. The listener runs on its own FreeRTOS task; configuration is
// loaded from NVS and applied on begin()/restart().

#pragma once

#include <WString.h>
#include <atomic>

#include <libssh/libssh.h>

class SerialService;

// ============================================================
//                         SSH Config
// ============================================================

// Persisted SSH server host key and client authentication settings.
struct SshConfig {

    // --- Server authentication ---

    String privateKey; // Server private key.

    // --- Client authentication ---

    String username;      // Client login name.
    String password;      // Client password.
    String authorizedKey; // Client public key.
    bool allowNoAuth;     // Allow login without password or public key.

    // Read config from NVS (falls back to defaults for absent keys).
    static SshConfig read();

    // Write this config to NVS.
    void write() const;

    // Erase all keys in the ssh NVS namespace.
    static void clear();
};

// ============================================================
//                         SSH Service
// ============================================================

// SSH server (port 22) bridging clients to the SerialService.
class SshService {
  private:
    // --- Configuration ---

    String privateKey;    // Server private key.
    String publicKey;     // Server public key.
    String username;      // Client login name.
    String password;      // Client password.
    String authorizedKey; // Client public key.
    bool allowNoAuth;     // Allow login without password or public key.

    // --- Runtime state ---

    // taskHandle doubles as a liveness flag: the task clears it to nullptr on
    // exit, which end() waits on. taskRunning gates the task loop; clearing it
    // requests cooperative shutdown. connected is published for the status page
    // and read from the loop task.
    SerialService& serial;                 // Serial service for forwarding data.
    TaskHandle_t taskHandle = nullptr;     // SSH task handle (nullptr once stopped).
    std::atomic<bool> taskRunning = false; // SSH task run signal (begin() sets it; false requests shutdown).
    std::atomic<bool> connected = false;   // A client session is active.

    // True while the task should keep running.
    bool isRunning() const {
        return this->taskRunning.load(std::memory_order_relaxed);
    }

    // Authenticate the client and bridge one accepted session until it closes.
    // Frees the channel on exit; the caller frees the session.
    void handleSession(ssh_session session);

    // Pump bytes between an open, shell-ready channel and the serial ring buffers
    // until either side closes, a libssh error occurs, or shutdown is requested.
    void bridgeChannel(ssh_channel channel);

    // Listener task entry point: serve SSH on port 22, one client at a time,
    // until end() requests a stop.
    static void task(void* arg);

  public:
    explicit SshService(SerialService& serial) : serial(serial) {}

    // --- Key management ---

    // Return true if line is a valid OpenSSH public key ("type base64 [comment]").
    static bool isValidOpenSshPublicKey(const String& line);

    // --- Status ---

    // Return the OpenSSH public key.
    const String& getPublicKey() const {
        return this->publicKey;
    }

    // Return the username.
    const String& getUsername() const {
        return this->username;
    }

    // Return the authorized key.
    const String& getAuthorizedKey() const {
        return this->authorizedKey;
    }

    // Return true if no authentication is allowed.
    bool getAllowNoAuth() const {
        return this->allowNoAuth;
    }

    // True when at least one SSH client session is active.
    bool getConnected() const {
        return this->connected.load(std::memory_order_relaxed);
    }

    // --- Lifecycle ---

    // Load SSH config from NVS and start the listener task on port 22.
    void begin();

    // Signal the listener task, block until it exits, and clear all config.
    void end();

    // Re-read NVS and restart the listener; call after config changes.
    void restart();
};
