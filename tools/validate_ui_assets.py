#!/usr/bin/env python3
"""Dependency-free static validation for the native/SWF/translation contract."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import struct
import zlib


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--native", required=True, type=Path)
    parser.add_argument("--actionscript", required=True, type=Path)
    parser.add_argument("--translation", required=True, type=Path)
    parser.add_argument("--swf", required=True, type=Path)
    parser.add_argument("--defaults", required=True, type=Path)
    return parser.parse_args()


def require(text: str, token: str, source: Path) -> None:
    if token not in text:
        raise RuntimeError(f"{source} is missing required UI contract token: {token}")


def root_tags(path: Path):
    data = path.read_bytes()
    if data[:3] == b"CWS":
        data = data[:8] + zlib.decompress(data[8:])
    elif data[:3] != b"FWS":
        raise RuntimeError(f"Unsupported SWF format: {path}")
    if len(data) < 9:
        raise RuntimeError(f"Truncated SWF: {path}")

    rect_bits = data[8] >> 3
    offset = 8 + (5 + 4 * rect_bits + 7) // 8 + 4  # RECT, frame rate, frame count
    while offset + 2 <= len(data):
        header = struct.unpack_from("<H", data, offset)[0]
        offset += 2
        tag = header >> 6
        length = header & 0x3F
        if length == 0x3F:
            if offset + 4 > len(data):
                break
            length = struct.unpack_from("<I", data, offset)[0]
            offset += 4
        if offset + length > len(data):
            break
        yield tag, data[offset:offset + length]
        offset += length
        if tag == 0:
            return
    raise RuntimeError(f"Truncated SWF tag stream: {path}")


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
        "EA_OnDeallocate",
        "EA_OnConfirm",
        "EA_OnReset",
    )
    for token in shared_tokens:
        require(native, token, args.native)
        require(actionscript, token, args.actionscript)
    require(native, "std::array<RE::GFxValue, 7>", args.native)
    for key in ("$SAL_SKILL_POINTS_LABEL", "$SAL_CONFIRM", "$SAL_RESET", "$SAL_ALLOC_LEVEL",
                "$SAL_ALLOC_REMAINING", "$SAL_ALLOC_CARRIED", "$SAL_ALLOC_BONUS", "$SAL_ALLOC_MAX", "$SAL_GROUP_COMBAT",
                "$SAL_GROUP_MAGIC", "$SAL_GROUP_STEALTH", "$SAL_ALLOC_HINT"):
        require(native, key, args.native)
        require(translation, key + "\t", args.translation)

    tags = list(root_tags(args.swf))
    if any(tag in (4, 26, 70) for tag, _ in tags):
        raise RuntimeError("Skill menu SWF has a static main-stage object; the panel must be drawn by EA_Init only.")
    if not any(tag == 12 and b"EA_Init" in payload for tag, payload in tags):
        raise RuntimeError("Skill menu SWF is missing the EA_Init frame script.")
    translated = {line.split("\t", 1)[0] for line in translation.splitlines() if "\t" in line}
    defaults = json.loads(args.defaults.read_text(encoding="utf-8"))
    def walk(node: dict, prefix: str = ""):
        for key, value in node.items():
            if key.startswith("_") or key == "config_version":
                continue
            path = f"{prefix}.{key}" if prefix else key
            if path == "notifications.messages":
                continue
            if isinstance(value, dict):
                yield from walk(value, path)
            elif isinstance(value, (int, float, bool)) or path == "starting_skills.mode":
                yield "$SAL_SETTING_" + path.replace(".", "_").upper()
    missing = set(walk(defaults)) - translated
    if missing:
        raise RuntimeError(f"Settings translation keys missing: {sorted(missing)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
