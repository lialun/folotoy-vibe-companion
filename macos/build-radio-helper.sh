#!/usr/bin/env bash
# Small LGPL FFmpeg subprocess. No Homebrew/runtime download dependency.
set -euo pipefail
root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
cache="${VIBE_RADIO_BUILD:-${root}/.work/radio-helper}"
version=8.0.3
sha=6136812ea6d4e68bdba27e33c2a94382711cdf4f8602ffef056ff792bd6f9818
mkdir -p "${cache}"
archive="${cache}/ffmpeg-${version}.tar.xz"
if [[ ! -f "${archive}" ]]; then
    curl --fail --location --retry 2 "https://ffmpeg.org/releases/ffmpeg-${version}.tar.xz" -o "${archive}.partial"
    mv "${archive}.partial" "${archive}"
fi
[[ "$(shasum -a 256 "${archive}" | cut -d ' ' -f 1)" == "${sha}" ]] || { echo 'FFmpeg source hash mismatch' >&2; exit 1; }
source_dir="${cache}/ffmpeg-${version}"
if [[ ! -d "${source_dir}" ]]; then tar -xJf "${archive}" -C "${cache}"; fi
sdk="$(xcrun --sdk macosx --show-sdk-path)"
archs="${VIBE_ARCHS:-arm64 x86_64}"
bins=()
for arch in ${archs}; do
    build="${cache}/${version}-${arch}"
    mkdir -p "${build}"
    # The recipe's hash invalidates cached binaries when options change.
    recipe="$(shasum -a 256 "${BASH_SOURCE[0]}" | cut -d ' ' -f 1)"
    if [[ ! -f "${build}/recipe" || "$(cat "${build}/recipe")" != "${recipe}" || ! -x "${build}/ffmpeg" ]]; then
        (
            cd "${build}"
            "${source_dir}/configure" --prefix="${build}/install" --arch="${arch}" --target-os=darwin \
                --enable-cross-compile --cc=clang --sysroot="${sdk}" \
                --extra-cflags="-arch ${arch} -mmacosx-version-min=13.0" \
                --extra-ldflags="-arch ${arch} -mmacosx-version-min=13.0" \
                --disable-everything --disable-autodetect --disable-asm --disable-debug --disable-doc \
                --disable-shared --enable-static --disable-gpl --disable-nonfree \
                --enable-ffmpeg --disable-ffplay --disable-ffprobe --enable-network --enable-securetransport \
                --enable-protocol=http,https,tcp,tls,crypto,pipe \
                --enable-demuxer=hls,mpegts,mpegtsraw,mp3,aac,mov \
                --enable-parser=aac,aac_latm,mpegaudio \
                --enable-decoder=aac,aac_latm,mp3,mp3float,pcm_s16le \
                --enable-muxer=pcm_s16le --enable-encoder=pcm_s16le \
                --enable-filter=aresample,aformat,anull --enable-swresample
            make -j "${VIBE_BUILD_JOBS:-4}" ffmpeg
            printf '%s' "${recipe}" > recipe
        )
    fi
    bins+=("${build}/ffmpeg")
done
xcrun lipo -create "${bins[@]}" -output "${cache}/RadioDecoder"
cp "${source_dir}/COPYING.LGPLv2.1" "${cache}/COPYING.LGPLv2.1"
# Ship corresponding unmodified sources and exact build recipe with the app.
cp "${BASH_SOURCE[0]}" "${cache}/build-radio-helper.sh"
echo "Radio decoder: ${cache}/RadioDecoder"
