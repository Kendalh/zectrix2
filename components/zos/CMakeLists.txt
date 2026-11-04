idf_component_register(
    SRCS
        "display.cc"
        "chrome.cc"
        "settings.cc"
        "shell.cc"
        "network.cc"
        "web_server.cc"
        "web_api.cc"
    INCLUDE_DIRS "include"
    REQUIRES
        zectrix_board
        zectrix_demo_ui
        zectrix_epd
        esp_http_server
    PRIV_REQUIRES
        esp_netif
        esp_wifi
        esp_event
        esp_timer
        lwip
        nvs_flash
        cjson
        esp-wifi-connect
)

target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)
