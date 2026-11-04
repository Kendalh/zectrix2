#ifndef ZOS_NETWORK_H_
#define ZOS_NETWORK_H_

#include <atomic>
#include <functional>
#include <string>

class RtcPcf8563;

namespace zos {

/**
 * Wi-Fi station / provisioning hotspot (esp-wifi-connect) and time sync.
 * `on_change` runs on a system task: it must only post to the shell.
 */
class Network {
public:
    void Init(std::function<void()> on_change, RtcPcf8563* rtc);

    bool connected() const;
    std::string ssid() const;
    std::string ip() const;
    int rssi() const;

    /** Start the provisioning hotspot with its captive portal. */
    void StartConfigAp();
    /** Leave provisioning and reconnect with the saved networks. */
    void StopConfigAp();
    bool config_mode() const;
    std::string ap_ssid() const;
    std::string ap_url() const;

    /** Shell thread: store a fresh NTP time in the RTC. Call on every wake. */
    void PollTimeSync();

    /** True once the system clock holds a plausible date. */
    static bool time_valid();

private:
    void StartSntp();

    std::function<void()> on_change_;
    RtcPcf8563* rtc_ = nullptr;
    bool sntp_started_ = false;
    std::atomic<bool> time_synced_{false};
};

}  // namespace zos

#endif  // ZOS_NETWORK_H_
