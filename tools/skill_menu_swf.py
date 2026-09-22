#!/usr/bin/env python3
"""Rebuild or verify EA_SkillMenu.swf with the pinned FFDec toolchain."""

from __future__ import annotations

import argparse
import hashlib
import os
from pathlib import Path
import shutil
import subprocess
import sys


PINNED_FFDEC_VERSION = "25.1.3"


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("mode", choices=("rebuild", "verify"))
    parser.add_argument("--java", required=True, type=Path)
    parser.add_argument("--ffdec-jar", required=True, type=Path)
    parser.add_argument("--swf", required=True, type=Path)
    parser.add_argument("--scripts", required=True, type=Path)
    parser.add_argument("--work-dir", required=True, type=Path)
    return parser.parse_args()


def run(command: list[str]) -> str:
    result = subprocess.run(command, check=False, text=True, capture_output=True)
    output = result.stdout + result.stderr
    if result.returncode != 0:
        raise RuntimeError(f"FFDec command failed ({result.returncode}):\n{output}")
    return output


def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main() -> int:
    args = parse_args()
    for required in (args.java, args.ffdec_jar, args.swf, args.scripts):
        if not required.exists():
            raise FileNotFoundError(f"Required SWF tool input is missing: {required}")

    base = [str(args.java), "-jar", str(args.ffdec_jar)]
    help_output = run(base + ["-help"])
    if PINNED_FFDEC_VERSION not in help_output:
        raise RuntimeError(
            f"FFDec {PINNED_FFDEC_VERSION} is required; tool banner did not match."
        )

    args.work_dir.mkdir(parents=True, exist_ok=True)
    rebuilt = args.work_dir / "EA_SkillMenu.rebuilt.swf"
    rebuilt.unlink(missing_ok=True)
    run(base + ["-importScript", str(args.swf), str(rebuilt), str(args.scripts)])
    if not rebuilt.is_file() or rebuilt.stat().st_size == 0:
        raise RuntimeError("FFDec did not produce a rebuilt SWF.")

    if args.mode == "rebuild":
        temporary = args.swf.with_suffix(".swf.tmp")
        shutil.copyfile(rebuilt, temporary)
        os.replace(temporary, args.swf)
        print(f"Rebuilt {args.swf} ({digest(args.swf)})")
        return 0

    actual = digest(args.swf)
    expected = digest(rebuilt)
    if actual != expected:
        print("Committed SWF is stale.", file=sys.stderr)
        print(f"committed: {actual}", file=sys.stderr)
        print(f"rebuilt:   {expected}", file=sys.stderr)
        return 1
    print(f"Verified deterministic SWF: {actual}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
