#include "zos/network.h"

#include <sys/time.h>

#include <ctime>

#include "esp_log.h"
#include "esp_netif_sntp.h"
#include "esp_sntp.h"
#include "esp_wifi.h"
#include "rtc_pcf8563.h"
#include "wifi_manager.h"

namespace zos {

namespace {

constexpr char kTag[] = "zos_net";
constexpr time_t kMinValidTime = 1700000000;  // Nov 2023
Network* g_network = nullptr;

// The RTC holds UTC; the zone is applied by TZ (see the clock app).
void LoadTimeFromRtc(RtcPcf8563* rtc) {
    tm utc = {};
    if (rtc == nullptr || !rtc->GetTime(utc)) return;
    utc.tm_isdst = 0;
    const time_t seconds = timegm(&utc);
    if (seconds < kMinValidTime) return;
    const timeval tv = {seconds, 0};
    settimeofday(&tv, nullptr);
    ESP_LOGI(kTag, "system time restored from RTC");
}

}  // namespace

void Network::Init(std::function<void()> on_change, RtcPcf8563* rtc) {
    on_change_ = std::move(on_change);
    rtc_ = rtc;
    LoadTimeFromRtc(rtc_);

    WifiManagerConfig config;
    config.ssid_prefix = "ZECTRIX";
    config.language = "en-US";
    WifiManager& wifi = WifiManager::GetInstance();
    wifi.SetEventCallback([this](WifiEvent event) {
        if (event == WifiEvent::Connected) StartSntp();
        if (on_change_) on_change_();
    });
    if (!wifi.Initialize(config)) {
        ESP_LOGE(kTag, "Wi-Fi initialization failed");
        return;
    }
    wifi.SetPowerSaveLevel(WifiPowerSaveLevel::LOW_POWER);
    wifi.StartStation();
}

void Network::StartSntp() {
    if (sntp_started_) return;
    sntp_started_ = true;
    g_network = this;
    esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
    config.server_from_dhcp = false;
    config.renew_servers_after_new_IP = true;
    // Runs on the lwIP task: only flag the sync and wake the shell.
    config.sync_cb = [](timeval*) {
        if (g_network != nullptr) {
            g_network->time_synced_ = true;
            if (g_network->on_change_) g_network->on_change_();
        }
    };
    if (esp_netif_sntp_init(&config) != ESP_OK) {
        ESP_LOGW(kTag, "SNTP start failed");
        sntp_started_ = false;
    }
}

void Network::PollTimeSync() {
    if (!time_synced_.exchange(false)) return;
    ESP_LOGI(kTag, "time synchronized");
    if (rtc_ != nullptr) {
        const time_t now = time(nullptr);
        tm utc = {};
        gmtime_r(&now, &utc);
        rtc_->SetTime(utc);
    }
}

bool Network::connected() const {
    return WifiManager::GetInstance().IsConnected();
}
std::string Network::ssid() const { return WifiManager::GetInstance().GetSsid(); }
std::string Network::ip() const {
    return WifiManager::GetInstance().GetIpAddress();
}
int Network::rssi() const { return WifiManager::GetInstance().GetRssi(); }

void Network::StartConfigAp() {
    if (before_config_ap_) before_config_ap_();
    WifiManager::GetInstance().StartConfigAp();
}
void Network::StopConfigAp() {
    WifiManager& wifi = WifiManager::GetInstance();
    wifi.StopConfigAp();
    wifi.StartStation();
}
bool Network::config_mode() const {
    return WifiManager::GetInstance().IsConfigMode();
}
std::string Network::ap_ssid() const {
    return WifiManager::GetInstance().GetApSsid();
}
std::string Network::ap_url() const {
    return WifiManager::GetInstance().GetApWebUrl();
}

bool Network::time_valid() { return time(nullptr) >= kMinValidTime; }

}  // namespace zos
