#!/usr/bin/env bash
# Render documentation images without launching or updating the installed app.
set -euo pipefail
root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
cd "${root}"
if [[ "$(uname -s)" != Darwin ]]; then
    echo "README artwork needs macOS AppKit and Swift." >&2
    exit 2
fi
if [[ ! -d managed_components/lvgl__lvgl ]]; then
    echo "Resolve the pinned ESP-IDF managed components before rendering." >&2
    exit 2
fi
work="${root}/.work/readme"
mkdir -p "${work}/frames" assets/brand
cmake -S tools/vibe_preview -B "${work}/preview" -DCMAKE_BUILD_TYPE=Release
cmake --build "${work}/preview" --parallel 4
"${work}/preview/preview" "${work}/frames"
xcrun swift tools/render_readme.swift "${work}/frames" assets/brand
sources=()
for path in macos/app/*.swift; do
    [[ "${path}" == macos/app/App.swift ]] || sources+=("${path}")
done
xcrun swiftc -swift-version 5 -O -parse-as-library \
    -target "$(uname -m)-apple-macos13.0" \
    "${sources[@]}" tools/readme_snapshot.swift -o "${work}/snapshot"
"${work}/snapshot" assets/brand
