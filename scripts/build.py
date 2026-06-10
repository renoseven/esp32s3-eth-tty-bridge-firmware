#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 RenoSeven

"""Compile and optionally upload firmware via arduino-cli profile.

Flow (make build / make flash):
  1. Inject FW_VERSION_ID from git (restored on exit)
  2. Print build context (profile, version, port if known)
  3. Embed assets into src/assets.cpp when inputs changed
  4. Run arduino-cli compile
  5. Flash only: resolve upload port, then arduino-cli upload
"""

from __future__ import annotations

import argparse
import atexit
import glob
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path

# --- Constants ---

DEFAULT_PROFILE = "release"
BUILD_OPT_BASE = "-Iinclude"
SERIAL_GLOBS = ("/dev/ttyACM*", "/dev/ttyUSB*")
SERIAL_PORT_RE = re.compile(r"^/dev/tty(ACM|USB)")

# --- Paths ---


def repo_root() -> Path:
    return Path(__file__).resolve().parent.parent


def scripts_dir() -> Path:
    return Path(__file__).resolve().parent


def local_cmd(name: str | None = None) -> str:
    return f"./{name or Path(__file__).name}"


def script_path(name: str) -> Path:
    return scripts_dir() / name


def run_script(script: Path, *args: str) -> None:
    subprocess.check_call([str(script.resolve()), *args])


# --- Argparse ---


def create_parser() -> argparse.ArgumentParser:
    build_cmd = local_cmd()
    parser = argparse.ArgumentParser(
        description="Compile and optionally upload SerialBridge firmware.",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=(
            "examples:\n"
            "  compile:\n"
            f"    {build_cmd}\n"
            f"    {build_cmd} -m debug\n"
            f"    {build_cmd} --force-assets\n"
            "\n"
            "  upload:\n"
            f"    {build_cmd} -u\n"
            f"    {build_cmd} -u -p /dev/ttyACM0\n"
            "\n"
            "  utilities:\n"
            f"    {build_cmd} --board-list\n"
            f"    {build_cmd} --clean"
        ),
    )

    compile_group = parser.add_argument_group("compile")
    compile_group.add_argument(
        "-m",
        "--profile",
        default=DEFAULT_PROFILE,
        help=f"sketch profile in sketch.yaml (default: {DEFAULT_PROFILE})",
    )
    compile_group.add_argument(
        "--force-assets",
        action="store_true",
        help="regenerate src/assets.cpp before compiling",
    )
    compile_group.add_argument(
        "-v",
        "--verbose",
        action="store_true",
        help="show verbose arduino-cli output",
    )
    compile_group.add_argument(
        "--no-git-hash",
        action="store_true",
        help="do not inject FW_VERSION_ID into build_opt.h",
    )

    upload_group = parser.add_argument_group("upload")
    upload_group.add_argument(
        "-u",
        "--upload",
        action="store_true",
        help="upload firmware after compiling",
    )
    upload_group.add_argument(
        "-p",
        "--port",
        help="upload port (default: auto-detect, e.g. /dev/ttyACM0)",
    )

    utility_group = parser.add_argument_group("utilities")
    utility_group.add_argument(
        "--board-list",
        action="store_true",
        help="list connected boards and serial ports",
    )
    utility_group.add_argument(
        "--clean",
        action="store_true",
        help="remove arduino-cli build cache",
    )
    return parser


# --- Git / build_opt ---


def write_build_opt(path: Path, sha: str | None = None) -> None:
    lines = [BUILD_OPT_BASE]
    if sha:
        lines.append(f'-DFW_VERSION_ID=\\"{sha}\\"')
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def git_short_head(base: Path) -> str | None:
    try:
        return subprocess.check_output(
            ["git", "-C", str(base), "rev-parse", "--short", "HEAD"],
            text=True,
            stderr=subprocess.DEVNULL,
        ).strip()
    except subprocess.CalledProcessError:
        return None


def is_git_repo(base: Path) -> bool:
    try:
        subprocess.run(
            ["git", "-C", str(base), "rev-parse", "--is-inside-work-tree"],
            check=True,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        )
        return True
    except subprocess.CalledProcessError:
        return False


def setup_git_hash(build_opt_path: Path, *, inject: bool) -> str | None:
    """Inject FW_VERSION_ID into build_opt.h; restore on exit. Returns short SHA if injected."""
    state = {"wrote": False}

    def restore() -> None:
        if state["wrote"]:
            write_build_opt(build_opt_path)

    atexit.register(restore)

    if not inject or not is_git_repo(build_opt_path.parent):
        return None

    sha = git_short_head(build_opt_path.parent)
    if not sha:
        return None

    write_build_opt(build_opt_path, sha)
    state["wrote"] = True
    return sha


def read_define_str(path: Path, name: str) -> str | None:
    """Read a string-literal #define value (e.g. FW_VERSION_NAME) from a header."""
    try:
        text = path.read_text(encoding="utf-8")
    except OSError:
        return None
    match = re.search(r"#define\s+" + re.escape(name) + r'\s+"([^"]*)"', text)
    return match.group(1) if match else None


def firmware_version(base: Path, sha: str | None) -> str | None:
    """Compose the full version string ("name (id)") matching firmware.h's FW_VERSION."""
    header = base / "include" / "firmware.h"
    name = read_define_str(header, "FW_VERSION_NAME")
    version_id = sha or read_define_str(header, "FW_VERSION_ID")
    if name and version_id:
        return f"{name} ({version_id})"
    return name or version_id


# --- Output ---


