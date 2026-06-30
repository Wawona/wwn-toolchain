#!/usr/bin/env python3
"""wwn-toolchain: ensure Android meson recipes use androidMesonSandbox.apply."""

from __future__ import annotations

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]

MESON_ANDROID_REL = (
    "dependencies/libs/fontconfig/android.nix",
    "dependencies/libs/freetype/android.nix",
    "dependencies/libs/pixman/android.nix",
    "dependencies/libs/cairo/android.nix",
    "dependencies/libs/glib/android.nix",
    "dependencies/libs/harfbuzz/android.nix",
    "dependencies/libs/fribidi/android.nix",
    "dependencies/libs/pango/android.nix",
    "dependencies/libs/xkbcommon/android.nix",
    "dependencies/libs/libwayland/android.nix",
)

APPLY_NEEDLE = "androidMesonSandbox.apply"
FORBIDDEN_INLINE = re.compile(
    r"postPatch\s*=\s*''[^'']*patchShebangs\s+\.",
    re.MULTILINE | re.DOTALL,
)


def main() -> int:
    errors: list[str] = []

    sandbox = ROOT / "dependencies/toolchains/android-meson-sandbox.nix"
    if not sandbox.is_file():
        errors.append("missing dependencies/toolchains/android-meson-sandbox.nix")

    default = ROOT / "dependencies/toolchains/default.nix"
    if default.is_file() and "androidMesonSandbox" not in default.read_text(encoding="utf-8"):
        errors.append("dependencies/toolchains/default.nix: androidMesonSandbox missing from androidArgs")

    for rel in MESON_ANDROID_REL:
        path = ROOT / rel
        if not path.is_file():
            errors.append(f"missing {rel}")
            continue
        content = path.read_text(encoding="utf-8")
        if APPLY_NEEDLE not in content:
            errors.append(f"{rel}: must use {APPLY_NEEDLE}")
        if FORBIDDEN_INLINE.search(content):
            errors.append(f"{rel}: inline patchShebangs; use androidMesonSandbox.apply")

    if errors:
        print("Android meson sandbox wiring check FAILED:")
        for err in errors:
            print(f"- {err}")
        return 1

    print("Android meson sandbox wiring check OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
