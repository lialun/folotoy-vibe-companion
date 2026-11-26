"""The bench identity follows source changes, never external plans/results."""
import importlib.util
from pathlib import Path
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("voice_bench", ROOT / "tools/voice_bench.py")
bench = importlib.util.module_from_spec(spec)
spec.loader.exec_module(bench)


class VoiceBenchBuildTests(unittest.TestCase):
    def test_external_scenarios_do_not_change_identity(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            for filename in ("macos/app/App.swift", "macos/app/Engine.swift", "macos/voice-bench/VoiceBenchMain.swift",
                             "macos/agents/adapter.py", "version.txt", "LICENSE", "tools/voice_bench.py"):
                path = root / filename
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text("fixture")
            def digest():
                return bench.source_digest(root, "compiler", "sdk", "arm64")
            before = digest()
            plan = root / "macos/voice-bench/plans/plan.json"
            plan.parent.mkdir()
            plan.write_text('{"stages":[]}')
            (root / "results.json").write_text('{"pass":false}')
            self.assertEqual(before, digest())
            # The production app entry point is neither compiled nor copied.
            (root / "macos/app/App.swift").write_text("changed normal GUI")
            self.assertEqual(before, digest())
            self.assertNotIn(root / "macos/app/App.swift", bench.source_files(root))
            (root / "macos/app/Engine.swift").write_text("changed shared core")
            self.assertNotEqual(before, digest())
            self.assertNotEqual(digest(), bench.source_digest(root, "new compiler", "sdk", "arm64"))

    def test_entry_points_have_separate_build_identities(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            for name in ("macos/app/Engine.swift", "macos/voice-bench/VoiceBenchMain.swift",
                         "macos/voice-bench/VoiceBenchCLI.swift", "version.txt", "LICENSE", "tools/voice_bench.py"):
                path = root / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text("fixture")
            gui = bench.source_digest(root, "compiler", "sdk", "target")
            backend = bench.source_digest(root, "compiler", "sdk", "target", True)
            self.assertNotEqual(gui, backend)
            (root / "macos/voice-bench/VoiceBenchCLI.swift").write_text("new backend")
            self.assertEqual(gui, bench.source_digest(root, "compiler", "sdk", "target"))
            self.assertNotEqual(backend, bench.source_digest(root, "compiler", "sdk", "target", True))
            self.assertNotIn(root / "macos/voice-bench/VoiceBenchMain.swift", bench.source_files(root, True))

    def test_backend_cache_rejects_binary_changes(self):
        import hashlib
        import json
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            binary = root / "VoiceWireBench"
            binary.write_bytes(b"verified binary")
            (root / "manifest.json").write_text(json.dumps({
                "sourceDigest": "source", "binarySha256": hashlib.sha256(binary.read_bytes()).hexdigest(),
            }))
            self.assertTrue(bench.cached_backend(root, "source"))
            binary.write_bytes(b"changed binary")
            self.assertFalse(bench.cached_backend(root, "source"))
            self.assertFalse(bench.cached_backend(root, "different source"))

    def test_identity_and_compile_mode_are_separate(self):
        self.assertNotEqual(bench.BUNDLE_ID, "io.github.vibecompanion.mac")
        self.assertIn("VIBE_VOICE_BENCH", bench.FLAGS)


if __name__ == "__main__":
    unittest.main()
