"""Exercise the actual release tools against isolated, non-firmware fixtures."""
from __future__ import annotations

import hashlib
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]


class ReleasePackageTests(unittest.TestCase):
    def setUp(self) -> None:
        self.directory = tempfile.TemporaryDirectory(prefix="vibe-release-")
        self.addCleanup(self.directory.cleanup)
        self.output = Path(self.directory.name)
        for name in ("FoloToy-AI-Passport-full.bin", "VibeCompanion-1.2.3-macOS.dmg",
                     "VibeCompanion-1.2.3-macOS.zip"):
            (self.output / name).write_bytes(b"packaging-test-fixture\n")
        subprocess.run([sys.executable, str(ROOT / "tools/package_notices.py"),
                        str(self.output / "THIRD_PARTY_NOTICES.txt")], check=True)

    def package(self) -> subprocess.CompletedProcess[str]:
        return subprocess.run([sys.executable, str(ROOT / "tools/package_release.py"),
                               str(self.output)], capture_output=True, text=True)

    def test_complete_package_and_portable_checksums(self) -> None:
        result = self.package()
        self.assertEqual(result.returncode, 0, result.stderr)
        sums = (self.output / "SHA256SUMS.txt").read_text().splitlines()
        self.assertEqual(len(sums), 6)
        for line in sums:
            digest, filename = line.split("  ", 1)
            self.assertEqual(Path(filename).name, filename)
            self.assertEqual(digest, hashlib.sha256((self.output / filename).read_bytes()).hexdigest())
        for language in ("", ".zh_CN"):
            guide = (self.output / f"INSTALL{language}.txt").read_text()
            self.assertIn("BlackHole", guide)
            self.assertNotIn("](install", guide)
        notes = (self.output / "RELEASE_NOTES.md").read_text()
        self.assertIn("Agent", notes)
        self.assertIn("macOS", notes)
        self.assertNotIn("0.6.0", notes)
        self.assertNotIn(str(ROOT), notes)
        before = (self.output / "SHA256SUMS.txt").read_bytes()
        self.assertEqual(self.package().returncode, 0)
        self.assertEqual(before, (self.output / "SHA256SUMS.txt").read_bytes())

    def test_rejects_missing_empty_and_ambiguous_payloads(self) -> None:
        zip_path = self.output / "VibeCompanion-1.2.3-macOS.zip"
        zip_path.unlink()
        self.assertNotEqual(self.package().returncode, 0)
        zip_path.touch()
        self.assertNotEqual(self.package().returncode, 0)
        zip_path.write_bytes(b"fixture")
        (self.output / "VibeCompanion-2.0.0-macOS.dmg").write_bytes(b"other-version")
        self.assertNotEqual(self.package().returncode, 0)
        self.assertFalse((self.output / "SHA256SUMS.txt").exists())

    def test_notices_retain_firmware_and_font_licenses(self) -> None:
        notices = (self.output / "THIRD_PARTY_NOTICES.txt").read_text()
        for phrase in ("Espressif Modified MIT", "Matthew Conte", "Marco Paland",
                       "Montserrat", "Font Awesome", "Apache License", "cJSON"):
            self.assertIn(phrase, notices)
        self.assertNotIn(str(ROOT), notices)

    def test_rejects_stale_zip_before_writing_release_metadata(self) -> None:
        (self.output / "VibeCompanion-0.9.0-macOS.zip").write_bytes(b"stale-version")
        result = self.package()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("matching", result.stderr)
        for name in ("SHA256SUMS.txt", "INSTALL.txt", "RELEASE_NOTES.md"):
            self.assertFalse((self.output / name).exists())


if __name__ == "__main__":
    unittest.main()
