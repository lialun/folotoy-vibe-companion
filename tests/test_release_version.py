"""Keep release tags and the packaged application version consistent."""
from __future__ import annotations

from pathlib import Path
import subprocess
import sys
import unittest


SCRIPT = Path(__file__).resolve().parents[1] / "tools" / "release_version.py"


class ReleaseVersionTests(unittest.TestCase):
    def parse(self, tag: str) -> subprocess.CompletedProcess[str]:
        return subprocess.run([sys.executable, str(SCRIPT), tag], capture_output=True, text=True)

    def test_plain_tags_produce_numeric_app_versions(self) -> None:
        for version in ("0.0.0", "0.8.4", "1.2.30"):
            with self.subTest(version=version):
                result = self.parse("v" + version)
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertEqual(result.stdout, version + "\n")

    def test_rejects_suffixes_incomplete_versions_and_unsafe_names(self) -> None:
        for tag in ("v0.8.4-vibe-companion", "v1.0.0-beta", "1.2.3", "v1.2", "v01.2.3",
                    "v1.02.3", "v1.2.03", "v1.2.3\n", "v1.2.3/other", "v1.2.3; echo unsafe"):
            with self.subTest(tag=tag):
                result = self.parse(tag)
                self.assertNotEqual(result.returncode, 0)
                self.assertEqual(result.stdout, "")
                self.assertIn("vMAJOR.MINOR.PATCH", result.stderr)


if __name__ == "__main__":
    unittest.main()
