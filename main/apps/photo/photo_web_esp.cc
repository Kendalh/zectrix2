// Firmware routes of the photo album page (esp_http_server). The logic is in photo_web.cc, shared with the simulator.
#include <cstdlib>
#include <cstring>

#include "apps/photo/photo_store.h"
#include "apps/photo/photo_web.h"
#include "apps/photo/photo_web_esp.h"
#include "esp_http_server.h"
#include "zos/web_server.h"

namespace {

esp_err_t SendText(httpd_req_t* req, const char* status, const std::string& body, const char* type) {
    httpd_resp_set_status(req, status);
    httpd_resp_set_type(req, type);
    return httpd_resp_send(req, body.data(), static_cast<ssize_t>(body.size()));
}

const char* StatusLine(int code) {
    switch (code) {
        case 200: return "200 OK";
        case 400: return "400 Bad Request";
        case 404: return "404 Not Found";
        case 507: return "507 Insufficient Storage";
        default: return "500 Internal Server Error";
    }
}

int IdOf(httpd_req_t* req) {
    const char* tail = std::strrchr(req->uri, '/');
    return tail ? std::atoi(tail + 1) : -1;
}

esp_err_t HandlePage(httpd_req_t* req) {
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    return httpd_resp_send(req, photos::web::kPageHtml, HTTPD_RESP_USE_STRLEN);
}

esp_err_t HandleList(httpd_req_t* req) {
    return SendText(req, "200 OK", photos::web::ListJson(), "application/json");
}

esp_err_t HandleGet(httpd_req_t* req) {
    std::vector<uint8_t> image;
    if (!photos::web::Get(IdOf(req), &image)) return httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "no such photo");
    httpd_resp_set_type(req, "application/octet-stream");
    return httpd_resp_send(req, reinterpret_cast<const char*>(image.data()), static_cast<ssize_t>(image.size()));
}

esp_err_t HandleDelete(httpd_req_t* req) {
    std::string error;
    const int code = photos::web::Delete(IdOf(req), &error);
    return SendText(req, StatusLine(code), code == 200 ? "ok" : error, "text/plain");
}

esp_err_t HandlePut(httpd_req_t* req) {
    if (req->content_len != photos::kImageBytes) {
        return SendText(req, StatusLine(400), "图片大小不对（应为 60000 字节）", "text/plain");
    }
    std::vector<uint8_t> body(req->content_len);
    size_t received = 0;
    while (received < body.size()) {
        const int n = httpd_req_recv(req, reinterpret_cast<char*>(body.data()) + received, body.size() - received);
        if (n <= 0) return httpd_resp_send_err(req, HTTPD_408_REQ_TIMEOUT, "timeout");
        received += static_cast<size_t>(n);
    }
    std::string title;
    char query[160];
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK) {
        char value[128];
        if (httpd_query_key_value(query, "title", value, sizeof(value)) == ESP_OK) title = value;
    }
    std::string error;
    const int code = photos::web::Put(title, body.data(), body.size(), &error);
    return SendText(req, StatusLine(code), code == 200 ? "ok" : error, "text/plain");
}

esp_err_t HandleSettingsGet(httpd_req_t* req) {
    return SendText(req, "200 OK", photos::web::SettingsJson(), "application/json");
}

esp_err_t HandleSettingsPost(httpd_req_t* req) {
    if (req->content_len == 0 || req->content_len > 256) return SendText(req, StatusLine(400), "bad body size", "text/plain");
    std::string body(req->content_len, '\0');
    size_t received = 0;
    while (received < body.size()) {
        const int n = httpd_req_recv(req, body.data() + received, body.size() - received);
        if (n <= 0) return httpd_resp_send_err(req, HTTPD_408_REQ_TIMEOUT, "timeout");
        received += static_cast<size_t>(n);
    }
    std::string error;
    const int code = photos::web::SetSettings(body, &error);
    return SendText(req, StatusLine(code), code == 200 ? "ok" : error, "text/plain");
}

}  // namespace

void RegisterPhotoRoutes(zos::WebServer& web) {
    web.AddRoute({"/photos", HTTP_GET, HandlePage, nullptr});
    web.AddRoute({"/api/photo-settings", HTTP_GET, HandleSettingsGet, nullptr});
    web.AddRoute({"/api/photo-settings", HTTP_POST, HandleSettingsPost, nullptr});
    web.AddRoute({"/api/photos", HTTP_GET, HandleList, nullptr});
    web.AddRoute({"/api/photos", HTTP_POST, HandlePut, nullptr});
    web.AddRoute({"/api/photos/*", HTTP_GET, HandleGet, nullptr});
    web.AddRoute({"/api/photos/*", HTTP_DELETE, HandleDelete, nullptr});
}
