#!/usr/bin/env python3
"""Build the macOS voice bench or its existing-permission command-line backend.

The signed bundle contains code/resources only. Plans and results stay outside
it; changing a plan never changes the build identity or requires a rebuild.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import platform
import plistlib
import shutil
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
BUNDLE_ID = "io.github.vibecompanion.voicebench"
FLAGS = ["-swift-version", "5", "-whole-module-optimization", "-O", "-D", "VIBE_VOICE_BENCH"]


def source_files(root: Path, backend: bool = False) -> list[Path]:
    return sorted([p for p in (root / "macos/app").glob("*.swift") if p.name != "App.swift"]
                  + [p for p in (root / "macos/voice-bench").glob("*.swift")
                     if p.name not in ({"VoiceBenchMain.swift"} if backend else {"VoiceBenchCLI.swift"})])


def source_digest(root: Path, compiler: str, sdk: str, target: str, backend: bool = False) -> str:
    files = source_files(root, backend) + sorted(p for p in (root / "macos/agents").rglob("*") if p.is_file())
    files += [root / "version.txt", root / "LICENSE", root / "tools/voice_bench.py"]
    digest = hashlib.sha256(json.dumps([compiler, sdk, target, FLAGS, backend], sort_keys=True).encode())
    for path in files:
        digest.update(path.relative_to(root).as_posix().encode() + b"\0")
        digest.update(path.read_bytes() + b"\0")
    return digest.hexdigest()


def run(*args: str) -> str:
    return subprocess.check_output(args, text=True).strip()


def cached(app: Path, digest: str) -> bool:
    try:
        info = plistlib.loads((app / "Contents/Info.plist").read_bytes())
        if info.get("CFBundleIdentifier") != BUNDLE_ID or info.get("VoiceBenchSourceDigest") != digest:
            return False
        subprocess.run(["codesign", "--verify", "--deep", "--strict", str(app)], check=True,
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        return True
    except (OSError, ValueError, subprocess.CalledProcessError):
        return False


def cached_backend(directory: Path, digest: str) -> bool:
    try:
        info = json.loads((directory / "manifest.json").read_text())
        return (info.get("sourceDigest") == digest and
                info.get("binarySha256") == hashlib.sha256((directory / "VoiceWireBench").read_bytes()).hexdigest())
    except (OSError, ValueError):
        return False


def build_backend(digest: str, compiler: str, sdk_identity: str, sdk: str, target: str) -> None:
    parent = ROOT / "build/macos-voice-bench-cli"
    destination = parent / digest
    if cached_backend(destination, digest):
        print(f"Reused backend: {destination / 'VoiceWireBench'}")
        return
    if destination.exists():
        raise SystemExit(f"Existing backend failed verification; preserved at {destination}")
    parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="stage-", dir=parent) as temporary:
        stage = Path(temporary)
        binary = stage / "VoiceWireBench"
        subprocess.run(["xcrun", "swiftc", *FLAGS, "-sdk", sdk, "-target", target,
                        *map(str, source_files(ROOT, True)), "-o", str(binary)], check=True)
        subprocess.run([str(binary), "--self-test"], check=True)
        if source_digest(ROOT, compiler, sdk_identity, target, True) != digest:
            raise SystemExit("Source changed during the build; backend was not cached")
        (stage / "manifest.json").write_text(json.dumps({
            "sourceDigest": digest, "coreVersion": (ROOT / "version.txt").read_text().strip(),
            "binarySha256": hashlib.sha256(binary.read_bytes()).hexdigest(),
        }, indent=2) + "\n")
        stage.rename(destination)
    print(f"Built backend: {destination / 'VoiceWireBench'}")
    print("Uses a mock keyboard and existing Bluetooth access; does not install or replace an app.")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("mode", nargs="?", choices=["build", "build-backend"], default="build")
    backend = parser.parse_args().mode == "build-backend"
    if platform.system() != "Darwin":
        raise SystemExit("Voice Bench requires macOS")
    sdk = run("xcrun", "--sdk", "macosx", "--show-sdk-path")
    sdk_identity = json.dumps([str(Path(sdk).resolve()), run("xcrun", "--sdk", "macosx", "--show-sdk-version"),
                               run("xcrun", "--sdk", "macosx", "--show-sdk-build-version")])
    compiler = run("xcrun", "swiftc", "--version")
    target = f"{platform.machine()}-apple-macos13.0"
    digest = source_digest(ROOT, compiler, sdk_identity, target, backend)
    if backend:
        build_backend(digest, compiler, sdk_identity, sdk, target)
        return
    parent = ROOT / "build/macos-voice-bench" / digest
    app = parent / "Vibe Voice Bench.app"
    if cached(app, digest):
        print(f"Reused signed bench: {app}")
        return
    # Never replace an existing build that failed identity/signature validation.
    if app.exists():
        raise SystemExit(f"Existing bench failed verification; preserved at {app}")
    parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="stage-", dir=parent) as stage:
        bundle = Path(stage) / app.name
        contents = bundle / "Contents"
        binary = contents / "MacOS/VoiceBench"
        resources = contents / "Resources"
        binary.parent.mkdir(parents=True)
        resources.mkdir()
        subprocess.run(["xcrun", "swiftc", *FLAGS, "-sdk", sdk, "-target", target,
                        *map(str, source_files(ROOT)), "-o", str(binary)], check=True)
        info = {
            "CFBundleIdentifier": BUNDLE_ID, "CFBundleName": "Vibe Voice Bench",
            "CFBundleDisplayName": "Vibe Voice Bench", "CFBundleExecutable": "VoiceBench",
            "CFBundlePackageType": "APPL", "CFBundleVersion": "1",
            "CFBundleShortVersionString": "1.0", "LSMinimumSystemVersion": "13.0",
            "NSBluetoothAlwaysUsageDescription": "Connect to your FoloToy for voice transport testing.",
            "NSLocalNetworkUsageDescription": "Test voice transport with your FoloToy on the local network.",
            "NSPrincipalClass": "NSApplication", "VoiceBenchSourceDigest": digest,
            "VoiceBenchCoreVersion": (ROOT / "version.txt").read_text().strip(),
        }
        (contents / "Info.plist").write_bytes(plistlib.dumps(info))
        shutil.copytree(ROOT / "macos/agents", resources / "AgentAdapters")
        shutil.copy2(ROOT / "LICENSE", resources / "LICENSE.txt")
        subprocess.run(["codesign", "--force", "--sign", "-", str(bundle)], check=True)
        subprocess.run(["codesign", "--verify", "--deep", "--strict", str(bundle)], check=True)
        # This path runs deterministic tests only: no GUI, network, or permission request.
        subprocess.run([str(binary), "--self-test"], check=True)
        if source_digest(ROOT, compiler, sdk_identity, target) != digest:
            raise SystemExit("Source changed during the build; refusing to cache a mismatched bundle")
        bundle.rename(app)
    print(f"Built isolated bench: {app}")
    print("No installed application or system permission was changed.")


if __name__ == "__main__":
    main()
