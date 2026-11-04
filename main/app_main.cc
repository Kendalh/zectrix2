#include <memory>

#include "apps/ai_chat_app.h"
#include "apps/clock_weather_app.h"
#include "apps/epd_demo_app.h"
#include "apps/token_board_app.h"
#include "apps/wifi_setup_app.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "zectrix_board.h"
#include "zos/display.h"
#include "zos/network.h"
#include "zos/shell.h"
#include "zos/web_server.h"

namespace {
constexpr char kTag[] = "zos";
}

extern "C" void app_main(void) {
    static ZectrixBoard board;
    static zos::Display display;
    static zos::Network network;
    static zos::WebServer web;
    static zos::Shell shell(board, display, network, web);

    // Settings live in NVS and apps read them while being constructed.
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES ||
        err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    err = board.Init();
    if (err != ESP_OK) {
        ESP_LOGE(kTag, "board initialization failed: %s", esp_err_to_name(err));
    }
    err = display.Init();
    if (err != ESP_OK) {
        ESP_LOGE(kTag, "display initialization failed: %s", esp_err_to_name(err));
        return;
    }

    shell.Register(std::make_unique<ClockWeatherApp>());
    shell.Register(std::make_unique<AiChatApp>(board));
    shell.Register(std::make_unique<TokenBoardApp>());
    shell.Register(std::make_unique<WifiSetupApp>());
    shell.Register(std::make_unique<EpdDemoApp>());

    network.Init([] { shell.Wake(); }, board.rtc());
    shell.Run();
}
