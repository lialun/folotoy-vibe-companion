<p align="right">
  <a href="README.zh_CN.md">简体中文</a> · <strong>English</strong>
</p>

# Assets

This directory stores reusable fonts, images, music, sound effects, and battery profiles, organized by asset type.

Keep each asset in the matching subdirectory and document its destination, naming, integration method, and source/license. Do not mix binary assets with Markdown documentation.

## Fonts

The Vibe application links `main/vibe/vibe_font.c`, the community Source Han
Sans SC 16px/2bpp subset (4,717 glyphs), with explicit Montserrat symbol fallback.
Its [OFL license](fonts/vibe-font-license.txt) is included in release notices.
Source, generation provenance and coverage checks are in
[the implementation record](../docs/vibe/implementation.md).

Store reusable font files and generated font sources in `fonts/`.

- Use descriptive names that include the family, weight, size, and format when relevant.
- Document the source, license, character range, conversion command, and expected destination.
- Check Flash and internal-RAM impact before adding a font; the ESP32-C3 has no PSRAM.
- Do not commit fonts whose license does not permit redistribution.

## Images

Store reusable source images and generated display assets in `images/`.

| File | Dimensions and format | Use and source |
| --- | --- | --- |
| [`images/home.jpg`](images/home.jpg) | 3840 × 2160, JPEG | Original product photograph retained in the upstream hardware overview under docs/README.md. |
| [`images/readme-hardware-specs.png`](images/readme-hardware-specs.png) | 2172 × 724, PNG RGBA | Optional technical infographic retained as a reference asset; it is no longer used as the homepage hero. Generated for this repository with the built-in image generation tool on 2026-09-17; the six labels and values were checked against the documented hardware contract. |
| [`images/logo-wordmark.png`](images/logo-wordmark.png) | 1648 × 336, PNG RGBA | Transparent black wordmark extracted from the repository's original `images/logo.png`; retained for light-background upstream hardware documentation. |
| [`images/logo-wordmark-dark.png`](images/logo-wordmark-dark.png) | 1648 × 336, PNG RGBA | White version of the extracted wordmark, used by the upstream hardware overview in dark mode. |

- Use descriptive names and document dimensions, pixel format, conversion steps, and destination.
- Prefer formats suitable for the 240 × 320 RGB565 display and account for Flash and internal RAM.
- Preserve editable sources where licensing permits, and record the source and license.
- Never commit device QR secrets, credentials, or personal data in images.

## Music and sound effects

Store reusable music and sound-effect sources in `music/`.

- Document the source, license, sample rate, bit depth, channels, conversion command, and destination.
- Prefer 16 kHz, 16-bit mono PCM when it matches the current BSP audio path.
- Check Flash and internal-RAM cost before embedding audio; stream or chunk long recordings.
- Do not commit media without redistribution permission.

## Battery profiles

[`battery/wanli-500mah-profile2-xil40x-20261020.txt`](battery/wanli-500mah-profile2-xil40x-20261020.txt)
contains the supplied CW2017 profile for the Wanli New Energy 500 mAh cell
(profile2, XIL40X, 2026-10-20). It is an unchanged copy of the project-supplied
file, retained at the maintainer's request for this firmware update. Its
original filename is recorded in the paired Chinese document. No separate
license was included in the supplied file.

