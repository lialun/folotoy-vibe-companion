**English** · [简体中文](install.zh_CN.md)

# Install the GitHub Release Mac app

Requires macOS 13 or later. The same universal package supports Apple Silicon
and Intel. Download `VibeCompanion-<version>-macOS.dmg` from this project's
GitHub Releases. No Apple developer membership or build tools are needed.

1. Open the DMG and drag the Vibe app onto **Applications**. Eject the disk
   image and launch the app from Applications. The ZIP is an alternative: unzip
   it and move the app to Applications before opening it.
2. This free distribution uses an **ad-hoc signature**, without an Apple
   Developer ID certificate or notarization. If macOS blocks the app, try
   opening it once, then open **System Settings → Privacy & Security → Open
   Anyway** and confirm this app. Follow
   [Apple's per-app instructions](https://support.apple.com/102445).
3. If a verified download still reports that the app is damaged and there is no
   Open Anyway option, quit the app. Type the command below in Terminal with a
   trailing space, drag the installed Vibe app from Applications into Terminal
   to insert its exact path, then press Return. This removes the download
   quarantine flag **only from this installed app**:

   ```bash
   xattr -dr com.apple.quarantine
   ```

   Run it only after confirming the download came from this project's Release
   and checking its SHA-256 against `SHA256SUMS.txt`. Do not use it to repair a
   mismatched checksum. No global Gatekeeper change is needed.
4. **For voice input**, install [BlackHole 2ch](https://existential.audio/blackhole/) and
   [WeType](https://z.weixin.qq.com/) separately. In WeType's voice settings,
   select **BlackHole 2ch** as microphone and set hold-to-talk to **left
   Control + left Option + V**. Keep the same shortcut in Vibe Companion.
   Skip audio and keyboard-control setup if you only need Agent notifications.
5. Update the matching device firmware before launching this client. The client
   automatically uses an available UDP port, even when another program owns 33333.
   Allow local network access when prompted. Mac and device must be on a LAN
   whose clients can communicate.
6. Enter the six-digit pairing code shown on the device Home. For voice input
   and Enter, enable the Vibe app itself in **System Settings → Privacy &
   Security → Accessibility**. A permission granted to Terminal is not inherited
   by this app.

Keep the Mac awake and select a text field. Hold device UP to speak, release to
finish, and short-press DOWN to insert Enter. Closing the app window leaves it
running in the menu bar; Quit stops reception. Login startup is optional.

Return to the app after granting permission. It rechecks automatically and,
when a process reload is needed, restores the current page and unsaved drafts
without requiring a manual quit/reopen. Ad-hoc-signed updates may still need
a new system grant.

## Updating

Quit the running app, replace it in Applications, and open the new version.
Free ad-hoc signatures change between builds: macOS may require another Open
Anyway confirmation or Accessibility authorization. If
Accessibility is checked but shortcuts stop working, remove the old Vibe entry
and add the new app again. Keep the same bundle name and location. Device settings
stay on the device; the app saves its pairing record in a local file with
account-only permissions and does not use Keychain. Other programs under the
same account may read that file. If the record is lost, unpair on the device and
pair again; this does not remove saved Wi-Fi networks.

Download updates from GitHub Releases. This version does not install updates
automatically. Before publishing, verify the exact firmware and app together.
Physical Intel and clean-Mac installation remain separate checks; signing status
does not establish functionality.
