#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 RenoSeven

"""Attach Espressif USB serial/JTAG from Windows into WSL via usbipd.

Flow:
  1. Verify WSL environment
  2. Run attach_wsl_usb.ps1 on Windows (usbipd attach)
  3. Wait for /dev/ttyACM* or /dev/ttyUSB* to appear
"""

from __future__ import annotations

import argparse
import glob
import os
import shutil
import subprocess
import sys
import time
from pathlib import Path

# --- Constants ---

DEFAULT_VIDPID = "303a:1001"
PS1_NAME = "attach_wsl_usb.ps1"
SERIAL_GLOBS = ("/dev/ttyACM*", "/dev/ttyUSB*")
WAIT_TIMEOUT_SEC = 15
WAIT_INTERVAL_SEC = 0.5
ATTACH_ATTEMPTS = 3
ATTACH_RETRY_DELAY_SEC = 2

# --- Paths ---


def script_dir() -> Path:
    return Path(__file__).resolve().parent


def local_cmd() -> str:
    return f"./{Path(__file__).name}"


# --- Argparse ---


def create_parser() -> argparse.ArgumentParser:
    cmd = local_cmd()
    return argparse.ArgumentParser(
        description="Attach an Espressif USB device from Windows to WSL (usbipd).",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=(
            "examples:\n"
            f"  {cmd}\n"
            "\n"
            "environment:\n"
            f"  VIDPID={DEFAULT_VIDPID}    hardware ID to match (default: Espressif)\n"
            "  AUTO_ATTACH=1              keep device attached in background (default: 1)\n"
            "\n"
            "notes:\n"
            "  Run from WSL with the board connected and at least one WSL terminal open.\n"
            "  First-time setup requires one Administrator PowerShell bind:\n"
            "    usbipd bind --busid <BUSID>"
        ),
    )


# --- Windows / usbipd ---


def wslpath_windows(path: Path) -> str:
    return subprocess.check_output(["wslpath", "-w", str(path)], text=True).strip()


def run_powershell_attach(ps1_path: Path, vidpid: str, auto_attach: str, *, quiet: bool = False) -> int:
    cmd = [
        "powershell.exe",
        "-NoProfile",
        "-ExecutionPolicy",
        "Bypass",
        "-File",
        wslpath_windows(ps1_path),
        "-VidPid",
        vidpid,
        "-AutoAttach",
        auto_attach,
    ]
    if quiet:
        cmd.append("-Quiet")
    proc = subprocess.run(cmd, check=False)
    return proc.returncode


# --- Serial port ---


def find_serial_ports() -> list[Path]:
    ports: list[Path] = []
    for pattern in SERIAL_GLOBS:
        ports.extend(Path(path) for path in glob.glob(pattern))
    return sorted(ports)


def wait_for_serial_port(timeout_sec: int = WAIT_TIMEOUT_SEC) -> bool:
    attempts = int(timeout_sec / WAIT_INTERVAL_SEC)
    for _ in range(attempts):
        if find_serial_ports():
            return True
        time.sleep(WAIT_INTERVAL_SEC)
    return False


# --- Attach flow ---


def check_wsl_environment() -> int | None:
    if shutil.which("powershell.exe") is None:
        print("Error: This system is not WSL environment.", file=sys.stderr)
        return 1

    ps1_path = script_dir() / PS1_NAME
    if not ps1_path.is_file():
        print("Error: Missing the Windows USB attach helper.", file=sys.stderr)
        return 1

    return None


def attach_on_windows(vidpid: str, auto_attach: str, *, quiet: bool = False) -> int:
    ps1_path = script_dir() / PS1_NAME
    return run_powershell_attach(ps1_path, vidpid, auto_attach, quiet=quiet)


def wait_for_serial_or_warn(*, announce: bool) -> int:
    if announce:
        print("Waiting for serial port ...", flush=True)
    if wait_for_serial_port():
        return 0

    print(
        f"Warning: Device attached, but no serial port appeared within {WAIT_TIMEOUT_SEC}s.",
        file=sys.stderr,
    )
    print("  Try: dmesg | tail", file=sys.stderr)
    return 1


def attach_usb(vidpid: str, auto_attach: str) -> int:
    if err := check_wsl_environment():
        return err

    for attempt in range(ATTACH_ATTEMPTS):
        label = attempt + 1
        if attempt > 0:
            time.sleep(ATTACH_RETRY_DELAY_SEC)

        print(
            f"Attaching USB device ({label}/{ATTACH_ATTEMPTS}) ...",
            flush=True,
        )

        status = attach_on_windows(
            vidpid,
            auto_attach,
            quiet=(attempt < ATTACH_ATTEMPTS - 1),
        )
        if status != 0:
            continue

        if wait_for_serial_or_warn(announce=(attempt == 0)) == 0:
            return 0

    return 1


def main() -> int:
    create_parser().parse_args()
    return attach_usb(
        os.environ.get("VIDPID", DEFAULT_VIDPID),
        os.environ.get("AUTO_ATTACH", "1"),
    )


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except SystemExit as exc:
        raise SystemExit(0 if exc.code is None else exc.code) from None
    except subprocess.CalledProcessError as exc:
        raise SystemExit(exc.returncode or 1) from None
    except KeyboardInterrupt:
        print("\nInterrupted.", file=sys.stderr)
        raise SystemExit(130) from None
    except Exception as exc:
        print(f"Error: {exc}", file=sys.stderr)
        raise SystemExit(1) from None