The format is 80 lines of two hexadecimal digits, one byte per line in register
order. The BSP embeds those bytes in `components/bsp/src/bsp_battery.c`; the
host gate checks the source file and initializer for an exact match. Text-file
SHA-256: `7ea96eba4634f4495a37c25ffacd54c1deaa0693b162c892d671a2ba68460919`.
Decoded 80-byte SHA-256:
`fa7365671093ef449c5bdcfb4adb279e3d275f888109f50b6b2f21e9b96220d5`.
This is a fuel-gauge profile, not a charging configuration or a generic profile
for every 500 mAh cell; see the [hardware guide](../docs/hardware-design/AI_HARDWARE_DEVELOPMENT_GUIDE.md#9-cw2017-fuel-gauge).

## Vibe Companion identity and typography

[`brand/vibe-companion.svg`](brand/vibe-companion.svg) is the editable V-shaped companion mark, created for this project under the repository MIT license. `macos/render-icon.swift` renders matching macOS iconset sizes and `iconutil` packages them as AppIcon.icns. `macos/app/Brand.swift` draws the compact menu-bar and in-app variants; no microphone symbol is used as the app identity.

`main/vibe/font_20.c` contains the 20 px, 2 bpp Source Han Sans SC primary font; `main/vibe/vibe_font.c` supplies 16 px supporting text and key hints. The 12 px font is no longer linked. Both use the character inventory in `fonts/vibe-symbols.txt`. Source: the pinned LVGL SourceHanSansSC-Normal.otf; converter: lv_font_conv 1.5.3. The [font license](fonts/vibe-font-license.txt) applies to both assets. Static firmware text is checked against both sizes. Dynamic city/station names retain the common CJK inventory, not arbitrary Unicode coverage.

## Weather icons

`icons/weather/` retains the twelve SVG sources from [Lucide 0.468.0](https://github.com/lucide-icons/lucide/tree/0.468.0/icons), with the upstream ISC/Feather MIT notices in `LICENSE.txt` and version in `SOURCE.txt`. `python3 tools/render_weather_icons.py` uses macOS AppKit/Swift and Pillow to render at 192 px, downsample to 48 px and generate `main/vibe/ui_weather_icons.inc`. Twelve uncompressed A8 masks occupy 27,648 bytes in Flash; no network image downloads or persistent animation are used. The runtime enum order is shared with `weather.h`, including day/night, rain/snow/fog/thunder/hail and unknown fallback. Temperature and intensity remain readable text rather than requiring a separate illustration per condition.

The voice-state waveform in `main/vibe/ui_wave.inc` is original project artwork under MIT: five rounded bars in a static 28 x 28, 784-byte A8 mask. The timer entry uses an original 16 x 16, 256-byte A8 stopwatch (`main/vibe/ui_timer.inc`). Regenerate both with `python3 tools/render_ui_icons.py` (Pillow). The waveform sits on the left of the compact voice header; the device recolors it at draw time without scaling or animation.

The application exit symbol is original MIT artwork: a 16 x 16, 256-byte A8
door-and-arrow mask in `main/vibe/ui_exit.inc`. The same UI-symbol generator
recreates it. It replaces the stopwatch on the DOWN long-press hint whenever
Pomodoro occupies the Home module, without scaling, animation or new LVGL objects.

## Agent adapters

MIT notices for agent-notify, agent-notify-dsh and PeonPing are retained in `licenses/`. Bundled adapter sources are in `macos/agents/`, with notices copied into release packages. No sound assets from these projects are used.

## Vibe Companion project images

| File | Dimensions | Source and use |
| --- | --- | --- |
| [Hero](brand/vibe-companion-hero.png) | 3200 × 1760 PNG | Chinese promotional artwork with original enclosures, vector typography and high-resolution screen illustrations following the current UI. |
| [Community cover](brand/vibe-companion-cover.png) | 1152 × 1536 PNG | Portrait 3:4 UI artwork for the community listing, labeled as an illustration; no physical-device photograph or mockup. |
| [Device screens](brand/vibe-companion-screens.png) | 3200 × 1200 PNG | Chinese feature gallery with native high-resolution text and vector graphics. Documentation assets only; no firmware font/image changes. |
| [Actual firmware frames](brand/vibe-companion-screens-actual.png) | 1600 × 600 PNG | Unmodified 240 × 320 LVGL frames, retained separately as a rendering reference. |
| [Desktop station management](brand/vibe-companion-desktop.png) | 980 × 690 pt, 2× PNG on the capture host | Production SwiftUI radio view with sample data. No device/network information is captured. |
| [Desktop Agent integrations](brand/vibe-companion-desktop-agents.png) | 980 × 1100 pt, 2× PNG on the capture host | Production SwiftUI Agent view with sample installation states, without inspecting or changing installed Agents. |

Regenerate on macOS, after resolving the pinned managed LVGL component:

```bash
bash tools/render_readme.sh
```

The script runs the native firmware renderer, draws the high-resolution artwork,
compiles the desktop preview without
its application entry point, and renders offscreen. It does not launch/update the
installed app, request permissions, connect to a device, or install hooks.
`tools/render_readme.swift` is the editable composition source;
`tools/readme_snapshot.swift` captures the real desktop view. Project artwork is
MIT; rendered firmware typography retains its font licenses. These PNGs and tools
are documentation-only, excluded from firmware and its runtime memory budget.

## Retained firmware dependency licenses

`tools/package_notices.py` uses these retained texts so a Mac-only packaging host
does not need ESP-IDF or downloaded managed components. The combined release
notice includes them along with the project, Source Han, icon and Agent notices.

| Retained file | Source / pinned dependency |
| --- | --- |
| `licenses/apache-2.0.txt` | ESP-IDF 5.5.3 `LICENSE`; Apache-2.0 also supplied by button 4.2.0, cmake_utilities 1.1.1, esp_codec_dev 1.6.2, esp_lvgl_port 2.9.0, and IDF Wi-Fi/PHY libraries. |
| `licenses/esp-audio-codec-2.5.0.txt` | Unmodified `LICENSE` from esp_audio_codec 2.5.0, Espressif Modified MIT. Its third-party MP3/AAC attribution follows the [vendor copyright page](https://docs.espressif.com/projects/esp-adf/en/latest/COPYRIGHT.html). |
| `licenses/lvgl-9.5.0.txt` | LVGL 9.5.0 `LICENCE.txt`. |
| `licenses/lvgl-tlsf.txt` | Complete copyright/license block from LVGL 9.5.0 `src/stdlib/builtin/lv_tlsf.h`, Matthew Conte, BSD. |
| `licenses/lvgl-sprintf.txt` | LVGL 9.5.0 `src/stdlib/builtin/LICENSE_SPRINTF.txt`. |
| `licenses/montserrat.txt` | LVGL 9.5.0 `scripts/built_in_font/font_license/Montserrat/OFL.txt`. |
| `licenses/fontawesome5.txt` | LVGL 9.5.0 `scripts/built_in_font/font_license/FontAwesome5/LICENSE.txt`. |
| `licenses/cjson.txt` | ESP-IDF 5.5.3 `components/json/cJSON/LICENSE`. |

Upstream source: [ESP-IDF](https://github.com/espressif/esp-idf/tree/v5.5.3),
[LVGL](https://github.com/lvgl/lvgl/tree/v9.5.0), and the
[Espressif registry](https://components.espressif.com/).
When changing dependencies, review their current notices and refresh these
retained copies and package labels together. The Espressif audio component's
license is distinct from this project's MIT source license.

The bundled radio decoder license is [LGPL-2.1](licenses/ffmpeg-lgpl-2.1.txt). FFmpeg 8.0.3 is built from the hash-pinned upstream source by `macos/build-radio-helper.sh`; its unmodified source archive and recipe are included in the application. Generated binaries and downloaded source stay outside Git.
