"""Compile and run the real radio catalog with deterministic HTTP/DNS fixtures."""
import os
import re
import subprocess
import tempfile
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
CJSON = ROOT / 'tests/vibe/vendor/cjson'
# The desktop recommendations and firmware defaults are one product list.
presets = (ROOT / 'main/vibe/radio/radio_presets.h').read_text(encoding='utf-8').split('#else\n', 1)[1].split('#endif', 1)[0]
firmware = re.findall(r'\{"([^"]+)",\s*"[^"]*",\s*"([^"]+)",\s*0\}', presets)
swift = (ROOT / 'macos/app/RadioLibrary.swift').read_text(encoding='utf-8').split('static let presets:', 1)[1].split('].map', 1)[0]
desktop = re.findall(r'\("([^"]+)","([^"]+)"\)', swift)
assert firmware == desktop and 0 < len(firmware) <= 20, 'Desktop recommendations differ from firmware defaults'
assert len({url for _, url in firmware}) == len(firmware), 'Duplicate recommendation URLs'
assert all(len(name.encode()) < 49 and len(url.encode()) < 257 for name, url in firmware)
print(f'Recommendation/default list consistency: PASS ({len(firmware)} stations)', flush=True)
with tempfile.TemporaryDirectory(prefix='passport-radio-') as tmp:
    obj = Path(tmp) / 'json.o'
    exe = Path(tmp) / 'radio'
    subprocess.run(['cc', '-c', '-Wno-deprecated-declarations', '-fsanitize=address,undefined', '-I'+str(CJSON),
                    str(CJSON / 'cJSON.c'), '-o', str(obj)], check=True)
    subprocess.run(['c++', '-std=c++17', '-Wall', '-Wextra', '-Werror',
                    '-fsanitize=address,undefined', '-fno-omit-frame-pointer',
                    '-Itests/vibe/radio_mocks', '-Imain/vibe', '-I'+str(CJSON),
                    'tests/vibe/test_radio_catalog.cc', 'main/vibe/radio/radio_catalog.cc',
                    str(obj), '-o', str(exe)], cwd=ROOT, check=True)
    subprocess.run([str(exe)]+sys.argv[1:], check=True)
