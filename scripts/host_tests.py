#!/usr/bin/env python3
"""Build and run the host-side unit tests in tests/host (no ESP-IDF needed).

Requires CMake >= 3.16, a C compiler and network access on first configure
(Unity and cJSON are fetched at pinned tags). Build output goes to build-host/.
"""

from __future__ import annotations

import argparse
import os
import shutil
import subprocess
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parent.parent
SOURCE_DIR = ROOT / "tests" / "host"
DEFAULT_BUILD_DIR = ROOT / "build-host"


def run(cmd: list[str]) -> None:
    print("$ " + " ".join(cmd), flush=True)
    subprocess.run(cmd, cwd=ROOT, check=True)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path, default=DEFAULT_BUILD_DIR)
    parser.add_argument("--clean", action="store_true", help="delete the build dir first")
    parser.add_argument(
        "--no-sanitize", action="store_true", help="build without ASan/UBSan"
    )
    args = parser.parse_args()

    if shutil.which("cmake") is None:
        print("host_tests: cmake not found on PATH", file=sys.stderr)
        return 127

    build_dir: Path = args.build_dir
    if args.clean and build_dir.exists():
        shutil.rmtree(build_dir)

    run(
        [
            "cmake",
            "-S", str(SOURCE_DIR),
            "-B", str(build_dir),
            "-DCMAKE_BUILD_TYPE=Debug",
            f"-DHOST_TESTS_SANITIZE={'OFF' if args.no_sanitize else 'ON'}",
        ]
    )
    run(["cmake", "--build", str(build_dir), "--parallel", str(os.cpu_count() or 2)])
    run(["ctest", "--test-dir", str(build_dir), "--output-on-failure"])
    print("\nhost_tests: OK")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except subprocess.CalledProcessError as exc:
        print(f"\nhost_tests: failed with exit code {exc.returncode}", file=sys.stderr)
        raise SystemExit(exc.returncode)
