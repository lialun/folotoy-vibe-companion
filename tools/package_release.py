#!/usr/bin/env python3
"""Prepare GitHub Release instructions, notes and portable file checksums."""
from __future__ import annotations

import argparse
import hashlib
from pathlib import Path


def document(path: Path) -> str:
    # The repository's reciprocal language navigation is not a release link.
    return path.read_text(encoding="utf-8").split("\n", 1)[1].lstrip()


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path)
    args = parser.parse_args()
    root = Path(__file__).resolve().parent.parent
    output = args.directory.resolve()
    dmg = sorted(output.glob("VibeCompanion-*-macOS.dmg"))
    if len(dmg) != 1:
        parser.error("release directory must contain exactly one Mac DMG")
    zip_file = dmg[0].with_suffix(".zip")
    if sorted(output.glob("VibeCompanion-*-macOS.zip")) != [zip_file]:
        parser.error("release directory must contain only the ZIP matching its Mac DMG")
    files = [output / "FoloToy-AI-Passport-full.bin", dmg[0], zip_file,
             output / "THIRD_PARTY_NOTICES.txt"]
    for path in files:
        if not path.is_file() or not path.stat().st_size:
            parser.error(f"missing or empty release file: {path.name}")
    for language in ("", ".zh_CN"):
        path = output / f"INSTALL{language}.txt"
        path.write_text(document(root / f"docs/vibe/install{language}.md"), encoding="utf-8")
        files.append(path)
    notes = [document(root / f"docs/vibe/release-notes{language}.md")
             for language in (".zh_CN", "")]
    (output / "RELEASE_NOTES.md").write_text("\n---\n\n".join(notes), encoding="utf-8")
    sums = []
    for path in files:
        digest = hashlib.sha256()
        with path.open("rb") as stream:
            for chunk in iter(lambda: stream.read(1024 * 1024), b""):
                digest.update(chunk)
        sums.append(f"{digest.hexdigest()}  {path.name}\n")
    (output / "SHA256SUMS.txt").write_text("".join(sums), encoding="utf-8")
    print(f"Release package: {len(files)} files, instructions, notes and SHA-256 checksums")


if __name__ == "__main__":
    main()
