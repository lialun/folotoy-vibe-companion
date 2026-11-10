**English** · [简体中文](install.zh_CN.md)

# Install Vibe Companion on Mac

Requires **macOS 13+**, on Apple Silicon or Intel. Download the DMG or ZIP from
[GitHub Releases](https://github.com/lialun/folotoy-vibe-companion/releases/latest).

1. **Install:** drag the app to **Applications**, then open it there. To update,
   quit the old app before replacing it.
2. **Allow opening:** if macOS blocks it, choose **System Settings → Privacy &
   Security → Open Anyway**. If it says the app is damaged with no Open Anyway
   option, confirm it came from this project's Release and check `SHA256SUMS.txt`.
   In Terminal, type the following followed by a space, drag the installed app
   into Terminal, and press Return; then open it again:

   ```bash
   xattr -dr com.apple.quarantine
   ```

3. **Connect:** install the matching device firmware, keep Mac and device on a
   reachable LAN, and allow local network access when prompted. Enter the
   six-digit code shown on the device to pair.
4. **Set up voice (optional):** install [BlackHole 2ch](https://existential.audio/blackhole/)
   and [WeType](https://z.weixin.qq.com/). In the input method, choose BlackHole 2ch
   as microphone and set hold-to-talk to **left Control + left Option + V**;
   use the same shortcut in Vibe. Enable the app in **System Settings → Privacy &
   Security → Accessibility**. Skip this step for Agent notifications alone.

If Accessibility is enabled after an update but Vibe still reports no permission,
remove the old entry and add the current app from Applications again.

Keep the Mac awake and select a text field. Hold device UP to speak, release to
finish, and tap DOWN for Enter. Closing the window keeps the app in the menu bar;
Quit stops it. The app does not use Keychain.
