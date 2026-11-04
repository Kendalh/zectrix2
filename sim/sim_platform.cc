// Host implementations of the ESP-IDF pieces the OS layer touches:
// FreeRTOS queues/tasks, NVS, esp_timer, the e-paper panel, the board
// buttons/battery, Wi-Fi and the (absent) device HTTP server.
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <deque>
#include <map>
#include <mutex>
#include <thread>

#include "esp_log.h"
#include "esp_sleep.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "nvs.h"
#include "sim_platform.h"
#include "zectrix_board.h"
#include "zectrix_epd.h"
#include "zos/network.h"
#include "zos/web_server.h"

using Clock = std::chrono::steady_clock;

// ---------------------------------------------------------------- FreeRTOS
namespace {
struct Queue {
    std::mutex mutex;
    std::condition_variable cv;
    std::deque<std::vector<uint8_t>> items;
    uint32_t length, item_size;
};
}  // namespace

QueueHandle_t xQueueCreate(uint32_t length, uint32_t item_size) {
    auto* q = new Queue();
    q->length = length;
    q->item_size = item_size;
    return q;
}

static BaseType_t Send(QueueHandle_t handle, const void* item, bool front) {
    auto* q = static_cast<Queue*>(handle);
    std::lock_guard<std::mutex> lock(q->mutex);
    if (q->items.size() >= q->length) return pdFALSE;
    std::vector<uint8_t> copy(static_cast<const uint8_t*>(item),
                              static_cast<const uint8_t*>(item) + q->item_size);
    if (front) q->items.push_front(std::move(copy));
    else q->items.push_back(std::move(copy));
    q->cv.notify_one();
    return pdTRUE;
}

BaseType_t xQueueSend(QueueHandle_t q, const void* item, TickType_t) {
    return Send(q, item, false);
}
BaseType_t xQueueSendToFront(QueueHandle_t q, const void* item, TickType_t) {
    return Send(q, item, true);
}

BaseType_t xQueueReceive(QueueHandle_t handle, void* item, TickType_t wait) {
    auto* q = static_cast<Queue*>(handle);
    std::unique_lock<std::mutex> lock(q->mutex);
    auto ready = [&] { return !q->items.empty(); };
    if (wait == portMAX_DELAY) {
        q->cv.wait(lock, ready);
    } else if (!q->cv.wait_for(lock, std::chrono::milliseconds(wait), ready)) {
        return pdFALSE;
    }
    std::memcpy(item, q->items.front().data(), q->item_size);
    q->items.pop_front();
    return pdTRUE;
}

BaseType_t xTaskCreate(TaskFunction_t fn, const char*, uint32_t, void* arg,
                       uint32_t, TaskHandle_t*) {
    std::thread([fn, arg] { fn(arg); }).detach();
    return pdPASS;
}

void vTaskDelay(TickType_t ticks) {
    std::this_thread::sleep_for(std::chrono::milliseconds(ticks));
}

int64_t esp_timer_get_time() {
    static const auto start = Clock::now();
    return std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - start).count();
}

// ---------------------------------------------------------------- NVS
namespace {
std::mutex g_nvs_mutex;
std::map<std::string, std::string> g_nvs;  // "ns/key" -> value
std::vector<std::string> g_nvs_handles;    // handle - 1 -> namespace

std::string NvsKey(nvs_handle_t h, const char* key) {
    return g_nvs_handles[h - 1] + "/" + key;
}
}  // namespace

esp_err_t nvs_open(const char* ns, nvs_open_mode_t, nvs_handle_t* out) {
    std::lock_guard<std::mutex> lock(g_nvs_mutex);
    g_nvs_handles.push_back(ns);
    *out = static_cast<nvs_handle_t>(g_nvs_handles.size());
    return ESP_OK;
}
void nvs_close(nvs_handle_t) {}
esp_err_t nvs_commit(nvs_handle_t) { return ESP_OK; }

