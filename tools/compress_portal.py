#!/usr/bin/env python3
"""Build the phone portal's deterministic gzip representation, without runtime RAM."""
from __future__ import annotations
import argparse
import gzip
import io
from pathlib import Path


def compress_page(data: bytes) -> bytes:
    output = io.BytesIO()
    with gzip.GzipFile(filename="", mode="wb", compresslevel=9, fileobj=output, mtime=0) as stream:
        stream.write(data)
    return output.getvalue()


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("destination", type=Path)
    args = parser.parse_args()
    packed = compress_page(args.source.read_bytes())
    args.destination.parent.mkdir(parents=True, exist_ok=True)
    if not args.destination.exists() or args.destination.read_bytes() != packed:
        args.destination.write_bytes(packed)


if __name__ == "__main__":
    main()
