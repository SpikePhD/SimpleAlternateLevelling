#!/usr/bin/env python3
"""Fail if the shipped configuration contains debug or fast-test values."""

from __future__ import annotations

import argparse
import json
from pathlib import Path


# The shipped JSON is packaged verbatim. Local test values belong in the
# deployed SimpleAlternateLevelling.user.json instead.
EXPECTED = {
    ("debug", "verbose"): False,
    ("leveling", "xp_base"): 75.0,
    ("leveling", "xp_increase"): 25.0,
    ("leveling", "xp_cap"): 10000000.0,
}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--defaults", required=True, type=Path)
    args = parser.parse_args()

    config = json.loads(args.defaults.read_text(encoding="utf-8"))
    errors = []
    for path, expected in EXPECTED.items():
        node = config
        for key in path:
            node = node.get(key) if isinstance(node, dict) else None
        if node != expected or type(node) is not type(expected):
            errors.append(f"{'.'.join(path)} is {node!r}; release default is {expected!r}")

    for error in errors:
        print(error)
    if errors:
        print("Shipped config contains non-release values; move test values to the user override file.")
        return 1
    print("Shipped config uses release defaults.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
