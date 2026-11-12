#!/usr/bin/env python3
"""Package retained licenses without requiring a downloaded SDK on the build host."""
from __future__ import annotations

import argparse
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
NOTICES = (
    ("Project and community firmware references (MIT)", "LICENSE"),
    ("Source Han Sans SC bitmap fonts (OFL)", "assets/fonts/vibe-font-license.txt"),
    ("Lucide weather icons 0.468.0", "assets/icons/weather/LICENSE.txt"),
    ("Feather icons", "assets/icons/weather/LICENSE-feather.txt"),
    ("agent-notify adapter reference", "assets/licenses/agent-notify.txt"),
    ("agent-notify-dsh adapter reference", "assets/licenses/agent-notify-dsh.txt"),
    ("PeonPing adapter reference", "assets/licenses/peon-ping.txt"),
    ("FFmpeg 8.0.3 radio subprocess (LGPL-2.1-or-later)", "assets/licenses/ffmpeg-lgpl-2.1.txt"),
    ("LVGL 9.5.0", "assets/licenses/lvgl-9.5.0.txt"),
    ("LVGL TLSF allocator, Matthew Conte", "assets/licenses/lvgl-tlsf.txt"),
    ("LVGL printf, Marco Paland", "assets/licenses/lvgl-sprintf.txt"),
    ("Montserrat embedded font", "assets/licenses/montserrat.txt"),
    ("Font Awesome 5 embedded font and symbols", "assets/licenses/fontawesome5.txt"),
    ("cJSON", "assets/licenses/cjson.txt"),
    ("Apache License 2.0 for the SDK and components listed above", "assets/licenses/apache-2.0.txt"),
)
SOURCES = """FoloToy Vibe Companion — third-party notices

Project and community sources:
https://github.com/FoloToy/ai-passport
https://github.com/manchunx7-bit/passport-os
https://github.com/manchunx7-bit/folo-ai-passport-voice
https://github.com/hellolib/agent-notify
https://github.com/wanfanggreat/agent-notify-dsh
https://github.com/PeonPing/peon-ping

Firmware dependencies:
ESP-IDF 5.5.3, including Wi-Fi and PHY libraries (Apache-2.0):
https://github.com/espressif/esp-idf/tree/v5.5.3
Espressif esp_codec_dev 1.6.2, esp_lvgl_port 2.9.0, button 4.2.0,
and cmake_utilities 1.1.1 (Apache-2.0): https://components.espressif.com/
LVGL 9.5.0: https://github.com/lvgl/lvgl/tree/v9.5.0

The desktop app uses a separate FFmpeg 8.0.3 executable under LGPL-2.1-or-later:
https://ffmpeg.org/
Its corresponding unmodified source archive, license and exact build recipe are
included inside the app at Contents/Resources/RadioDecoderSource/. The app does
not link against FFmpeg libraries; the bundled executable is replaceable.
No GPL/nonfree components or external codec libraries are enabled.

These notices cover the desktop app and accompanying device firmware.
BlackHole and the input method are separately installed, not bundled.

"""


def build_notices() -> str:
    sections = []
    for label, relative in NOTICES:
        body = (ROOT / relative).read_text(encoding="utf-8")
        if not body.strip():
            raise ValueError(f"Empty license: {relative}")
        sections.append(f"{'=' * 72}\n{label}\n{'=' * 72}\n\n{body.rstrip()}\n")
    return SOURCES + "\n".join(sections)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    # Read every required license before replacing an existing notice file.
    notices = build_notices()
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(notices, encoding="utf-8")


if __name__ == "__main__":
    main()
