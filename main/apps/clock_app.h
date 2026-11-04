#ifndef APPS_CLOCK_APP_H_
#define APPS_CLOCK_APP_H_

#include "zos/app.h"

/**
 * Big HH:MM clock. Wakes once per minute and partially refreshes only the
 * time area (a full refresh on the hour clears ghosting), so the panel and
 * the CPU stay idle in between.
 */
class ClockApp : public zos::App {
public:
    ClockApp();

    const char* id() const override { return "clock"; }
    const char* title() const override { return "时钟"; }
    const char* summary() const override { return "分钟时钟"; }
    const zos::ConfigField* config_fields() const override;
    size_t config_field_count() const override;

    void OnEnter(zos::Context& ctx) override;
    void OnLeave(zos::Context& ctx) override;
    int64_t NextTickMs(zos::Context& ctx) override;
    void OnTick(zos::Context& ctx) override;
    void OnWake(zos::Context& ctx) override;
    void OnConfigChanged(zos::Context& ctx) override;
    void Render(zos::Context& ctx, ZectrixCanvas& canvas,
                const zos::Rect& content) override;

private:
    static void ApplyTimezone(const zos::Settings& settings);
    static zos::Rect TimeArea(const zos::Rect& content);
};

#endif  // APPS_CLOCK_APP_H_
