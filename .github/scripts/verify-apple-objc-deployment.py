#!/usr/bin/env python3
"""Fail when an Apple-mobile recipe compiles C without the ObjC deployment floor.

C flags and Meson c_args do not apply to .m / .mm. Clang then targets the
iPhoneOS SDK and emits _objc_release_xN. dyld aborts on iOS 13, 14, and 15.
"""

from __future__ import annotations

import sys
from pathlib import Path

NAMES = {
    "ios.nix",
    "apple-mobile.nix",
    "apple-cmake-toolchain.nix",
    "compositor-apple-mobile.nix",
}


def interesting(path: Path, root: Path) -> bool:
    rel = str(path.relative_to(root))
    if any(part in rel for part in ("runtime-work", "macos.nix", "android")):
        return False
    return path.name in NAMES or rel.endswith("platforms/ios.nix")


def check(root: Path) -> list[str]:
    fails: list[str] = []
    if not root.is_dir():
        return [f"missing root {root}"]
    for path in root.rglob("*.nix"):
        if not interesting(path, root):
            continue
        text = path.read_text(errors="replace")
        rel = str(path.relative_to(root))
        if "CMAKE_C_FLAGS" in text and "CMAKE_OBJC_FLAGS" not in text:
            fails.append(f"{rel}: CMAKE_C_FLAGS without CMAKE_OBJC_FLAGS")
        if "c_args" in text and "objc_args" not in text:
            fails.append(f"{rel}: c_args without objc_args")
    return fails


def main(argv: list[str]) -> int:
    roots = [Path(a).resolve() for a in argv[1:]] or [
        Path(__file__).resolve().parents[2]
    ]
    fails: list[str] = []
    for root in roots:
        for item in check(root):
            fails.append(f"{root.name}: {item}")
    if fails:
        print("Apple ObjC deployment floor missing:", file=sys.stderr)
        for item in fails:
            print(f"  {item}", file=sys.stderr)
        return 1
    print("objc deployment floor ok: " + ", ".join(r.name for r in roots))
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
