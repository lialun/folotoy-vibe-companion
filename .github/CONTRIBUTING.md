English | [简体中文](/.github/CONTRIBUTING.zh_CN.md)

# Contributing to Vibe Companion

Contributions to the device firmware, desktop client, Agent adapters, design,
and documentation are welcome. Start with the [project overview](/README.md)
and [design contract](/docs/vibe/DESIGN.md). Discuss substantial interaction or
architecture changes in this repository's Issues before implementing them.

## Development

- Device application: `main/vibe/`. Reuse board drivers in `components/bsp/`.
- macOS client and hook adapters: `macos/app/` and `macos/agents/`.
- Product documentation: `docs/vibe/`. Root README files introduce the project;
  detailed controls, defaults, and implementation records belong in the guides.
- Firmware toolchain: ESP-IDF 5.5.3. Desktop: Swift and the macOS SDK, targeting
  macOS 13+, arm64 and x86_64. See [build instructions](/docs/vibe/release.md).

Create a focused branch from the branch that contains the Vibe application.
The upstream FoloToy hardware-test baseline is not the application. Preserve
existing work and keep generated builds and local configuration out of commits.
AI-assisted changes must follow [AGENTS.md](/AGENTS.md).

## Changes and verification

Keep changes small enough to review. Explain the concrete problem, resulting
behavior, compatibility or stored-data impact, and validation. Tests should
cover meaningful behavior and failure cases. Do not describe compilation as
physical-device acceptance.

```bash
./tools/validate.sh --static  # repository checks and host tests
./tools/validate.sh          # complete firmware gate; activate ESP-IDF first
bash macos/build.sh          # universal app, native tests and packages on a Mac
```

For firmware/UI changes, preserve the three-button contract, voice priority,
low-power behavior, and memory ownership rules. Report new resident memory,
peak allocations, and unverified device behavior. Check both themes and Chinese
glyphs at native resolution. Do not change audio/network ownership just to
simplify a page. Flash only with the device owner's authorization.

For Agent adapters, retain unrelated user configuration, install only owned
hooks, and keep the helper bounded and fail-open. Never send prompts, replies,
tool arguments, or transcripts to the device. Check duplicate callbacks,
different sessions, and the actual Agent entry point you support.

## Pull requests

Use a descriptive title, such as `fix(radio): retain buffered input across decode`.
Include relevant screenshots for UI changes and separate build, host, and device
results. Update the authoritative English/Chinese documentation pair. Leave the
shared upstream changelog unchanged for ordinary changes; release preparation
owns versioned release notes. Target this project's application branch, not
FoloToy's upstream repository, unless the change is intended for upstream.

Contributions are submitted under the [MIT license](/LICENSE), with third-party
licenses preserved. Follow the [Code of Conduct](/.github/CODE_OF_CONDUCT.md).
Report security issues through [the private reporting process](/.github/SECURITY.md).
Never include credentials, pairing tokens, personal data, or unsanitized logs.
