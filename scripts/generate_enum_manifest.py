#!/usr/bin/env python3
import argparse
import json
import re
from pathlib import Path


def read_enums(header):
    enums, current = {}, None
    for number, source in enumerate(header.splitlines(), 1):
        line = source.split("//", 1)[0].strip()
        if not line:
            continue
        if current is None:
            if not re.search(r"\benum\b", line):
                continue
            start = re.fullmatch(r"typedef\s+enum\s+(Nozzle\w+)\s*\{", line)
            if not start or start[1] in enums:
                raise ValueError(f"line {number}: unsupported/duplicate enum: {line}")
            current = start[1]
            enums[current] = {}
        elif re.fullmatch(r"}\s*" + current + r"\s*;", line):
            if not enums[current]:
                raise ValueError(f"line {number}: empty enum {current}")
            current = None
        else:
            entry = re.fullmatch(r"(NOZZLE_[A-Z0-9_]+)\s*=\s*(\d+)(?:u)?(?:\s*<<\s*(\d+))?\s*,?", line)
            if not entry or entry[1] in enums[current]:
                raise ValueError(f"line {number}: unsupported/duplicate member: {line}")
            shift = int(entry[3] or 0)
            if shift > 31:
                raise ValueError(f"line {number}: unsupported shift {shift}")
            value = int(entry[2]) << shift
            if value > 0xffffffff:
                raise ValueError(f"line {number}: unsupported value {value}")
            enums[current][entry[1]] = value
    if current is not None or not enums:
        raise ValueError("unterminated enum or no Nozzle enums found")
    return enums


def main():
    root = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description="Generate C enum values; reject unfamiliar syntax.")
    parser.add_argument("--header", type=Path, default=root / "include/nozzle/nozzle_c.h")
    parser.add_argument("--output", type=Path, default=root / "tools/enum_manifest.json")
    parser.add_argument("--check", action="store_true")
    parser.add_argument("--assertions", type=Path)
    args = parser.parse_args()
    enums = read_enums(args.header.read_text(encoding="utf-8"))
    manifest = json.dumps(enums, indent=2) + "\n"
    if args.check:
        if args.output.read_text(encoding="utf-8") != manifest:
            parser.error("enum manifest is stale; regenerate from the embedded header")
    else:
        args.output.write_text(manifest, encoding="utf-8")
    if args.assertions:
        assertions = ['#include "nozzle/nozzle_c.h"']
        for members in enums.values():
            for name, value in members.items():
                assertions.append(f'_Static_assert({name} == {value}, "{name}");')
        args.assertions.write_text("\n".join(assertions) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
