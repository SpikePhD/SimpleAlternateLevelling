#!/usr/bin/env python3
"""Dependency-free static validation for the native/SWF/translation contract."""

from __future__ import annotations

import argparse
from pathlib import Path


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--native", required=True, type=Path)
    parser.add_argument("--actionscript", required=True, type=Path)
    parser.add_argument("--translation", required=True, type=Path)
    return parser.parse_args()


def require(text: str, token: str, source: Path) -> None:
    if token not in text:
        raise RuntimeError(f"{source} is missing required UI contract token: {token}")


def main() -> int:
    args = parse_args()
    native = args.native.read_text(encoding="utf-8")
    actionscript = args.actionscript.read_text(encoding="utf-8")
    raw_translation = args.translation.read_bytes()
    if not raw_translation.startswith(b"\xff\xfe"):
        raise RuntimeError("Translation must be UTF-16 LE with a BOM.")
    translation = raw_translation[2:].decode("utf-16-le")

    shared_tokens = (
        "EA_Init",
        "EA_UpdateSkill",
        "EA_UpdatePoints",
        "EA_SetClosing",
        "EA_OnAllocate",
        "EA_OnConfirm",
        "EA_OnReset",
    )
    for token in shared_tokens:
        require(native, token, args.native)
        require(actionscript, token, args.actionscript)
    require(native, "std::array<RE::GFxValue, 18>", args.native)
    for key in ("$SAL_SKILL_POINTS_LABEL", "$SAL_CONFIRM", "$SAL_RESET"):
        require(native, key, args.native)
        require(translation, key + "\t", args.translation)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