esp_err_t nvs_get_str(nvs_handle_t h, const char* key, char* out, size_t* length) {
    std::lock_guard<std::mutex> lock(g_nvs_mutex);
    auto it = g_nvs.find(NvsKey(h, key));
    if (it == g_nvs.end()) return ESP_ERR_NVS_NOT_FOUND;
    if (out != nullptr) std::memcpy(out, it->second.c_str(), it->second.size() + 1);
    *length = it->second.size() + 1;
    return ESP_OK;
}
esp_err_t nvs_set_str(nvs_handle_t h, const char* key, const char* value) {
    std::lock_guard<std::mutex> lock(g_nvs_mutex);
    g_nvs[NvsKey(h, key)] = value;
    return ESP_OK;
}
esp_err_t nvs_get_i32(nvs_handle_t h, const char* key, int32_t* out) {
    std::lock_guard<std::mutex> lock(g_nvs_mutex);
    auto it = g_nvs.find(NvsKey(h, key));
    if (it == g_nvs.end()) return ESP_ERR_NVS_NOT_FOUND;
    *out = std::atoi(it->second.c_str());
    return ESP_OK;
}
esp_err_t nvs_set_i32(nvs_handle_t h, const char* key, int32_t value) {
    return nvs_set_str(h, key, std::to_string(value).c_str());
}
esp_err_t nvs_get_u8(nvs_handle_t h, const char* key, uint8_t* out) {
    int32_t value;
    const esp_err_t err = nvs_get_i32(h, key, &value);
    if (err == ESP_OK) *out = static_cast<uint8_t>(value);
    return err;
}
esp_err_t nvs_set_u8(nvs_handle_t h, const char* key, uint8_t value) {
    return nvs_set_i32(h, key, value);
}

// ---------------------------------------------------------------- panel
namespace {

constexpr int kW = ZECTRIX_EPD_PANEL_WIDTH, kH = ZECTRIX_EPD_PANEL_HEIGHT;

struct Panel {
    std::mutex mutex;
    std::vector<uint8_t> gray = std::vector<uint8_t>(kW * kH, 255);
    sim::PanelStats stats;
    bool powered = false;
    bool base_valid = false;  // a full 1bpp refresh happened (partial needs it)
} g_panel;

esp_err_t Fail(const char* what) {
    ESP_LOGE("sim_epd", "driver would reject: %s", what);
    ++g_panel.stats.errors;
    g_panel.stats.last = std::string("ERROR ") + what;
    return ESP_ERR_INVALID_STATE;
}

}  // namespace

struct zectrix_epd_t { int unused; };

void zectrix_epd_get_default_config(zectrix_epd_config_t*) {}
esp_err_t zectrix_epd_new(const zectrix_epd_config_t*, zectrix_epd_handle_t* out) {
    static zectrix_epd_t epd;
    *out = &epd;
    return ESP_OK;
}
esp_err_t zectrix_epd_power_on(zectrix_epd_handle_t) {
    g_panel.powered = true;
    return ESP_OK;
}
esp_err_t zectrix_epd_power_off(zectrix_epd_handle_t) {
    g_panel.powered = false;
    return ESP_OK;
}

esp_err_t zectrix_epd_refresh_full_1bpp(zectrix_epd_handle_t, const uint8_t* fb, size_t size) {
    std::lock_guard<std::mutex> lock(g_panel.mutex);
    if (!g_panel.powered) return Fail("full refresh while powered off");
    if (size != ZECTRIX_EPD_1BPP_FRAME_BYTES) return Fail("full 1bpp: bad size");
    for (int y = 0; y < kH; ++y)
        for (int x = 0; x < kW; ++x)
            g_panel.gray[y * kW + x] = (fb[y * (kW / 8) + x / 8] >> (7 - (x & 7))) & 1 ? 255 : 0;
    g_panel.base_valid = true;
    ++g_panel.stats.full;
    g_panel.stats.last = "full 1bpp";
    return ESP_OK;
}

// The real driver accepts partial refreshes of this rect only after the seed; the fake panel just allows partials again.
esp_err_t zectrix_epd_seed_bw_region(zectrix_epd_handle_t, const zectrix_epd_rect_t* r, const uint8_t*, size_t size) {
    std::lock_guard<std::mutex> lock(g_panel.mutex);
    if (r->x % 8 != 0 || r->width % 8 != 0) return Fail("seed rect not 8-px aligned");
    if (size != static_cast<size_t>(r->width / 8 * r->height)) return Fail("seed: bad size");
    g_panel.base_valid = true;
    return ESP_OK;
}

esp_err_t zectrix_epd_refresh_partial_1bpp(zectrix_epd_handle_t, const zectrix_epd_rect_t* r,
                                           const uint8_t* px, size_t size) {
    std::lock_guard<std::mutex> lock(g_panel.mutex);
    if (!g_panel.powered) return Fail("partial refresh while powered off");
    if (!g_panel.base_valid) return Fail("partial refresh without a 1bpp base image");
    if (r->x % 8 != 0 || r->width % 8 != 0) return Fail("partial rect not 8-px aligned");
    if (r->x < 0 || r->y < 0 || r->width <= 0 || r->height <= 0 ||
        r->x + r->width > kW || r->y + r->height > kH) return Fail("partial rect out of range");
    if (size != static_cast<size_t>(r->width / 8 * r->height)) return Fail("partial: bad size");
    const int stride = r->width / 8;
    for (int y = 0; y < r->height; ++y)
        for (int x = 0; x < r->width; ++x)
            g_panel.gray[(r->y + y) * kW + r->x + x] =
                (px[y * stride + x / 8] >> (7 - (x & 7))) & 1 ? 255 : 0;
    ++g_panel.stats.partial;
    char text[64];
    std::snprintf(text, sizeof(text), "partial %d,%d %dx%d", r->x, r->y, r->width, r->height);
    g_panel.stats.last = text;
    return ESP_OK;
}

