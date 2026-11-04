// Simulator HTTPS GET for the weather client: the system curl (real Open-Meteo).
#include <cstdio>
#include <string>

#include "apps/weather/weather.h"

bool WeatherHttpGet(const std::string& url, std::string* body) {
    // The URL is built by weather.cc from URL-encoded parts and never holds a quote.
    if (url.find('\'') != std::string::npos) return false;
    const std::string command = "curl -sf -m 10 '" + url + "'";
    FILE* pipe = popen(command.c_str(), "r");
    if (!pipe) return false;
    std::string out;
    char buffer[2048];
    size_t n;
    while ((n = fread(buffer, 1, sizeof(buffer), pipe)) > 0) out.append(buffer, n);
    if (pclose(pipe) != 0 || out.empty()) return false;
    *body = std::move(out);
    return true;
}
