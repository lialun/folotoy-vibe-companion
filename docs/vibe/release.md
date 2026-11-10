**English** · [简体中文](release.zh_CN.md)

# Build and distribute

This project distributes through **GitHub Releases only**, using free **ad-hoc
signing**. Neither a paid Apple membership nor App Store submission is required.
The packages are not Apple notarized. Users may need to approve the individual
app when first opening it; the [installation guide](install.md) covers this and
permission changes after updates. This signing method can be used for public
releases. Final release status depends on the exact version's validation record,
including clean-machine and physical Intel checks.

## Local firmware

Install/activate ESP-IDF **5.5.3** for ESP32-C3. Keep the toolchain outside commits.
Run `./tools/validate.sh`; this includes upstream and application host tests,
an isolated build, merged-image verification and a matching debug archive.
The deliverable is `build/FoloToy-AI-Passport-full.bin`, flashed from `0x0`.
Verify `build/firmware/<sha256>/` with `tools/archive_firmware.py verify`.
The 8 MB factory partition layout is unchanged. A merged flash may reset NVS.
Read [firmware layout](../development/engineering/firmware-layout.md) before
choosing a data-preserving segmented upgrade.

## Local Mac app

Requires a Mac with Swift and the macOS SDK, normally from Xcode Command Line
Tools or full Xcode. The developer needs a compiler; end users do not.

```bash
bash macos/build.sh
```

The script builds both `arm64` and `x86_64`, combines them into one application,
signs it, runs native protocol tests and packages a DMG and ZIP in `build/macos/`.
`VIBE_ARCHS=arm64` is available for a development-only build.
The app uses Apple frameworks directly; no Python, Homebrew or JavaScript
runtime is required by end users. Voice input requires a separate installation
of BlackHole 2ch and a compatible input method such as WeType. Agent-only use
does not need them or keyboard-control permission. Installation and
Accessibility/local-network permission cannot be silently granted by this app.

The DMG contains the app, an Applications shortcut and both installation guides.
The ZIP is an alternative containing the same app. Both include app resources
with license/third-party notices. `SHA256SUMS.txt` uses portable filenames.
No Apple certificate or account secrets are used by default.

Optional future Developer ID signing and notarization remain supported by the
build script, but are not part of the selected free release route. That option
requires your own [Developer ID](https://developer.apple.com/developer-id/),
Keychain identity and `notarytool` profile:

```bash
VIBE_VERSION=0.1.0 \
VIBE_SIGN_IDENTITY='Developer ID Application: Your Name (TEAMID)' \
VIBE_NOTARIZE=1 VIBE_NOTARY_PROFILE=vibe-notary \
bash macos/build.sh
```

The script enables Hardened Runtime, submits the ZIP to the notary service,
staples and validates the ticket, assesses the app with Gatekeeper, and rebuilds
the ZIP. Use full Xcode if your Command Line Tools lack `notarytool`/`stapler`.
No App Store review is involved. Keep certificates, private keys and Apple
credentials out of the repository and release artifacts.

## GitHub Actions

The workflow runs repository/host checks and builds firmware and the universal Mac app. Manual
dispatch produces downloadable Actions artifacts without publishing a release.
An explicitly pushed tag matching `v*-vibe-companion` produces a **draft,
prerelease** containing both packages. Only the release job can write repository
contents. Automatic Mac builds are ad-hoc signed and do not use Apple secrets.
`tools/package_release.py` generates bilingual installation attachments, combined
[release notes](release-notes.md) and checksums for all uploaded payloads.

Assets are the merged firmware, universal DMG, universal ZIP, `INSTALL.txt`,
`INSTALL.zh_CN.txt`, `THIRD_PARTY_NOTICES.txt` and `SHA256SUMS.txt`.
To stage the same package locally, copy the firmware, DMG, ZIP and notices into
an empty directory and run `python3 tools/package_release.py <directory>`.

Before publishing: perform device acceptance, test on clean Apple Silicon and
Intel Macs, prepare both changelog languages, select a version and tag such as
`v0.1.0-vibe-companion`, and review/update the bilingual draft release notes.
Do not replace the free packages with a paid signing dependency.
Include font/source licenses and SHA-256 sums.
Do not describe the local command-line tests as a successful GitHub Actions run.

Creating a repository, pushing source, pushing a release tag and publishing a
draft are separate external actions. Prepare and review the concrete files
before executing an authorized action. The current development task does not
automatically publish anything.

## Local release review

Keep each candidate in a fresh output directory so old versions are not mixed:

```bash
VIBE_MAC_OUTPUT="$PWD/build/review/macos" bash macos/build.sh
```

Choose the final semantic version with `VIBE_VERSION` when preparing the actual
release; the examples above do not select a public version. Before tagging,
review the current bilingual release overview and add the candidate's actual
validation results. Installation attachments and notes must be readable outside
the source tree. The release-package host test checks complete/missing/ambiguous
payloads, reproducible portable checksums, and retained notices.

PR/push checks include the separate `macos-checks.yml` for native Swift tests and
universal packaging. The upstream baseline sync is manual-only; publishing this
application must not schedule automatic merges over application work.

Regenerate the README images with `bash tools/render_readme.sh` on a Mac with
managed components resolved. The tool renders production UI with sample data,
without replacing the installed app or communicating with the device.