esp_err_t zectrix_epd_refresh_full_4bpp(zectrix_epd_handle_t, const uint8_t* fb, size_t size) {
    std::lock_guard<std::mutex> lock(g_panel.mutex);
    if (!g_panel.powered) return Fail("4bpp refresh while powered off");
    if (size != ZECTRIX_EPD_4BPP_FRAME_BYTES) return Fail("full 4bpp: bad size");
    for (int y = 0; y < kH; ++y)
        for (int x = 0; x < kW; ++x) {
            const uint8_t byte = fb[y * (kW / 2) + x / 2];
            const int v = (x & 1) ? (byte & 0x0F) : (byte >> 4);
            g_panel.gray[y * kW + x] = static_cast<uint8_t>(v * 17);
        }
    g_panel.base_valid = false;  // partial needs a new 1bpp base afterwards
    ++g_panel.stats.gray;
    g_panel.stats.last = "full 4bpp (16 gray)";
    return ESP_OK;
}

namespace sim {
PanelStats panel_stats() {
    std::lock_guard<std::mutex> lock(g_panel.mutex);
    return g_panel.stats;
}
std::vector<uint8_t> panel_pixels() {
    std::lock_guard<std::mutex> lock(g_panel.mutex);
    return g_panel.gray;
}
}  // namespace sim

void esp_deep_sleep_start() {
    {
        std::lock_guard<std::mutex> lock(g_panel.mutex);
        g_panel.stats.powered_off = true;
    }
    ESP_LOGI("sim", "deep sleep: device is off (use reset to boot again)");
    for (;;) std::this_thread::sleep_for(std::chrono::hours(1));
}

// ---------------------------------------------------------------- board
bool ZectrixBoard::WaitButton(ZectrixButtonEvent* event, TickType_t timeout) {
    if (buttons_ == nullptr) buttons_ = xQueueCreate(16, sizeof(ZectrixButtonEvent));
    return xQueueReceive(buttons_, event, timeout) == pdTRUE;
}

void ZectrixBoard::Press(ZectrixButton button, ZectrixButtonAction action) {
    if (buttons_ == nullptr) buttons_ = xQueueCreate(16, sizeof(ZectrixButtonEvent));
    ZectrixButtonEvent event;
    event.button = button;
    event.action = action;
    xQueueSend(buttons_, &event, 0);
}

ZectrixPowerSnapshot ZectrixBoard::ReadPowerSnapshot() {
    ZectrixPowerSnapshot snapshot;
    snapshot.battery_valid = true;
    snapshot.battery_percent = static_cast<uint8_t>(battery_percent_);
    snapshot.battery_mv = static_cast<uint16_t>(3300 + battery_percent_ * 9);
    snapshot.charge.charging = charging_;
    return snapshot;
}

void ZectrixBoard::SetBattery(int percent, bool charging) {
    battery_percent_ = percent;
    charging_ = charging;
}

// ---------------------------------------------------------------- network
namespace {
std::function<void()> g_on_change;
bool g_up = true, g_config = false;
}  // namespace

namespace sim {
void net_set(bool up) {
    g_up = up;
    if (up) g_config = false;
    if (g_on_change) g_on_change();
}
void net_fire_change() {
    if (g_on_change) g_on_change();
}
}  // namespace sim

namespace zos {
void Network::Init(std::function<void()> on_change, RtcPcf8563*) { g_on_change = on_change; }
bool Network::connected() const { return g_up && !g_config; }
std::string Network::ssid() const { return g_up ? "SimWiFi" : ""; }
std::string Network::ip() const { return g_up ? "192.168.1.50" : ""; }
int Network::rssi() const { return -60; }
void Network::StartConfigAp() {
    g_config = true;
    if (g_on_change) g_on_change();
}
void Network::StopConfigAp() {
    g_config = false;
    if (g_on_change) g_on_change();
}
bool Network::config_mode() const { return g_config; }
std::string Network::ap_ssid() const { return "ZECTRIX-SIM"; }
std::string Network::ap_url() const { return "http://192.168.4.1"; }
void Network::PollTimeSync() {}
bool Network::time_valid() { return true; }
void Network::StartSntp() {}

void WebServer::Start(Shell& shell) { shell_ = &shell; }
void WebServer::Stop() {}
}  // namespace zos
