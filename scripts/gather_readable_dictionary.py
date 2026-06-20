#!/usr/bin/env python3
"""Gather split human-readable dictionary TSV files into one generated TSV."""

from __future__ import annotations

import argparse
from pathlib import Path


INPUT_HEADER = [
    "yomi",
    "candidate",
    "freq",
    "left_connection_id",
    "right_connection_id",
]


def iter_rows(path: Path):
    lines = path.read_text(encoding="utf-8-sig").splitlines()
    if not lines:
        return
    header = lines[0].split("\t")
    if header != INPUT_HEADER:
        raise SystemExit(f"{path}: unexpected header")
    dic_type = path.stem
    for lineno, line in enumerate(lines[1:], 2):
        if not line:
            continue
        fields = line.split("\t")
        if len(fields) != len(INPUT_HEADER):
            raise SystemExit(f"{path}:{lineno}: expected {len(INPUT_HEADER)} columns")
        yield dic_type, fields


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("input_dir", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()

    paths = sorted(args.input_dir.glob("*.tsv"))
    if not paths:
        raise SystemExit(f"no TSV files found in {args.input_dir}")

    args.output.parent.mkdir(parents=True, exist_ok=True)
    with args.output.open("w", encoding="utf-8", newline="\n") as f:
        f.write("dic_type\t" + "\t".join(INPUT_HEADER) + "\n")
        for path in paths:
            for dic_type, fields in iter_rows(path):
                f.write(dic_type + "\t" + "\t".join(fields) + "\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
