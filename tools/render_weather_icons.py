#!/usr/bin/env python3
"""Regenerate local A8 LVGL weather assets (macOS AppKit, Swift and Pillow)."""
from pathlib import Path
import subprocess
import tempfile
from PIL import Image
ROOT = Path(__file__).resolve().parents[1]
NAMES = ['circle-help','sun','moon','cloud-sun','cloud-moon','cloud','cloud-fog',
         'cloud-drizzle','cloud-rain','cloud-snow','cloud-lightning','cloud-hail']
output = ['/* Generated from assets/icons/weather; Lucide 0.468.0, see LICENSE.txt. */']
with tempfile.TemporaryDirectory(prefix='vibe-weather-icons-') as temporary:
    subprocess.run(['swift',str(ROOT/'tools/render_weather_icons.swift'),str(ROOT/'assets/icons/weather'),temporary],check=True)
    for index,name in enumerate(NAMES):
        with Image.open(Path(temporary)/(name+'.png')) as source:
            alpha=source.convert('RGBA').getchannel('A').resize((48,48),Image.Resampling.LANCZOS)
            pixels=list(alpha.get_flattened_data())
        output.append(f'static const uint8_t weather_pixels_{index}[] = {{')
        output.extend(','.join(map(str,pixels[start:start+48]))+',' for start in range(0,len(pixels),48))
        output.append('};')
        output.append('static const lv_image_dsc_t weather_icon_%d = {.header={.magic=LV_IMAGE_HEADER_MAGIC,.cf=LV_COLOR_FORMAT_A8,.w=48,.h=48,.stride=48},.data_size=2304,.data=weather_pixels_%d};' % (index,index))
output.append('static const lv_image_dsc_t *const weather_icons[] = {'+','.join('&weather_icon_%d'%i for i in range(len(NAMES)))+'};')
(ROOT/'main/vibe/ui_weather_icons.inc').write_text('\n'.join(output)+'\n')
