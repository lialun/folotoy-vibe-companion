idf_component_register(
    SRCS "vibe/app.c" "vibe/model.c" "vibe/messages.c" "vibe/protocol.c" "vibe/portal.c" "vibe/network.c" "vibe/ble.c" "vibe/wifi_profiles.c"
         "vibe/audio.c" "vibe/media_workspace.c" "vibe/voice_capture.c" "vibe/voice_tx.c" "vibe/voice_uplink.c" "vibe/voice_power.c" "vibe/adpcm.c" "vibe/ble_hci_probe.c" "vibe/ui.c" "vibe/vibe_font.c" "vibe/font_20.c" "vibe/power.c"
         "vibe/quota.c" "vibe/agent_alert.c" "vibe/appearance.c" "vibe/clock_sync.c" "vibe/signal.c" "vibe/prefs.c" "vibe/weather.c" "vibe/weather_transport.c" "vibe/weather_worker.c" "vibe/radio/catalog_service.cc" "vibe/radio/library.c"
         "vibe/ui_sfx_tone.c" "vibe/radio/remote.c"
    INCLUDE_DIRS "." "vibe" "vibe/radio"
    REQUIRES bsp esp_event esp_hw_support esp_netif esp_timer esp_wifi nvs_flash
             esp_http_server esp_http_client esp-tls json esp_pm bt
    PRIV_REQUIRES esp_coex
    EMBED_FILES "vibe/provisioning.html"
)
target_link_options(${COMPONENT_LIB} INTERFACE
    "-Wl,--wrap=ble_transport_to_ll_acl_impl"
    "-Wl,--wrap=ble_transport_to_hs_evt_impl"
    "-Wl,--wrap=ble_hs_hci_evt_process")
