<p align="right">
  <a href="CI-build-and-release.zh_CN.md">简体中文</a> · <strong>English</strong>
</p>

# Automated Build and Release

`.github/workflows/build-firmware.yml` builds firmware and the universal Vibe Companion Mac app for `v*-vibe-companion` tags and manual dispatch. Ordinary branch pushes do not trigger it. Manual branch runs only upload Actions artifacts; pushing a release tag publishes a release after all validation jobs pass. Review files and obtain publishing authorization before pushing a tag. Keep this page synchronized with the workflow.

The checks job runs `./tools/validate.sh --static`. The build job restores ccache, runs `./tools/validate.sh --firmware` with ESP-IDF 5.5.3 for ESP32-C3, verifies the bootloader, partition table, and application at the offsets generated in `flash_args`, checks the 8 MB Flash arguments and configured partition layout, then uploads `FoloToy-AI-Passport-full.bin`. The repository default places these images at `0x0`, `0x8000`, and `0x10000`; user-defined partition tables may change the application offset.

The Mac job runs `bash macos/build.sh`, including C/Swift protocol tests, universal arm64/x86_64 compilation, ad-hoc signing, and DMG/ZIP packaging. It needs no paid Apple account or Apple secrets. Only after all three jobs pass does the release job prepare bilingual installation/release notes, checksums and publish both packages. Free ad-hoc distribution is the default; the app is not Apple notarized. See [Vibe release preparation](../../vibe/release.md) for the exact asset list and user instructions.

All Actions are pinned to full commit SHAs. The build job has `contents: read`; only the tag release job receives `contents: write`.

## Browser flashing

Open `https://ai-passport.folotoy.cn/tools/web-flasher/`, connect the USB JTAG/serial device, select the release's merged `FoloToy-AI-Passport-full.bin`, choose a baud rate such as 460800, and write it from `0x0`. The browser performs local writing and verification; it does not upload the firmware file.

For board and flashing details, see [the hardware development guide](../../hardware-design/AI_HARDWARE_DEVELOPMENT_GUIDE.md).

## Release title

When this repository publishes firmware for several different applications from
the same source tree, a bare version number does not tell a user which app a
release is for. Give each tag a name that carries the version and the app, and
make sure the release title shows both.

- **Tag naming convention**: name tags as `v<version>-<app-name>` in
  lowercase-kebab-case, e.g. `v0.1.0-voice-keychain`, `v1.0.0-pocket-pomodoro`.
  The `<app-name>` is the application this release builds (see the
  repository-relative `docs/reference/<username>/<app-name>/` archive naming).
  A tag that only says a version is ambiguous when several apps share the tree.
- **After the release is published, confirm the release title.** The workflow
  sets the title to the tag name, so a correctly-named tag already reads
  `v0.1.0-voice-keychain`. If the tag did not include the app, or the title is
  not obvious at a glance, edit the release (GitHub: `Edit release`; GitLab:
  edit the tag) so the title is `<version> <app-name>`, e.g. `v0.1.0 Voice
  Keychain`. One quick scan of the release list should distinguish which app a
  release is for.
- **Keep title and tag consistent.** Use `<version>-<app-name>` so the app name
  is visible in both the tag list and the release list. Do not rely on a
  human-readable body alone to carry the app name.

## Changelog preparation

Ordinary feature, application, and documentation pull requests leave the paired
changelog files unchanged. Before creating and pushing a release tag, the
release maintainer:

1. Identifies the previous release and reviews the pull requests merged since it,
   together with the authoritative product and application documentation.
2. Collects user-visible features, fixes, compatibility changes, and release-flow
   changes. Internal refactors, CI maintenance, typo fixes, and generated-file
   refreshes are omitted.
3. Updates `docs/CHANGELOG.md` and `docs/CHANGELOG.zh_CN.md` together, places the
   released entries under a `## <tag> - YYYY-MM-DD` heading, and leaves a fresh
   `Unreleased` section before committing the release preparation.

Treat any existing `Unreleased` entries as pending input: verify them against the
merged changes and the target release instead of copying them blindly. This
release-preparation change is the owner of the shared changelog write and must be
part of the commit that is tagged.

## Release notes

A tag-triggered release succeeds only when the merged firmware and its release
notes travel together. After the release is published, write release notes that
explain the build to a user who may not have read the repository. Cover three
things:

- **What's new**: the features, behaviors, or fixes this release adds or
  changes compared with the previous one. Keep it user-facing, not a commit log.
- **How to build**: run `./tools/validate.sh --firmware` to produce and verify
  the merged firmware, then identify `FoloToy-AI-Passport-full.bin` as the file
  to flash from `0x0`. `idf.py build` alone performs incremental compilation;
  it does not create or verify the merged full image and is not an alternative
  delivery command.
- **How to use**: how to flash the build (the browser flasher above) and the key
  interactions or hardware requirements of the release.

Write the release notes in English (and a Simplified Chinese version where the
project is bilingual) and link them from the GitHub/GitLab release. Derive the
user-visible summary from the release-preparation changelog so the two remain
consistent.

## Related documents

- Firmware publishing to the community: [publish-to-community.md](../release/publish-to-community.md)
- Post-release follow-up: [project-completion.md](../release/project-completion.md)

## Desktop pull-request checks

`.github/workflows/macos-checks.yml` independently validates desktop-related pull
requests, matching pushes to `main`, and manual dispatch. It runs the universal
build, native protocol/Agent tests, signature verification and DMG verification.
It has read-only repository permissions and does not create a release.
