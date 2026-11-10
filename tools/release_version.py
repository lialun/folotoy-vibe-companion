#!/usr/bin/env python3
"""Validate a vMAJOR.MINOR.PATCH tag and print its application version."""
from __future__ import annotations

import argparse
import re


RELEASE_TAG = re.compile(r"v(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)")


def version_from_tag(tag: str) -> str:
    if not RELEASE_TAG.fullmatch(tag):
        raise ValueError("release tag must use vMAJOR.MINOR.PATCH (for example v0.8.4)")
    return tag[1:]


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("tag")
    args = parser.parse_args()
    try:
        version = version_from_tag(args.tag)
    except ValueError as error:
        parser.error(str(error))
    print(version)


if __name__ == "__main__":
    main()
