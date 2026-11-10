English | [简体中文](/.github/SECURITY.zh_CN.md)

# Security policy

This policy covers Vibe Companion firmware, the desktop client, LAN pairing and
transport, Agent adapters, and release tooling. This is a community project;
FoloToy's upstream maintainers do not operate its reporting channel.

## Reporting a vulnerability

Use **Security → Report a vulnerability** in this repository if private
vulnerability reporting is enabled. If that option is unavailable, open an
Issue requesting a private contact, with no vulnerability details, credentials,
or exploit steps. Wait for a project maintainer to provide a private channel.

Include the affected release/commit and platform, reproduction steps, impact,
and suggested mitigation in the private report. Remove Wi-Fi passwords, pairing
and hook tokens, conversation contents, and personal data from attachments.
Do not post an unpatched vulnerability in public Issues or pull requests.

The maintainer will review reports and coordinate disclosure as capacity allows;
this project does not promise a fixed response SLA. Bugs in separately installed
software or external services belong with their maintainers.

## Deployment context

The companion is designed for a trusted local network. A pairing token restricts
control, but the current LAN protocol is not encrypted transport. Pairing and
local Agent endpoint files use account-private permissions, not Keychain;
programs running as the same account may be able to read them.

GitHub packages use ad-hoc signing and are not Apple notarized. Compare downloads
with release checksums and use the per-app approval steps in the
[installation guide](/docs/vibe/install.md).
