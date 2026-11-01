#!/usr/bin/env python3
"""Check that the local sdkconfig carries every setting in sdkconfig.defaults.

sdkconfig is untracked and generated from sdkconfig.defaults only when it does not
exist yet. ESP-IDF never re-reads the defaults afterwards, so a fix that lands in
sdkconfig.defaults silently does not reach a machine with an older sdkconfig
(the LVGL malloc fix, 02fb020, went missing this way until 2026-10-07).

Exit status: 0 when every default matches (or there is no sdkconfig yet), 1 on a
mismatch. Options present in the defaults but absent from sdkconfig are listed
as notes only: Kconfig dropped them (no such option, or a dependency is off).
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
DEFAULTS = ROOT / "sdkconfig.defaults"
SDKCONFIG = ROOT / "sdkconfig"

_SET = re.compile(r"^(CONFIG_[A-Za-z0-9_]+)=(.*)$")
_UNSET = re.compile(r"^# (CONFIG_[A-Za-z0-9_]+) is not set$")


def parse(text: str) -> dict[str, str]:
    """Map each option to its value; "is not set" and "=n" both become "n"."""
    values: dict[str, str] = {}
    for line in text.splitlines():
        line = line.strip()
        m = _SET.match(line)
        if m:
            values[m.group(1)] = m.group(2)
            continue
        m = _UNSET.match(line)
        if m:
            values[m.group(1)] = "n"
    return values


def compare(defaults_text: str, sdkconfig_text: str) -> tuple[list[str], list[str]]:
    """Return (mismatches, notes) as printable lines."""
    current = parse(sdkconfig_text)
    mismatches: list[str] = []
    notes: list[str] = []
    for key, want in parse(defaults_text).items():
        if key not in current:
            notes.append(f"{key}={want} (not in sdkconfig: unknown option or its dependency is off)")
        elif current[key] != want:
            mismatches.append(f"{key}: defaults {want}, sdkconfig {current[key]}")
    return mismatches, notes


def check(defaults: Path = DEFAULTS, sdkconfig: Path = SDKCONFIG, *, verbose: bool = True) -> bool:
    """Print any mismatches; return True when sdkconfig agrees with the defaults."""
    if not sdkconfig.exists():
        return True  # the next build generates it from the defaults
    mismatches, notes = compare(defaults.read_text(), sdkconfig.read_text())
    if mismatches:
        print("! sdkconfig does not match sdkconfig.defaults:", file=sys.stderr)
        for line in mismatches:
            print(f"    {line}", file=sys.stderr)
        print(
            "  sdkconfig is never regenerated from the defaults once it exists. Set these in\n"
            "  sdkconfig (or `idf.py menuconfig`), or delete sdkconfig to regenerate it\n"
            "  (that drops any local settings not in the defaults).",
            file=sys.stderr,
            flush=True,
        )
    if verbose:
        for line in notes:
            print(f"  note: {line}")
    return not mismatches


def main() -> int:
    ok = check()
    if ok:
        print("check_sdkconfig: OK")
    return 0 if ok else 1


if __name__ == "__main__":
    raise SystemExit(main())
