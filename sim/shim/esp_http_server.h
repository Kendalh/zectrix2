// Host shim: the sim has no esp_http_server; zos::WebServer is a no-op here
// and tools/server.py serves the same JSON via zos/web_api.h.
#pragma once
typedef void* httpd_handle_t;
// Only the type that zos::WebServer::AddRoute names: the sim never registers routes.
struct httpd_uri_t {
    int unused;
};