def display_profile(profile: str) -> str:
    return "-".join(part.capitalize() for part in profile.split("-"))


def print_build_context(
    *,
    profile: str,
    version: str | None,
    port: str | None = None,
) -> None:
    if version:
        print(f"Firmware Version: {version}", flush=True)
    else:
        print("Firmware Version: (not set)", flush=True)
    print(f"Profile: {display_profile(profile)}", flush=True)
    if port is not None:
        print(f"Port: {port}", flush=True)


# --- Serial / upload ---


def detect_serial_ports() -> list[str]:
    ports: list[str] = []
    for pattern in SERIAL_GLOBS:
        ports.extend(sorted(glob.glob(pattern)))
    if ports:
        return ports

    try:
        out = subprocess.check_output(["arduino-cli", "board", "list"], text=True, stderr=subprocess.DEVNULL)
    except (subprocess.CalledProcessError, FileNotFoundError):
        return []

    return [parts[0] for line in out.splitlines()[1:] if (parts := line.split()) and SERIAL_PORT_RE.match(parts[0])]


def attach_wsl_usb() -> None:
    run_script(script_path("attach_wsl_usb.py"))


def detect_upload_port() -> str:
    """Auto-detect upload port; on WSL, attach USB when none is found."""
    candidates = detect_serial_ports()

    if not candidates and os.environ.get("WSL_DISTRO_NAME"):
        print("No serial port found.")
        attach_wsl_usb()
        candidates = detect_serial_ports()

    if not candidates:
        print("Error: No serial port found.", file=sys.stderr)
        raise SystemExit(1)

    if len(candidates) > 1:
        print(f"Error: Found multiple serial ports: {' '.join(candidates)}", file=sys.stderr)
        raise SystemExit(1)

    return candidates[0]


# --- Assets ---


def embed_assets(force: bool) -> None:
    args = ("--force",) if force else ()
    run_script(script_path("embed_assets.py"), *args)


# --- arduino-cli ---


def arduino_build_path(sketch: Path, profile: str) -> Path | None:
    try:
        out = subprocess.check_output(
            [
                "arduino-cli",
                "compile",
                "--profile",
                profile,
                "--show-properties=expanded",
                str(sketch),
            ],
            stderr=subprocess.DEVNULL,
            text=True,
        )
    except (subprocess.CalledProcessError, FileNotFoundError):
        return None

    prefix = "build.path="
    for line in out.splitlines():
        if line.startswith(prefix):
            return Path(line[len(prefix) :])
    return None


def show_board_list() -> None:
    subprocess.check_call(["arduino-cli", "board", "list"])


def clean_build_cache(base: Path, profile: str) -> None:
    build_path = arduino_build_path(base, profile)
    if build_path and build_path.is_dir():
        print(f"Removed build cache: {build_path}", flush=True)
        shutil.rmtree(build_path)
        return
    print("No build cache to remove.", flush=True)


def run_arduino_compile(
    base: Path,
    *,
    profile: str,
    verbose: bool,
) -> None:
    cmd = ["arduino-cli", "compile", "--profile", profile]
    if verbose:
        cmd.append("-v")
    cmd.append(str(base))

    if verbose:
        print(f"  Command: {' '.join(cmd)}", flush=True)
    subprocess.check_call(cmd)


def run_arduino_upload(
    base: Path,
    *,
    profile: str,
    port: str,
    verbose: bool,
) -> None:
    cmd = ["arduino-cli", "upload", "--profile", profile, "-p", port]
    if verbose:
        cmd.append("-v")
    cmd.append(str(base))

    if verbose:
        print(f"  Command: {' '.join(cmd)}", flush=True)
    subprocess.check_call(cmd)


# --- Build pipeline ---


def compile_firmware(
    base: Path,
    *,
    profile: str,
    upload: bool,
    port: str | None,
    verbose: bool,
    force_assets: bool,
    ignore_git_hash: bool,
) -> None:
    sha = setup_git_hash(base / "build_opt.h", inject=ignore_git_hash)
    version = firmware_version(base, sha)

    # 1. Print build context
    print_build_context(
        profile=profile,
        version=version,
        port=port if upload and port else None,
    )
    print(flush=True)

    # 2. Embed assets
    print("Embedding assets...", flush=True)
    embed_assets(force_assets)
    print(flush=True)

    # 3. Compile firmware
    print("Building firmware...", flush=True)
    run_arduino_compile(
        base,
        profile=profile,
        verbose=verbose,
    )
    print(flush=True)
    if not upload:
        print("Done.", flush=True)
        return

    # 4. Upload firmware
    print("Uploading firmware...", flush=True)
    upload_port = port
    if not upload_port:
        upload_port = detect_upload_port()
    run_arduino_upload(
        base,
        profile=profile,
        port=upload_port,
        verbose=verbose,
    )
    print(flush=True)

    # 5. Done
    print("Done.", flush=True)


# --- Main ---


def main() -> int:
    args = create_parser().parse_args()

    if shutil.which("arduino-cli") is None:
        print("Error: Could not find arduino-cli in PATH.", file=sys.stderr)
        print("  Install: https://arduino.github.io/arduino-cli/", file=sys.stderr)
        return 1

    base = repo_root()
    os.chdir(base)

    if args.board_list:
        show_board_list()
        return 0

    if args.clean:
        clean_build_cache(base, args.profile)
        return 0

    compile_firmware(
        base,
        profile=args.profile,
        upload=args.upload,
        port=args.port,
        verbose=args.verbose,
        force_assets=args.force_assets,
        ignore_git_hash=not args.no_git_hash,
    )
    return 0


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
