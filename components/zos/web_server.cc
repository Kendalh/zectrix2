#include "zos/web_server.h"

#include <cstdlib>
#include <cstring>
#include <string>

#include "esp_log.h"
#include "zos/network.h"
#include "zos/shell.h"
#include "zos/web_api.h"
#include "web_ui.h"

namespace zos {

namespace {

constexpr char kTag[] = "zos_web";
constexpr size_t kMaxBody = 2048;

Shell& ShellOf(httpd_req_t* req) {
    return static_cast<WebServer*>(req->user_ctx)->shell();
}

esp_err_t SendJson(httpd_req_t* req, const std::string& json) {
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, json.c_str());
}

esp_err_t HandleIndex(httpd_req_t* req) {
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, kWebUiHtml, HTTPD_RESP_USE_STRLEN);
}

esp_err_t HandleStatus(httpd_req_t* req) {
    return SendJson(req, web_api::StatusJson(ShellOf(req)));
}

esp_err_t HandleApps(httpd_req_t* req) {
    return SendJson(req, web_api::AppsJson(ShellOf(req)));
}

// POST /api/apps/<id>/config
esp_err_t HandleConfig(httpd_req_t* req) {
    const std::string uri = req->uri;
    const std::string prefix = "/api/apps/";
    const std::string suffix = "/config";
    if (uri.size() <= prefix.size() + suffix.size() ||
        uri.compare(uri.size() - suffix.size(), suffix.size(), suffix) != 0) {
        return httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "not found");
    }
    const std::string id =
        uri.substr(prefix.size(), uri.size() - prefix.size() - suffix.size());
    if (req->content_len == 0 || req->content_len > kMaxBody) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad body size");
    }
    std::string body(req->content_len, '\0');
    size_t received = 0;
    while (received < body.size()) {
        const int n = httpd_req_recv(req, body.data() + received,
                                     body.size() - received);
        if (n <= 0) {
            return httpd_resp_send_err(req, HTTPD_408_REQ_TIMEOUT, "timeout");
        }
        received += n;
    }
    std::string error;
    switch (web_api::ApplyConfig(ShellOf(req), id, body.data(), body.size(),
                                 &error)) {
        case 200: return httpd_resp_sendstr(req, "ok");
        case 404: return httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, error.c_str());
        default: return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, error.c_str());
    }
}

}  // namespace

void WebServer::Start(Shell& shell) {
    if (server_ != nullptr) return;
    shell_ = &shell;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.uri_match_fn = httpd_uri_match_wildcard;
    config.stack_size = 6144;
    config.lru_purge_enable = true;
    if (httpd_start(&server_, &config) != ESP_OK) {
        ESP_LOGE(kTag, "httpd_start failed");
        server_ = nullptr;
        return;
    }
    const httpd_uri_t routes[] = {
        {"/", HTTP_GET, HandleIndex, this},
        {"/api/status", HTTP_GET, HandleStatus, this},
        {"/api/apps", HTTP_GET, HandleApps, this},
        {"/api/apps/*", HTTP_POST, HandleConfig, this},
    };
    for (const httpd_uri_t& route : routes) {
        httpd_register_uri_handler(server_, &route);
    }
    ESP_LOGI(kTag, "web server started");
}

}  // namespace zos
