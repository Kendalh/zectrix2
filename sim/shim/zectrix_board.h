// Host shim of ZectrixBoard: buttons come from the simulator, power is fake.
#pragma once
#include <cstdint>
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

class RtcPcf8563;

struct ChargeStatus {
    struct Snapshot {
        bool charging = false;
    };
};

enum class ZectrixButton : uint8_t { kUp = 0, kDown, kOk };
enum class ZectrixButtonAction : uint8_t { kClick = 0, kLongPress };

struct ZectrixButtonEvent {
    ZectrixButton button = ZectrixButton::kOk;
    ZectrixButtonAction action = ZectrixButtonAction::kClick;
};

struct ZectrixPowerSnapshot {
    bool battery_valid = false;
    uint16_t battery_mv = 0;
    uint8_t battery_percent = 0;
    ChargeStatus::Snapshot charge = {};
};

class ZectrixBoard {
public:
    esp_err_t Init() { return ESP_OK; }
    bool WaitButton(ZectrixButtonEvent* event, TickType_t timeout);
    RtcPcf8563* rtc() const { return nullptr; }
    ZectrixPowerSnapshot ReadPowerSnapshot();
    void SetPowerLed(bool) {}
    void SetAudioPower(bool) {}
    void CutBatteryPower() {}

    // Simulator controls.
    void Press(ZectrixButton button, ZectrixButtonAction action);
    void SetBattery(int percent, bool charging);

private:
    QueueHandle_t buttons_ = nullptr;
    int battery_percent_ = 85;
    bool charging_ = false;
};
