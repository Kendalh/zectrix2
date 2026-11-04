// Firmware HTTPS GET for the weather client (the simulator has its own, using curl).
#include "apps/weather/weather.h"

#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"

namespace {

constexpr char kTag[] = "weather_http";
constexpr size_t kResponseMax = 12 * 1024;

struct Response {
    std::string body;
    bool overflow = false;
};

esp_err_t OnEvent(esp_http_client_event_t* event) {
    if (event->event_id == HTTP_EVENT_ON_DATA && event->user_data) {
        auto* response = static_cast<Response*>(event->user_data);
        if (response->body.size() + event->data_len > kResponseMax) {
            response->overflow = true;
        } else {
            response->body.append(static_cast<const char*>(event->data), event->data_len);
        }
    }
    return ESP_OK;
}

}  // namespace

bool WeatherHttpGet(const std::string& url, std::string* body) {
    Response response;
    esp_http_client_config_t config = {};
    config.url = url.c_str();
    config.method = HTTP_METHOD_GET;
    config.event_handler = OnEvent;
    config.user_data = &response;
    config.timeout_ms = 10000;
    config.crt_bundle_attach = esp_crt_bundle_attach;
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) return false;
    const esp_err_t err = esp_http_client_perform(client);
    const int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);
    if (err != ESP_OK) {
        ESP_LOGW(kTag, "request failed: %s", esp_err_to_name(err));
        return false;
    }
    if (status != 200 || response.overflow) {
        ESP_LOGW(kTag, "HTTP %d overflow=%d", status, response.overflow ? 1 : 0);
        return false;
    }
    *body = std::move(response.body);
    return true;
}
