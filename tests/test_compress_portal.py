"""The transmitted representation must preserve every byte of the actual page."""
import gzip
import importlib.util
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("compress_portal", ROOT / "tools/compress_portal.py")
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


class PortalCompressionTests(unittest.TestCase):
    def test_actual_page_round_trip_and_reproducibility(self):
        page = (ROOT / "main/vibe/provisioning.html").read_bytes()
        packed = module.compress_page(page)
        self.assertEqual(gzip.decompress(packed), page)
        self.assertEqual(packed, module.compress_page(page))
        self.assertEqual(packed[4:8], b"\0" * 4)
        self.assertEqual(packed[9], 255)
        self.assertLess(len(packed), len(page) // 2)

    def test_empty_and_arbitrary_bytes(self):
        for data in (b"", bytes(range(256)), b"hello\0world"):
            self.assertEqual(gzip.decompress(module.compress_page(data)), data)


if __name__ == "__main__":
    unittest.main()
