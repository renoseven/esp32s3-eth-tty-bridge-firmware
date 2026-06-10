# SerialBridge

基于 SSH 的 USB 串口 —— 运行于 ESP32‑S3 的固件。

[English](README.md) | 简体中文

SerialBridge 把 ESP32‑S3 变成一个双通道串口适配器：本地通过 USB 以 CDC 设备身份出现
（`/dev/ttyACM*` / `COMx`），同时同一串口通道可经 Wi‑Fi 或以太网通过 SSH 远程访问，
并提供一个内置的小型 Web 配置界面。它适用于无显示器的实验台、机房，以及希望同时拥有
本地 USB 串口和网络远程串口、又不想为设备单独接一台 PC 的嵌入式场景。

## 目录

- [功能](#功能)
- [工作原理](#工作原理)
- [硬件](#硬件)
- [快速上手](#快速上手)
- [目标主机配置（Linux）](#目标主机配置linux)
- [状态指示灯](#状态指示灯)
- [网络](#网络)
- [SSH 访问](#ssh-访问)
- [Web 界面与 REST API](#web-界面与-rest-api)
- [配置参考](#配置参考)
- [依赖](#依赖)
- [构建与烧录](#构建与烧录)
- [Web 资源](#web-资源)
- [工程结构](#工程结构)
- [许可证](#许可证)

## 功能

- **USB‑CDC 串口** —— ESP32‑S3 以 USB‑CDC 设备身份枚举；接入主机后表现为
  `/dev/ttyACM*`（Windows 为 `COMx`）。同一数据流也可通过 SSH 远程访问。数据由固定
  在核心 1 的专用任务，通过无锁环形缓冲区（每个方向 8 KiB）搬运。
- **SSH 访问** —— `22` 端口的 SSH 服务，基于 LibSSH‑ESP32。支持密码与公钥认证，
  并提供可在 Web 界面切换的免认证模式；首次启动时自动生成持久化的 Ed25519 主机密钥。
- **Web 界面** —— `80` 端口的单页配置与状态界面：实时显示设备/网络/SSH 状态，
  配置网络与 SSH，切换语言（English / 简体中文），重启与恢复出厂。
- **Wi‑Fi** —— 支持模式设置（关闭 / 客户端 / AP），STA 与 AP 各自独立配置 SSID
  和密码。STA（客户端）模式下使用已保存的凭据联网；未配置或无法连接已保存网络时，
  回退为带强制门户（captive portal）的接入点，便于首次配置。
- **RGB 状态灯**（GPIO 21）—— 指示启动进度，运行时指示 USB 收发活动。详见
  [状态指示灯](#状态指示灯)。
- **配置持久化** —— 设备、Wi‑Fi、SSH 配置保存在 ESP32 的 NVS（非易失存储），
  重启与重新烧录后依然保留。

## 工作原理

固件被组织为一组服务，由顶层 `Runtime` 对象（`include/runtime.h`）组合。各服务按依赖
顺序构造并依次启动：

1. **Device** —— 从 NVS 加载设备名；提供版本、内存、运行时长等信息。
2. **Serial** —— 打开 USB‑CDC 接口，启动在 USB 与网络环形缓冲区之间搬运字节的桥接任务。
3. **Wi‑Fi** —— 启动 STA 或接入点并维护链路。
4. **SSH** —— 绑定 22 端口，接受客户端会话并转发到 Serial 环形缓冲区。
5. **Web** —— 注册 REST 路由并在 80 端口提供内置 Web 界面。

USB 桥接任务（核心 1）与 SSH 任务（核心 0）通过共享环形缓冲区协作，串口桥接响应迅速且
无需忙轮询。通过 Web 界面所做的配置更改会写入 NVS，并只重启受影响的服务（Wi‑Fi 或
SSH）即可生效，无需整机重启。

## 硬件

- 具备原生 USB 的 ESP32‑S3 开发板（基于 **ESP32‑S3‑ETH** 开发）。
- **GPIO 21** 上的可寻址 RGB LED（多数 ESP32‑S3 开发板自带的 WS2812 类 LED）。
- 一根从开发板 USB 口连到主机 PC 的 USB 线（ESP32‑S3 在主机上显示为 CDC 串口设备）。

## 快速上手

1. **构建并烧录**固件（见[构建与烧录](#构建与烧录)）：

```sh
make flash
```

2. **首次启动** —— 未配置 Wi‑Fi 时，开发板会启动接入点。连接名为
   `SerialBridge-AP-XXXX`（`XXXX` 是取自 MAC 地址的十六进制后缀）的 Wi‑Fi，
   密码 `12345678`。强制门户会自动打开 Web 界面；否则在浏览器访问网关 IP。
3. 在 Web 界面**配置 Wi‑Fi** 并保存，开发板会以客户端模式加入你的网络。记下分配到的
   IP（见“运行状态”页，或通过路由器 / mDNS 主机名查看）。
4. 将**开发板的 USB 口**接到目标机器——目标机器上会识别为 `/dev/ttyACM0`
   （Windows 为 `COMx`）。
5. 在目标机器上**启用串口登录**（Linux 示例）：

```sh
sudo systemctl enable --now serial-getty@ttyACM0.service
```

6. 通过 SSH **连接**（默认凭据 `admin` / `admin`）：

```sh
ssh admin@<设备IP>
```

登录后会先显示一段简短提示（见 [SSH 访问](#ssh-访问)），随后你输入的内容会转发给
目标设备的串口，其输出会回传到你的终端。

## 目标主机配置（Linux）

ESP32‑S3 接入 Linux 目标主机后，会识别为 `/dev/ttyACM0`。要在该串口上开放登录
终端，启用 systemd 的 serial‑getty 服务：

```sh
sudo systemctl enable --now serial-getty@ttyACM0.service
```

这样会在 `/dev/ttyACM0` 上生成一个 `getty`（登录提示符），通过 SerialBridge 的 SSH
连入时即可获得目标机器的完整终端会话。

**提示：**

- 如果目标主机上有多个 CDC 设备，实际设备名可能不同（`ttyACM1` 等）。插入后用
  `ls /dev/ttyACM*` 或 `dmesg | grep ttyACM` 确认。
- 若需要在重启后保持稳定的设备名，可创建 udev 规则，按 USB 序列号或路径创建符号链接。
- 如果不需要登录终端（只需原始串口 I/O），可跳过 `serial-getty`，直接让应用程序
  读写该 TTY。

## 状态指示灯

GPIO 21 上的 RGB LED 用于指示启动进度，随后指示运行时的 USB 活动。

**启动阶段**（每个阶段初始化时短暂显示一种纯色）：

| 颜色 | 阶段              |
| ---- | ----------------- |
| 红   | 加载设备配置      |
| 橙   | Serial / USB 桥接 |
| 绿   | Wi‑Fi             |
| 黄   | SSH               |
| 蓝   | Web 服务          |
| 白   | 启动完成 / 就绪   |

**运行时活动**（在稳定状态上叠加的短暂闪烁）：

| 颜色 | 含义                 |
| ---- | -------------------- |
| 绿   | USB 发送（到目标）   |
| 黄   | USB 接收（来自目标） |

LED 亮度经过缩小处理（见 `Firmware::LED_BRIGHTNESS`），观感更柔和。

## 网络

SerialBridge 支持三种 Wi‑Fi 模式（`WifiMode`），可通过 Web 界面的**模式**设置
选择。STA 和 AP 各自拥有独立的 SSID 与密码：

- **客户端（STA）** —— 加入已保存的 STA SSID。配置好 Wi‑Fi 后这是正常工作模式。
- **接入点（AP）** —— 开启一个 WPA2 接入点（默认 SSID `SerialBridge-AP-XXXX`，密码
  `12345678`），并运行强制门户 DNS，使任何浏览器请求都跳到 Web 界面。
- **关闭** —— 禁用 Wi‑Fi；等同于 AP 模式。

故障转移逻辑（时间常量见 `include/firmware.h`）：

- 首次 STA 连接尝试 **10 秒**超时后回退为接入点。
- STA 链路断开时会持续重连；断开 **60 秒**后重新开启接入点以便重新配置。
- 接入点模式下若存在已保存 SSID，会定期（每 **10 分钟**）重试加入该网络。

通过 Web 界面保存网络设置后，Wi‑Fi 服务会延迟片刻再重启，以便 HTTP 响应先完成。

## SSH 访问

- 监听 TCP **22** 端口。
- **主机密钥**：首次启动 SSH 时生成 Ed25519 服务器密钥并持久化到 NVS。OpenSSH 格式的
  公钥会显示在 Web 界面“运行状态”页，便于核对指纹。
- **客户端认证**（任一已配置方式均可通过）：
    - **密码** —— 默认用户名 `admin`，密码 `admin`。
    - **公钥** —— 在 Web 界面粘贴一行 OpenSSH `authorized_keys`（密钥类型最高
      RSA‑8192，最多 1536 字符）。
    - **免认证** —— 可选；启用后客户端无需密码或密钥即可登录，默认关闭。
- 同一时间桥接一个客户端会话到 USB‑CDC 目标。
- **会话提示** —— 认证成功后、开始桥接前，服务器会输出一段提示信息：

```sh
This SSH session is bridged to the device serial port.
Escape sequence is '~' '.' (at line start)
```

> 安全提示：默认值（`admin`/`admin`、接入点密码 `12345678`）只为首次配置方便。
> 在不受信任的网络中部署前，请更改 SSH 凭据与 Wi‑Fi 设置，并保持免认证模式关闭，
> 除非你清楚其暴露风险。

## Web 界面与 REST API

Web 界面（`80` 端口）是一个由小型 REST API 支撑的单页应用。POST 接口成功时返回
`204 No Content`；错误使用 `application/problem+json` 响应体，带 `type` 字段
（由界面渲染为本地化消息）。静态资源与 API 共享 `include/restful.h` 中定义的常量。

| 方法 | 路径                    | 用途                                 |
| ---- | ----------------------- | ------------------------------------ |
| GET  | `/`                     | Web 界面（HTML）                     |
| GET  | `/style.css`            | 样式表                               |
| GET  | `/script.js`            | 界面脚本                             |
| GET  | `/status`               | 设备、Wi‑Fi、SSH 运行状态快照        |
| GET  | `/config/device/fields` | 设备配置 + 字段元数据                |
| POST | `/config/device/save`   | 保存设备配置（设备名）               |
| POST | `/config/device/reset`  | 重置设备配置为默认                   |
| GET  | `/config/wifi/fields`   | Wi‑Fi 配置 + 字段元数据              |
| POST | `/config/wifi/save`     | 保存 Wi‑Fi 配置（模式、SSID、密码）  |
| POST | `/config/wifi/reset`    | 重置 Wi‑Fi 配置为默认                |
| GET  | `/config/ssh/fields`    | SSH 配置 + 字段元数据                |
| POST | `/config/ssh/save`      | 保存 SSH 配置（用户、密码、密钥...） |
| POST | `/config/ssh/reset`     | 重置 SSH 配置为默认                  |
| POST | `/factory-reset`        | 清除全部配置并重启                   |
| POST | `/reboot`               | 不改配置直接重启                     |

## 配置参考

出厂默认值与校验限制定义在 `include/firmware.h`。

**默认值**

| 配置项         | 默认值                                     |
| -------------- | ------------------------------------------ |
| 设备名         | `SerialBridge-XXXX`（追加 MAC 后缀）       |
| Wi‑Fi 模式     | AP（接入点）                               |
| STA SSID/密码  | 空（无已保存 STA 网络）                    |
| AP SSID        | `SerialBridge-AP-XXXX`（追加 MAC 后缀）    |
| AP 密码        | `12345678`                                 |
| SSH 用户名     | `admin`                                    |
| SSH 密码       | `admin`                                    |
| SSH 授权公钥   | 无                                         |
| SSH 允许免认证 | 关闭                                       |
| SSH 主机密钥   | Ed25519，首次启动生成                      |

**限制**

| 字段         | 范围                                                   |
| ------------ | ------------------------------------------------------ |
| 设备名       | 1–32 字符（字母、数字、连字符；不能以连字符开头/结尾） |
| Wi‑Fi SSID   | 1–32 字符                                              |
| Wi‑Fi 密码   | 8–63 字符，或留空使用开放网络                          |
| SSH 用户名   | 1–32 字符                                              |
| SSH 密码     | 最多 64 字符                                           |
| SSH 授权公钥 | 最多 1536 字符（OpenSSH 行，RSA‑8192 上限）            |

**端口**：Web `80`，SSH `22`，强制门户 DNS `53`（仅接入点模式）。

## 依赖

Arduino 库（版本固定在 `sketch.yaml`）：

- [ESP Async WebServer](https://github.com/ESP32Async/ESPAsyncWebServer) `3.11.0`
- [Async TCP](https://github.com/ESP32Async/AsyncTCP) `3.4.10`
- [ArduinoJson](https://arduinojson.org/) `7.4.3`
- [LibSSH‑ESP32](https://github.com/ewpa/LibSSH-ESP32) `5.8.0`

构建工具：

- [`arduino-cli`](https://arduino.github.io/arduino-cli/) 及 ESP32 开发板支持包
  （`esp32:esp32` `3.3.8`）。
- 辅助脚本（`scripts/`）需要 Python 3。

## 构建与烧录

`Makefile` 是对 `scripts/build.py` 的轻量封装：

```sh
make build                 # 编译（默认 profile：release）
make flash                 # 编译并上传（自动检测串口）
make flash PORT=/dev/ttyACM0
make build PROFILE=debug   # debug | debug-full | release | release-full
make boards                # 列出开发板与串口
make clean                 # 清除 arduino-cli 构建缓存
make help                  # 完整的目标/变量说明
```

**Profile**（定义在 `sketch.yaml`）：

| Profile        | 说明                              |
| -------------- | --------------------------------- |
| `release`      | 普通构建/上传（默认）             |
| `release-full` | release，并在上传时擦除整片 flash |
| `debug`        | 输出详细调试日志                  |
| `debug-full`   | 调试日志，并擦除整片 flash        |

**Make 变量**：`PROFILE`、`PORT`、`VERBOSE=1`、`FORCE_ASSETS=1`、`NO_GIT_HASH=1`。

构建时会把 git 短 HEAD 哈希注入为 `FW_VERSION_ID`（通过 `build_opt.h`），因此固件版本
形如 `v0.1a (a63d8ff)`。用 `NO_GIT_HASH=1` 可跳过注入。

**WSL 说明**：在 WSL 下构建、而开发板接在 Windows 上时，构建脚本会自动调用
`scripts/attach_wsl_usb.py`（基于 [`usbipd-win`](https://github.com/dorssel/usbipd-win)）
把 USB 设备转发进 WSL。首次使用需在管理员 PowerShell 中执行一次 `usbipd bind`——
脚本会在需要时打印确切命令。

如需 IDE/clangd 补全，可生成编译数据库：

```sh
cd scripts && ./generate_compile_commands.py
```

## Web 资源

Web 界面源码位于 `assets/`（`index.html`、`style.css`、`script.js`）。
`scripts/embed_assets.py` 会对每个文件进行 gzip 压缩，以 PROGMEM 字节数组嵌入到
`include/assets.h`（声明）和 `src/assets.cpp`（数据）。构建时输入变化会自动运行，
或用 `make assets` 强制生成。**请编辑 `assets/` 下的源文件，不要改生成的
`include/assets.h` 或 `src/assets.cpp`。** 请保持 `include/restful.h` 中的 REST 路由
与 JSON 键常量与 `assets/script.js` 同步，并保持错误 `type` 字符串与
`assets/script.js` 中 I18N 的 `err` 段同步。

## 工程结构

```
assets/    Web 界面源码（嵌入固件）
include/   头文件（runtime、device、wifi、ssh、serial、web、restful、nvram、led、ringbuf 等）
src/       各服务实现 + 生成的 assets.cpp
scripts/   build.py、embed_assets.py、generate_compile_commands.py、WSL USB 辅助脚本
Makefile   对 scripts/build.py 的便捷封装
sketch.yaml  arduino-cli profile、开发板 FQBN、固定版本的库
```

## 许可证

基于 [MIT 许可证](LICENSE)发布。Copyright (c) 2026 RenoSeven。
