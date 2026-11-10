#!/usr/bin/env bash
set -euo pipefail
root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
out="${VIBE_MAC_OUTPUT:-${root}/build/macos}"
version="${VIBE_VERSION:-0.1.0}"
if [[ -z "${VIBE_VERSION:-}" && "${GITHUB_REF_TYPE:-}" == tag ]]; then
    version="${GITHUB_REF_NAME#v}"
    version="${version%-vibe-companion}"
fi
if [[ ! "${version}" =~ ^[0-9]+\.[0-9]+\.[0-9]+([.-][A-Za-z0-9.-]+)?$ ]]; then
    echo "Invalid release version" >&2; exit 2
fi
archs="${VIBE_ARCHS:-arm64 x86_64}"
mkdir -p "${out}/objects"
app="${out}/Vibe 伴侣.app"
mkdir -p "${app}/Contents/MacOS" "${app}/Contents/Resources"
sdk="$(xcrun --sdk macosx --show-sdk-path)"
binaries=()
for arch in ${archs}; do
    binary="${out}/objects/VibeCompanion-${arch}"
    xcrun swiftc -swift-version 5 -O -sdk "${sdk}" -target "${arch}-apple-macos13.0" \
        "${root}"/macos/app/*.swift -o "${binary}"
    binaries+=("${binary}")
done
xcrun lipo -create "${binaries[@]}" -output "${app}/Contents/MacOS/VibeCompanion"
cp "${root}/macos/app/Info.plist" "${app}/Contents/Info.plist"
/usr/libexec/PlistBuddy -c "Set :CFBundleShortVersionString ${version}" "${app}/Contents/Info.plist"
xcrun swift "${root}/macos/render-icon.swift" "${out}/objects"
iconutil -c icns "${out}/objects/AppIcon.iconset" -o "${app}/Contents/Resources/AppIcon.icns"
rm -rf "${app}/Contents/Resources/AgentAdapters"
cp -R "${root}/macos/agents" "${app}/Contents/Resources/AgentAdapters"
cp "${root}/LICENSE" "${app}/Contents/Resources/LICENSE.txt"
cp "${root}/docs/vibe/install.md" "${app}/Contents/Resources/Installation.md"
cp "${root}/docs/vibe/install.zh_CN.md" "${app}/Contents/Resources/Installation.zh_CN.md"
python3 "${root}/tools/package_notices.py" "${app}/Contents/Resources/THIRD_PARTY_NOTICES.txt"
cp "${app}/Contents/Resources/THIRD_PARTY_NOTICES.txt" "${out}/THIRD_PARTY_NOTICES.txt"
if [[ -n "${VIBE_SIGN_IDENTITY:-}" ]]; then
    codesign --force --options runtime --timestamp --sign "${VIBE_SIGN_IDENTITY}" "${app}"
else
    codesign --force --sign - "${app}"
fi
codesign --verify --deep --strict "${app}"
cc -std=c11 -Wall -Wextra -Werror -I"${root}/main/vibe" \
    "${root}/tests/vibe/test_wire.c" "${root}/main/vibe/adpcm.c" \
    "${root}/main/vibe/protocol.c" -lm -o "${out}/objects/wire-vector"
"${out}/objects/wire-vector" "${out}/objects/voice-vector.bin"
"${app}/Contents/MacOS/VibeCompanion" --self-test --agent-tests
"${app}/Contents/MacOS/VibeCompanion" --self-test --vectors "${out}/objects/voice-vector.bin"
ditto -c -k --sequesterRsrc --keepParent "${app}" "${out}/VibeCompanion-${version}-macOS.zip"
if [[ "${VIBE_NOTARIZE:-0}" == 1 ]]; then
    : "${VIBE_SIGN_IDENTITY:?Developer ID signing identity is required}"
    : "${VIBE_NOTARY_PROFILE:?Create a local notarytool keychain profile first}"
    xcrun notarytool submit "${out}/VibeCompanion-${version}-macOS.zip" --keychain-profile "${VIBE_NOTARY_PROFILE}" --wait
    xcrun stapler staple "${app}"
    xcrun stapler validate "${app}"
    spctl --assess --type execute --verbose "${app}"
    ditto -c -k --sequesterRsrc --keepParent "${app}" "${out}/VibeCompanion-${version}-macOS.zip"
fi
staging="$(mktemp -d "${out}/dmg.XXXXXX")"
trap 'rm -rf -- "${staging}"' EXIT
ditto "${app}" "${staging}/Vibe 伴侣.app"
ln -s /Applications "${staging}/Applications"
cp "${root}/docs/vibe/install.md" "${staging}/Installation.txt"
cp "${root}/docs/vibe/install.zh_CN.md" "${staging}/安装说明.txt"
hdiutil create -quiet -volname "Vibe Companion" -srcfolder "${staging}" \
    -format UDZO -ov "${out}/VibeCompanion-${version}-macOS.dmg"
hdiutil verify "${out}/VibeCompanion-${version}-macOS.dmg"
(
    cd "${out}"
    shasum -a 256 "VibeCompanion-${version}-macOS.dmg" "VibeCompanion-${version}-macOS.zip" > SHA256SUMS.txt
)
echo "Mac app: ${app}"
