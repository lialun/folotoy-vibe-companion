"""Check every non-ASCII firmware string against the actual linked CJK asset."""
from pathlib import Path
import re

root = Path(__file__).resolve().parents[2]
font = (root / "main/vibe/vibe_font.c").read_text()
covered = {chr(int(cp, 16)) for cp in re.findall(r"/\* U\+([0-9A-Fa-f]{4,6})", font)}
used = set()
for path in (root / "main/vibe").rglob("*"):
    if path.suffix not in (".c", ".h", ".cc", ".inc") or path.name in ("vibe_font.c", "font_20.c"):
        continue
    for literal in re.findall(r'"([^"\\]*(?:\\.[^"\\]*)*)"', path.read_text()):
        used.update(char for char in literal if ord(char) > 127)
missing = used - covered
assert not missing, "Font missing: " + " ".join(f"U+{ord(c):04X}" for c in sorted(missing))
large = (root / "main/vibe/font_20.c").read_text()
large_covered = {chr(int(cp, 16)) for cp in re.findall(r"/\* U\+([0-9A-Fa-f]{4,6})", large)}
assert used <= large_covered, "20px font missing: " + repr(used-large_covered)
print(f"Firmware static glyph coverage: PASS ({len(used)} non-ASCII characters)")
