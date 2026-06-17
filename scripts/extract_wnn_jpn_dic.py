#!/usr/bin/env python3
"""Extract WnnJpnDic.c byte arrays into a stable TSV source file."""

from __future__ import annotations

import argparse
import re
from pathlib import Path


ARRAY_RE = re.compile(r"static\s+NJ_UINT8\s+(\w+)\[\]\s*=\s*\{(.*?)\};", re.S)
HEX_RE = re.compile(r"0x([0-9a-fA-F]{1,2})")


def parse_arrays(source: str) -> list[tuple[str, list[int]]]:
    arrays: list[tuple[str, list[int]]] = []
    for match in ARRAY_RE.finditer(source):
        name = match.group(1)
        values = [int(x, 16) for x in HEX_RE.findall(match.group(2))]
        arrays.append((name, values))
    return arrays


def write_tsv(arrays: list[tuple[str, list[int]]], path: Path, width: int) -> None:
    with path.open("w", encoding="utf-8", newline="\n") as f:
        f.write("# OpenWnn Japanese dictionary blobs extracted from WnnJpnDic.c\n")
        f.write("# Columns: record, name, offset_decimal, hex_bytes\n")
        for name, values in arrays:
            f.write(f"meta\t{name}\tsize\t{len(values)}\n")
            for offset in range(0, len(values), width):
                chunk = values[offset : offset + width]
                hex_bytes = " ".join(f"{b:02x}" for b in chunk)
                f.write(f"data\t{name}\t{offset}\t{hex_bytes}\n")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("source", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--width", type=int, default=16)
    args = parser.parse_args()

    arrays = parse_arrays(args.source.read_text(encoding="utf-8"))
    if not arrays:
        raise SystemExit(f"no byte arrays found in {args.source}")
    args.output.parent.mkdir(parents=True, exist_ok=True)
    write_tsv(arrays, args.output, args.width)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
