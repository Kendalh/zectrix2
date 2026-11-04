#include "apps/wifi_setup_app.h"

#include <cstdio>
#include <string>

zos::KeyHints WifiSetupApp::hints() const {
    zos::KeyHints hints;
    hints.select = (net_ != nullptr && net_->config_mode()) ? "取消"
                                                            : "配置 Wi-Fi";
    return hints;
}

void WifiSetupApp::OnEnter(zos::Context& ctx) { net_ = &ctx.net(); }

void WifiSetupApp::OnLeave(zos::Context& ctx) {
    if (ctx.net().config_mode()) ctx.net().StopConfigAp();
}

bool WifiSetupApp::OnKey(zos::Context& ctx, zos::Key key) {
    if (key != zos::Key::kSelect) return false;
    if (ctx.net().config_mode()) {
        ctx.net().StopConfigAp();
    } else {
        ctx.net().StartConfigAp();
    }
    ctx.Invalidate();
    return true;
}

void WifiSetupApp::OnWake(zos::Context& ctx) { ctx.Invalidate(); }

void WifiSetupApp::Render(zos::Context& ctx, ZectrixCanvas& canvas,
                          const zos::Rect& content) {
    zos::Network& net = ctx.net();
    constexpr int kLine = 26;
    const int x = 20;
    int y = content.y + 16;
    char line[96];
    const auto put = [&](const char* text, int scale = 1) {
        canvas.Text(x, y, text, scale);
        y += kLine;
    };

    if (net.config_mode()) {
        put("配网模式", 2);
        y += 12;
        put("1. 手机连接热点：");
        std::snprintf(line, sizeof(line), "   %s", net.ap_ssid().c_str());
        put(line);
        put("2. 自动弹出配置页，否则访问：");
        std::snprintf(line, sizeof(line), "   %s", net.ap_url().c_str());
        put(line);
        put("3. 选择你的网络并保存");
    } else if (net.connected()) {
        put("已连接", 2);
        y += 12;
        std::snprintf(line, sizeof(line), "网络：%s", net.ssid().c_str());
        put(line);
        std::snprintf(line, sizeof(line), "IP：%s", net.ip().c_str());
        put(line);
        std::snprintf(line, sizeof(line), "信号：%d dBm", net.rssi());
        put(line);
        std::snprintf(line, sizeof(line), "设置页：http://%s/",
                      net.ip().c_str());
        put(line);
    } else {
        put("未连接", 2);
        y += 12;
        put("按 OK 从手机添加或");
        put("更换 Wi-Fi 网络。");
    }
}
