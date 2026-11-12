#!/usr/bin/env bash
set -euo pipefail

mode="${1:---all}"
repo_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"

usage() {
    echo "Usage: $0 [--all|--static|--firmware]" >&2
}

run_static_checks() {
    local actionlint_bin
    local test_dir
    local dead_strip_flag

    # Apple ld does not accept --gc-sections; -dead_strip is the equivalent.
    dead_strip_flag="-Wl,--gc-sections"
    if [[ "$(uname -s)" == "Darwin" ]]; then
        dead_strip_flag="-Wl,-dead_strip"
    fi

    python3 tools/check_repo.py

    actionlint_bin="${ACTIONLINT_BIN:-}"
    if [[ -z "${actionlint_bin}" ]]; then
        actionlint_bin="$(command -v actionlint || true)"
    fi
    if [[ -z "${actionlint_bin}" || ! -x "${actionlint_bin}" ]]; then
        actionlint_bin="$(./tools/install-actionlint.sh)"
    fi
    "${actionlint_bin}" -color .github/workflows/*.yml

    test_dir="$(mktemp -d /tmp/ai-passport-host-tests.XXXXXX)"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Imain \
        tests/test_ui_pixel_math.c main/ui_pixel_math.c \
        -o "${test_dir}/test_ui_pixel_math"
    "${test_dir}/test_ui_pixel_math"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Imain \
        tests/test_demo_navigation.c main/demo_navigation.c \
        -o "${test_dir}/test_demo_navigation"
    "${test_dir}/test_demo_navigation"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Imain \
        tests/test_key_guard.c -o "${test_dir}/test_key_guard"
    "${test_dir}/test_key_guard"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Icomponents/bsp/src \
        tests/test_bsp_display_rounding.c components/bsp/src/bsp_display_rounding.c \
        -o "${test_dir}/test_bsp_display_rounding"
    "${test_dir}/test_bsp_display_rounding"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Icomponents/bsp/src \
        tests/test_bsp_es8311_sleep_check.c components/bsp/src/bsp_es8311_sleep_check.c \
        -o "${test_dir}/test_bsp_es8311_sleep_check"
    "${test_dir}/test_bsp_es8311_sleep_check"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror \
        -Itests/battery_stubs -Itests/bsp_stubs -Icomponents/bsp/include \
        tests/test_bsp_battery.c -o "${test_dir}/test_bsp_battery"
    "${test_dir}/test_bsp_battery"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror \
        -Itests/bsp_stubs -Icomponents/bsp/include \
        tests/test_bsp_button.c -o "${test_dir}/test_bsp_button"
    "${test_dir}/test_bsp_button"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror \
        -Itests/bsp_stubs -Icomponents/bsp/include \
        tests/test_bsp_lvgl_init.c components/bsp/src/bsp_display_rounding.c \
        -o "${test_dir}/test_bsp_lvgl_init"
    "${test_dir}/test_bsp_lvgl_init"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -DCONFIG_BSP_LVGL_DRAW_BUFFER_LINES=20 \
        -Itests/bsp_stubs -Icomponents/bsp/include \
        tests/test_bsp_lvgl_init.c components/bsp/src/bsp_display_rounding.c \
        -o "${test_dir}/test_bsp_lvgl_small_buffer"
    "${test_dir}/test_bsp_lvgl_small_buffer"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror \
        -Itests/audio_stubs -Icomponents/bsp/include -Icomponents/bsp/src \
        tests/test_bsp_audio_recovery.c components/bsp/src/bsp_es8311_sleep_check.c \
        -o "${test_dir}/test_bsp_audio_recovery"
    "${test_dir}/test_bsp_audio_recovery"
    for demo in audio low_power ble wifi; do
        "${CC:-cc}" -std=c11 -Wall -Wextra -Werror \
            -ffunction-sections -fdata-sections -Itests/demo_stubs -Imain \
            "tests/test_demo_${demo}_runtime.c" "${dead_strip_flag}" \
            -o "${test_dir}/test_demo_${demo}_runtime"
        "${test_dir}/test_demo_${demo}_runtime"
    done
    PYTHONDONTWRITEBYTECODE=1 python3 tests/test_battery_profile.py
    PYTHONDONTWRITEBYTECODE=1 python3 tests/test_deep_sleep_contract.py
    PYTHONDONTWRITEBYTECODE=1 python3 tests/test_key_wake_contract.py
    PYTHONDONTWRITEBYTECODE=1 python3 tests/test_check_repo.py
    PYTHONDONTWRITEBYTECODE=1 python3 tests/test_verify_firmware.py
    PYTHONDONTWRITEBYTECODE=1 python3 tests/test_archive_firmware.py
    PYTHONDONTWRITEBYTECODE=1 python3 tests/test_release_package.py
    PYTHONDONTWRITEBYTECODE=1 python3 tests/test_install_passport_skills.py
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Imain/vibe \
        tests/vibe/test_model.c main/vibe/model.c -o "${test_dir}/test_vibe_model"
    "${test_dir}/test_vibe_model"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Imain/vibe \
        tests/vibe/test_appearance.c main/vibe/appearance.c main/vibe/prefs.c main/vibe/signal.c -lm -o "${test_dir}/test_vibe_appearance"
    "${test_dir}/test_vibe_appearance"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined \
        -Itests/vibe/clock_stubs -Imain/vibe tests/vibe/test_clock_sync.c -o "${test_dir}/test_vibe_clock_sync"
    "${test_dir}/test_vibe_clock_sync"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Imain/vibe \
        tests/vibe/test_live_volume.c main/vibe/prefs.c main/vibe/appearance.c -lm -o "${test_dir}/test_vibe_live_volume"
    "${test_dir}/test_vibe_live_volume"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Imain/vibe \
        tests/vibe/test_wire.c main/vibe/adpcm.c main/vibe/protocol.c -lm \
        -o "${test_dir}/test_vibe_wire"
    "${test_dir}/test_vibe_wire"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -Imain/vibe \
        tests/vibe/test_radio_relay.c main/vibe/adpcm.c -o "${test_dir}/test_vibe_relay"
    "${test_dir}/test_vibe_relay"
    "${CC:-cc}" -std=c11 -Wno-deprecated-declarations -Itests/vibe/vendor/cjson -c \
        tests/vibe/vendor/cjson/cJSON.c -o "${test_dir}/vibe_json.o"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined \
        -Itests/vibe/diagnostic_stubs -Itests/vibe/relay_stubs -Itests/vibe/connection_stubs -Itests/vibe/stubs -Itests/audio_stubs \
        -Itests/vibe/vendor/cjson -Imain/vibe tests/vibe/test_diagnostics.c "${test_dir}/vibe_json.o" -o "${test_dir}/test_vibe_diagnostics"
    "${test_dir}/test_vibe_diagnostics"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined \
        -Itests/vibe/relay_stubs -Itests/vibe/connection_stubs -Itests/vibe/stubs -Itests/audio_stubs \
        -Icomponents/bsp/include -Itests/vibe/vendor/cjson -Imain/vibe \
        tests/vibe/test_relay_session.c main/vibe/adpcm.c "${test_dir}/vibe_json.o" -lm -o "${test_dir}/test_vibe_relay_session"
    "${test_dir}/test_vibe_relay_session"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Imain/vibe \
        tests/vibe/test_peer.c -o "${test_dir}/test_vibe_peer"
    "${test_dir}/test_vibe_peer"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -Imain/vibe \
        tests/vibe/test_hosts.c main/vibe/hosts.c main/vibe/ble_frame.c -o "${test_dir}/test_vibe_hosts"
    "${test_dir}/test_vibe_hosts"
    "${CC:-cc}" -std=c11 -D_POSIX_C_SOURCE=200809L -Wall -Wextra -Werror -fsanitize=address,undefined \
        -Itests/vibe/connection_stubs -Itests/vibe/stubs -Itests/audio_stubs -Itests/vibe/vendor/cjson -Imain/vibe \
        tests/vibe/test_connections.c main/vibe/hosts.c tests/vibe/vendor/cjson/cJSON.c -lm -o "${test_dir}/test_vibe_connections"
    "${test_dir}/test_vibe_connections"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined \
        -Itests/vibe/ble_stubs -Itests/vibe/connection_stubs -Itests/vibe/stubs -Itests/audio_stubs -Imain/vibe \
        tests/vibe/test_ble_runtime.c main/vibe/hosts.c main/vibe/ble_frame.c -o "${test_dir}/test_vibe_ble_runtime"
    "${test_dir}/test_vibe_ble_runtime"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -Imain/vibe \
        tests/vibe/test_wifi_profiles.c main/vibe/wifi_profiles.c -o "${test_dir}/test_vibe_wifi_profiles"
    "${test_dir}/test_vibe_wifi_profiles"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined \
        -Itests/vibe/stubs -Imain/vibe tests/vibe/test_messages.c -o "${test_dir}/test_vibe_messages"
    "${test_dir}/test_vibe_messages"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror \
        -Itests/vibe/stubs -Itests/audio_stubs -Icomponents/bsp/include -Imain/vibe \
        tests/vibe/test_audio_lifecycle.c main/vibe/adpcm.c -o "${test_dir}/test_vibe_audio"
    "${test_dir}/test_vibe_audio"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined \
        -Itests/vibe/stubs -Itests/audio_stubs -Imain/vibe \
        tests/vibe/test_weather_worker.c -o "${test_dir}/test_vibe_weather_worker"
    "${test_dir}/test_vibe_weather_worker"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Imain/vibe \
        tests/vibe/test_portal.c main/vibe/portal.c -o "${test_dir}/test_vibe_portal"
    "${test_dir}/test_vibe_portal"
    "${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -Imain/vibe/radio \
        tests/vibe/test_radio_hls.cc -o "${test_dir}/test_vibe_hls"
    "${test_dir}/test_vibe_hls"
    "${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined -Imain/vibe/radio \
        tests/vibe/test_radio_memory.cc -o "${test_dir}/test_vibe_radio_memory"
    "${test_dir}/test_vibe_radio_memory"
    "${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined -pthread -Imain/vibe/radio \
        tests/vibe/test_radio_buffer.cc -o "${test_dir}/test_vibe_radio_buffer"
    "${test_dir}/test_vibe_radio_buffer"
    "${CC:-cc}" -std=c11 -Wno-deprecated-declarations -Itests/vibe/vendor/cjson -c \
        tests/vibe/vendor/cjson/cJSON.c -o "${test_dir}/vibe_json.o"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined \
        -Itests/vibe/power_stubs -Itests/vibe/stubs -Itests/bsp_stubs -Icomponents/bsp/include -Itests/vibe/vendor/cjson \
        tests/vibe/test_power.c "${test_dir}/vibe_json.o" -o "${test_dir}/test_vibe_power"
    "${test_dir}/test_vibe_power"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Imain/vibe -Itests/vibe/vendor/cjson \
        tests/vibe/test_weather.c main/vibe/weather.c "${test_dir}/vibe_json.o" -lm -o "${test_dir}/test_vibe_weather"
    "${test_dir}/test_vibe_weather"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined \
        -Itests/audio_stubs -Itests/vibe/vendor/cjson tests/vibe/test_setup_scan.c \
        "${test_dir}/vibe_json.o" -lm -o "${test_dir}/test_vibe_setup_scan"
    "${test_dir}/test_vibe_setup_scan"
    node tests/vibe/test_portal_ui.mjs
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -Imain/vibe -Itests/vibe/vendor/cjson \
        tests/vibe/test_radio_library.c "${test_dir}/vibe_json.o" -lm -o "${test_dir}/test_vibe_library"
    "${test_dir}/test_vibe_library"
    PYTHONDONTWRITEBYTECODE=1 python3 tests/vibe/test_weather_transport.py
    PYTHONDONTWRITEBYTECODE=1 python3 tests/vibe/test_radio_catalog.py
    PYTHONDONTWRITEBYTECODE=1 python3 tests/vibe/test_radio_source.py
    PYTHONDONTWRITEBYTECODE=1 python3 tests/vibe/test_radio_session.py
    PYTHONDONTWRITEBYTECODE=1 python3 tests/vibe/check_fonts.py
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -Imain/vibe \
        tests/vibe/test_agent_alert.c main/vibe/agent_alert.c -o "${test_dir}/test_vibe_agents"
    "${test_dir}/test_vibe_agents"
    node tests/agents/test_plugins.mjs
    rm -rf "${test_dir}"
    echo "Host tests: PASS"
}

run_firmware_checks() (
    local validation_build_dir

    if ! command -v idf.py >/dev/null 2>&1; then
        echo "ERROR: idf.py is not available; activate ESP-IDF 5.5.3 first." >&2
        return 1
    fi

    validation_build_dir="$(mktemp -d /tmp/ai-passport-firmware.XXXXXX)"
    trap 'case "${validation_build_dir}" in /tmp/ai-passport-firmware.*) rm -rf -- "${validation_build_dir}" ;; esac' EXIT

    SDKCONFIG_DEFAULTS="${repo_root}/sdkconfig.defaults" \
        idf.py -B "${validation_build_dir}" \
        -D "SDKCONFIG=${validation_build_dir}/sdkconfig" build
    idf.py -B "${validation_build_dir}" merge-bin \
        -o "${validation_build_dir}/FoloToy-AI-Passport-full.bin"
    python3 tools/verify_firmware.py "${validation_build_dir}"
    PYTHONDONTWRITEBYTECODE=1 python3 tools/archive_firmware.py create \
        "${validation_build_dir}" --archive-root "${repo_root}/build/firmware"
    mkdir -p "${repo_root}/build"
    install -m 0644 \
        "${validation_build_dir}/FoloToy-AI-Passport-full.bin" \
        "${repo_root}/build/FoloToy-AI-Passport-full.bin"
    echo "Firmware build: PASS"
)

cd "${repo_root}"
case "${mode}" in
    --all)
        run_static_checks
        run_firmware_checks
        ;;
    --static)
        run_static_checks
        ;;
    --firmware)
        run_firmware_checks
        ;;
    *)
        usage
        exit 2
        ;;
esac
