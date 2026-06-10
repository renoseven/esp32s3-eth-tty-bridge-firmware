# SerialBridge

USB Serial over SSH — firmware for the ESP32-S3.

English | [简体中文](README_zh.md)

SerialBridge turns an ESP32-S3 into a dual‑access serial adapter: locally it
appears as a USB‑CDC serial port (`/dev/ttyACM*` / `COMx`), and the same serial
channel is reachable remotely over SSH via Wi‑Fi or Ethernet. A small built‑in
web UI handles configuration. It is meant for headless lab benches, server
rooms, and embedded targets where you want both a local USB serial port and
remote access over the network without a dedicated PC tethered to the device.

## Contents

- [Features](#features)
- [How It Works](#how-it-works)
- [Hardware](#hardware)
- [Quick Start](#quick-start)
- [Host Setup (Linux)](#host-setup-linux)
- [Status LED](#status-led)
- [Networking](#networking)
- [SSH Access](#ssh-access)
- [Web UI & REST API](#web-ui--rest-api)
- [Configuration Reference](#configuration-reference)
- [Dependencies](#dependencies)
- [Build & Flash](#build--flash)
- [Web Assets](#web-assets)
- [Project Layout](#project-layout)
- [License](#license)

## Features

- **USB‑CDC serial port** — the ESP32‑S3 enumerates as a USB‑CDC device; when
  plugged into a host it appears as `/dev/ttyACM*` (`COMx` on Windows). The
  same data stream is also accessible over SSH. Data is moved through lock‑free
  ring buffers (8 KiB each direction) by a dedicated task on core 1.
- **SSH access** — SSH server on port `22`, backed by LibSSH‑ESP32. Supports
  password and public‑key authentication, with an optional no‑auth mode that
  can be toggled from the web UI. A persistent Ed25519 host key is generated on
  first start.
- **Web UI** — a single‑page configuration and status page on port `80`:
  live device/network/SSH status, network and SSH settings, language switch
  (English / 简体中文), reboot, and factory reset.
- **Wi‑Fi** — supports a mode setting (Off / Client / AP) with separate
  SSID and password for each mode. In Client (STA) mode it joins the saved
  SSID; falls back to a configuration access point with a captive portal for
  first‑time setup or when the saved network is unreachable.
- **RGB status LED** (GPIO 21) — signals boot progress, then USB TX/RX
  activity at runtime. See [Status LED](#status-led).
- **Persistent configuration** — device, Wi‑Fi, and SSH settings are stored in
  the ESP32 NVS (non‑volatile storage) and survive reboots and reflashing.

## How It Works

The firmware is organized as a set of services composed by a top‑level
`Runtime` object (`include/runtime.h`). Services are constructed in dependency
order and booted sequentially:

1. **Device** — loads the device name from NVS; provides version, memory, and
   uptime info.
2. **Serial** — opens the USB‑CDC interface and starts the bridge task that
   copies bytes between USB and the network ring buffers.
3. **Wi‑Fi** — brings up STA or the configuration AP and maintains the link.
4. **SSH** — binds port 22, accepts a client session, and forwards it to the
   serial ring buffers.
5. **Web** — registers the REST routes and serves the embedded web UI on
   port 80.

The USB bridge task (core 1) and the SSH task (core 0) cooperate through
shared ring buffers, keeping the serial bridge responsive without busy‑polling.
Configuration changes made over the web UI are written to NVS and applied by
restarting only the affected service (Wi‑Fi or SSH), without a full reboot.

## Hardware

- ESP32‑S3 board with native USB (developed on the **ESP32‑S3‑ETH**).
- Addressable RGB LED on **GPIO 21** (the WS2812‑style LED present on most
  ESP32‑S3 dev boards).
- A USB cable from the board's USB port to the host PC (the ESP32‑S3 appears
  as a CDC serial device on the host).

## Quick Start

1. **Build & flash** the firmware (see [Build & Flash](#build--flash)):

```sh
make flash
```

2. **First boot** — with no Wi‑Fi configured, the board starts a configuration
   access point. Join the Wi‑Fi network `SerialBridge-AP-XXXX` (where `XXXX`
   is a hex suffix derived from the MAC address) using passphrase `12345678`.
   A captive portal opens the web UI automatically; otherwise browse to the
   gateway IP.
3. **Configure Wi‑Fi** in the web UI, save, and the board joins your network as
   a client. Note the assigned IP (shown on the Status tab, or via your
   router / mDNS hostname).
4. **Plug the board** into the target machine's USB port — the target sees it
   as `/dev/ttyACM0` (`COMx` on Windows).
5. **Enable serial login** on the target (Linux example):

```sh
sudo systemctl enable --now serial-getty@ttyACM0.service
```

6. **Connect over SSH** (default credentials `admin` / `admin`):

```sh
ssh admin@<device-ip>
```

After login, a short banner is shown (see [SSH Access](#ssh-access)), then
everything you type is forwarded to the target's serial port and its output
streams back to your terminal.

## Host Setup (Linux)

When the ESP32‑S3 is plugged into a Linux machine, it appears as
`/dev/ttyACM0`. To expose a login shell on that serial port, enable the
systemd serial‑getty service:

```sh
sudo systemctl enable --now serial-getty@ttyACM0.service
```

This spawns a `getty` (login prompt) on `/dev/ttyACM0`, so anyone connecting
via SSH through the SerialBridge gets a full terminal session on the target.

**Tips:**

- The device name may differ (`ttyACM1`, etc.) if other CDC devices are
  present. Check `ls /dev/ttyACM*` or `dmesg | grep ttyACM` after plugging in.
- To make the port assignment stable across reboots, create a udev rule that
  symlinks the device by its USB serial number or path.
- If you only need raw serial I/O (no login shell), skip `serial-getty` and
  point your application at the TTY directly.

## Status LED

The RGB LED on GPIO 21 indicates boot progress and then runtime USB activity.

**Boot sequence** (each stage briefly shows a solid color as it initializes):

| Color  | Stage                 |
| ------ | --------------------- |
| Red    | Device config load    |
| Orange | Serial / USB bridge   |
| Green  | Wi‑Fi                 |
| Yellow | SSH                   |
| Blue   | Web server            |
| White  | Boot complete / ready |

**Runtime activity** (brief flashes overlaid on the steady state):

| Color  | Meaning              |
| ------ | -------------------- |
| Green  | USB TX (to target)   |
| Yellow | USB RX (from target) |

LED brightness is scaled down (see `Firmware::LED_BRIGHTNESS`) so the indicator
is comfortable to look at.

## Networking

SerialBridge supports three Wi‑Fi modes (`WifiMode`), selectable via the
**mode** setting in the web UI. STA and AP each have their own SSID and
password, configured independently:

- **Client (STA)** — joins the saved STA SSID. This is the normal operating
  mode once Wi‑Fi is configured.
- **Access Point (AP)** — opens a WPA2 access point (default SSID
  `SerialBridge-AP-XXXX`, passphrase `12345678`) and runs a captive‑portal DNS
  so any browser request lands on the web UI.
- **Off** — Wi‑Fi disabled; treated the same as AP mode.

Failover logic (timings in `include/firmware.h`):

- First STA connect attempt times out after **10 s**, then falls back to the AP.
- While in STA with the link down, the board retries the connection; after
  **60 s** down it reopens the AP so you can reconfigure.
- While in AP mode with a saved SSID, it periodically (every **10 min**) retries
  joining the saved network.

After saving network settings over the web UI, the Wi‑Fi service restarts on a
short delay so the HTTP response can complete first.

## SSH Access

- Listens on TCP port **22**.
- **Host key**: an Ed25519 server key is generated on first SSH startup and
  persisted in NVS. The OpenSSH‑format public key is shown on the web UI Status
  tab so you can verify the fingerprint.
- **Client authentication** (any configured method is accepted):
    - **Password** — default username `admin`, password `admin`.
    - **Public key** — paste an OpenSSH `authorized_keys` line in the web UI
      (key types up to RSA‑8192, max 1536 characters).
    - **No‑auth** — optional; when enabled, clients may log in without a password
      or key. Disabled by default.
- A single client session is bridged to the USB‑CDC target at a time.
- **Session banner** — after authentication, the server prints a message before
  bridging begins:

```sh
This SSH session is bridged to the device serial port.
Escape sequence is '~' '.' (at line start)
```

> Security note: the defaults (`admin`/`admin`, AP passphrase `12345678`) are
> for first‑time setup convenience. Change the SSH credentials and Wi‑Fi
> settings before deploying on an untrusted network, and keep no‑auth mode off
> unless you understand the exposure.

## Web UI & REST API

The web UI (port `80`) is a single page backed by a small REST API. POST
endpoints return `204 No Content` on success; errors use an
`application/problem+json` body with a `type` key (rendered as a localized
message by the UI). Static assets and the API share the constants defined in
`include/restful.h`.

| Method | Path                    | Purpose                                    |
| ------ | ----------------------- | ------------------------------------------ |
| GET    | `/`                     | Web UI (HTML)                              |
| GET    | `/style.css`            | Stylesheet                                 |
| GET    | `/script.js`            | UI script                                  |
| GET    | `/status`               | Device, Wi‑Fi, and SSH runtime snapshot    |
| GET    | `/config/device/fields` | Device config + field metadata             |
| POST   | `/config/device/save`   | Save device config (device name)           |
| POST   | `/config/device/reset`  | Reset device config to defaults            |
| GET    | `/config/wifi/fields`   | Wi‑Fi config + field metadata              |
| POST   | `/config/wifi/save`     | Save Wi‑Fi config (mode, SSID, password)   |
| POST   | `/config/wifi/reset`    | Reset Wi‑Fi config to defaults             |
| GET    | `/config/ssh/fields`    | SSH config + field metadata                |
| POST   | `/config/ssh/save`      | Save SSH config (user, password, key, ...) |
| POST   | `/config/ssh/reset`     | Reset SSH config to defaults               |
| POST   | `/factory-reset`        | Erase all saved config, then reboot        |
| POST   | `/reboot`               | Reboot without changing config             |

## Configuration Reference

Factory defaults and validation limits live in `include/firmware.h`.

**Defaults**

| Setting              | Default                                        |
| -------------------- | ---------------------------------------------- |
| Device name          | `SerialBridge-XXXX` (MAC suffix appended)      |
| Wi‑Fi mode           | AP (access point)                              |
| STA SSID/password    | empty (no saved STA network)                   |
| AP SSID              | `SerialBridge-AP-XXXX` (MAC suffix appended)   |
| AP password          | `12345678`                                     |
| SSH username         | `admin`                                        |
| SSH password         | `admin`                                        |
| SSH authorized key   | none                                           |
| SSH allow no‑auth    | off                                            |
| SSH host key         | Ed25519, generated on first start              |

**Limits**

| Field              | Range                                                             |
| ------------------ | ----------------------------------------------------------------- |
| Device name        | 1–32 chars (letters, digits, hyphens; no leading/trailing hyphen) |
| Wi‑Fi SSID         | 1–32 chars                                                        |
| Wi‑Fi password     | 8–63 chars, or empty for an open network                          |
| SSH username       | 1–32 chars                                                        |
| SSH password       | up to 64 chars                                                    |
| SSH authorized key | up to 1536 chars (OpenSSH line, RSA‑8192 max)                     |

**Ports**: web `80`, SSH `22`, captive‑portal DNS `53` (AP mode only).

## Dependencies

Arduino libraries (pinned in `sketch.yaml`):

- [ESP Async WebServer](https://github.com/ESP32Async/ESPAsyncWebServer) `3.11.0`
- [Async TCP](https://github.com/ESP32Async/AsyncTCP) `3.4.10`
- [ArduinoJson](https://arduinojson.org/) `7.4.3`
- [LibSSH‑ESP32](https://github.com/ewpa/LibSSH-ESP32) `5.8.0`

Build tooling:

- [`arduino-cli`](https://arduino.github.io/arduino-cli/) with the ESP32 board
  package (`esp32:esp32` `3.3.8`).
- Python 3 for the helper scripts under `scripts/`.

## Build & Flash

The `Makefile` is a thin wrapper over `scripts/build.py`:

```sh
make build                 # compile (default profile: release)
make flash                 # compile and upload (auto-detect port)
make flash PORT=/dev/ttyACM0
make build PROFILE=debug   # debug | debug-full | release | release-full
make boards                # list boards and serial ports
make clean                 # clear the arduino-cli build cache
make help                  # full target/variable reference
```

**Profiles** (defined in `sketch.yaml`):

| Profile        | Description                            |
| -------------- | -------------------------------------- |
| `release`      | Normal build/upload (default)          |
| `release-full` | Release + erase entire flash on upload |
| `debug`        | Verbose debug logging                  |
| `debug-full`   | Debug logging + erase entire flash     |

**Make variables**: `PROFILE`, `PORT`, `VERBOSE=1`, `FORCE_ASSETS=1`,
`NO_GIT_HASH=1`.

The short git HEAD hash is injected at build time as `FW_VERSION_ID` (via
`build_opt.h`), so the firmware version reports as e.g. `v0.1a (a63d8ff)`. Use
`NO_GIT_HASH=1` to skip injection.

**WSL note**: when building under WSL with the board attached to Windows, the
build script automatically invokes `scripts/attach_wsl_usb.py` (which uses
[`usbipd-win`](https://github.com/dorssel/usbipd-win)) to forward the USB
device into WSL. First‑time use requires a one‑time `usbipd bind` in an
Administrator PowerShell — the script prints the exact command if needed.

To get IDE/clangd completion, generate a compile database:

```sh
cd scripts && ./generate_compile_commands.py
```

## Web Assets

The web UI lives in `assets/` (`index.html`, `style.css`, `script.js`).
`scripts/embed_assets.py` gzip-compresses each file and embeds them as PROGMEM
byte arrays into `include/assets.h` (declarations) and `src/assets.cpp` (data).
The script runs automatically when inputs change, or use `make assets` to force.
**Edit the files under `assets/`, not the generated `include/assets.h` or
`src/assets.cpp`.** Keep the REST route and JSON key constants in
`include/restful.h` in sync with `assets/script.js`, and the error `type` strings
in sync with the I18N `err` section in `assets/script.js`.

## Project Layout

```
assets/    web UI sources (embedded into firmware)
include/   headers (runtime, device, wifi, ssh, serial, web, restful, nvram, led, ringbuf, ...)
src/       service implementations + generated assets.cpp
scripts/   build.py, embed_assets.py, generate_compile_commands.py, WSL USB helpers
Makefile   convenience targets over scripts/build.py
sketch.yaml  arduino-cli profiles, board FQBN, pinned libraries
```

## License

Released under the [MIT License](LICENSE). Copyright (c) 2026 RenoSeven.
