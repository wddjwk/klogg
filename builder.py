#!/usr/bin/env python3
"""Cross-platform build entry point for klogg.

Usage:
    python builder.py prepare
    python builder.py build
    python builder.py release
    python builder.py clean
    python builder.py clangd

Options:
    --msvc-setup PATH   Path to MSVC setup_x64.bat (Windows only)
    --qt-root PATH      Path to Qt installation root
    --build-type TYPE   Build type (Release, Debug, RelWithDebInfo)
"""
import argparse
import platform
import subprocess
import sys
from pathlib import Path

SCRIPT_DIR = Path(__file__).resolve().parent / "scripts"
PROJECT_ROOT = SCRIPT_DIR.parent


def run_windows(args: argparse.Namespace) -> int:
    ps_script = SCRIPT_DIR / "build.ps1"
    cmd = [
        "pwsh", "-NoProfile", "-ExecutionPolicy", "Bypass",
        "-File", str(ps_script),
        "-Action", args.action,
    ]
    if args.msvc_setup:
        cmd += ["-MsvcSetup", args.msvc_setup]
    if args.qt_root:
        cmd += ["-QtRoot", args.qt_root]
    if args.build_type:
        cmd += ["-BuildType", args.build_type]

    print(f"[builder.py] Running: {' '.join(cmd)}")
    return subprocess.call(cmd)


def run_linux(args: argparse.Namespace) -> int:
    # Placeholder for future Linux build support
    print("[builder.py] Linux build not yet implemented.")
    print("[builder.py] Use cmake directly:")
    print(f"  mkdir -p {PROJECT_ROOT}/build && cd {PROJECT_ROOT}/build")
    print(f"  cmake -G Ninja -DCMAKE_BUILD_TYPE={args.build_type or 'Release'} ..")
    print("  cmake --build .")
    return 1


def run_macos(args: argparse.Namespace) -> int:
    # Placeholder for future macOS build support
    print("[builder.py] macOS build not yet implemented.")
    return 1


def main() -> int:
    parser = argparse.ArgumentParser(description="Build klogg")
    parser.add_argument(
        "action",
        choices=["prepare", "build", "release", "clean", "clangd"],
        help="Build action to perform",
    )
    parser.add_argument("--msvc-setup", help="Path to MSVC setup_x64.bat (Windows)")
    parser.add_argument("--qt-root", help="Path to Qt installation root")
    parser.add_argument(
        "--build-type",
        default="Release",
        help="CMake build type (default: Release)",
    )
    args = parser.parse_args()

    system = platform.system()
    if system == "Windows":
        return run_windows(args)
    elif system == "Linux":
        return run_linux(args)
    elif system == "Darwin":
        return run_macos(args)
    else:
        print(f"[builder.py] Unsupported platform: {system}")
        return 1


if __name__ == "__main__":
    sys.exit(main())
