#!/usr/bin/env python3
"""Remove SDK startup-table launcher fingerprints from main BFpilot payloads."""

from __future__ import annotations

import pathlib
import sys


REPLACEMENTS = {
    b"libSceAppInstUtil.sprx": b"libBfpilotCleanMain.sx",
}


def scrub(path: pathlib.Path) -> int:
    data = path.read_bytes()
    changed = False
    for old, new in REPLACEMENTS.items():
        if len(old) != len(new):
            raise ValueError(f"replacement length mismatch for {old!r}")
        if old in data:
            data = data.replace(old, new)
            changed = True
    if changed:
        path.write_bytes(data)
    return 1 if changed else 0


def main(argv: list[str]) -> int:
    if len(argv) < 2:
        print("usage: scrub_main_payload.py <elf> [<elf>...]", file=sys.stderr)
        return 2
    for name in argv[1:]:
        path = pathlib.Path(name)
        changed = scrub(path)
        print(f"scrub-main-payload: {path} changed={changed}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
