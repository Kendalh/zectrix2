// Simulated device: the real zos Shell, chrome, display logic and apps on the
// host. Commands come on stdin (one per line), used by web/server.py:
//
//   key up|down|ok|okhold|downhold   press, wait until the shell is quiet
//   screen                           just report the panel
//   net up|down                      Wi-Fi station on/off
//   battery <percent> [charging]
//   status | apps                    "JSON <body>" like the device web API
//   config <app id> <json>           "HTTP <code> <error>"
//   quit
// Frame replies: FRAME<TAB>path<TAB>full<TAB>partial<TAB>gray<TAB>errors<TAB>off<TAB>last
#include <chrono>
#include <cstdio>
#include <iostream>
#include <memory>
#include <sstream>
#include <thread>

#include "apps/ai/ai_chat.h"
#include "apps/ai_chat_app.h"
#include "apps/photo/photo_backend.h"
#include "apps/photo/photo_web.h"
#include "apps/photos_app.h"
#include "apps/ai_ink_app.h"
#include "apps/clock_weather_app.h"
#include "apps/epd_demo_app.h"
#include "apps/token_board_app.h"
#include "apps/wifi_setup_app.h"
#include "sim_platform.h"
#include "zos/display.h"
#include "zos/network.h"
#include "zos/shell.h"
#include "zos/web_api.h"
#include "zos/web_server.h"

namespace {

std::string Base64Decode(const std::string& text) {
    static const std::string kTable = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    uint32_t acc = 0;
    int bits = 0;
    for (const char c : text) {
        const size_t v = kTable.find(c);
        if (v == std::string::npos) continue;  // padding and anything else
        acc = (acc << 6) | static_cast<uint32_t>(v);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out.push_back(static_cast<char>((acc >> bits) & 0xFF));
        }
    }
    return out;
}

void WaitQuiet() {
    using namespace std::chrono;
    const auto start = steady_clock::now();
    auto last_change = start;
    int seen = sim::panel_stats().total() + sim::panel_stats().errors;
    while (steady_clock::now() - start < seconds(6)) {
        std::this_thread::sleep_for(milliseconds(20));
        const auto s = sim::panel_stats();
        const int now = s.total() + s.errors;
        if (now != seen) {
            seen = now;
            last_change = steady_clock::now();
        }
        if (steady_clock::now() - last_change > milliseconds(250) &&
            steady_clock::now() - start > milliseconds(200)) {
            return;
        }
    }
}

void ReplyFrame(const char* out_dir, int* index) {
    const std::vector<uint8_t> px = sim::panel_pixels();
    char path[512];
    std::snprintf(path, sizeof(path), "%s/zos_sim_%d.pgm", out_dir, (*index)++ % 4);
    if (FILE* f = std::fopen(path, "wb")) {
        std::fprintf(f, "P5\n400 300\n255\n");
        std::fwrite(px.data(), 1, px.size(), f);
        std::fclose(f);
    }
    const sim::PanelStats s = sim::panel_stats();
    std::printf("FRAME\t%s\t%d\t%d\t%d\t%d\t%d\t%d\t%s\n", path, s.full, s.partial,
                s.gray, s.errors, s.powered_off ? 1 : 0, sim::recording() ? 1 : 0,
                s.last.c_str());
}

}  // namespace

int main(int argc, char** argv) {
    std::string out_dir = "/tmp";
    for (int i = 1; i + 1 < argc; ++i) {
        if (std::string(argv[i]) == "--out") out_dir = argv[i + 1];
    }

    static ZectrixBoard board;
    static zos::Display display;
    static zos::Network network;
    static zos::WebServer web;
    static zos::Shell shell(board, display, network, web);

    board.Init();
    display.Init();
    photos::Store::Get().Init(NewPhotoBackend());
    shell.Register(std::make_unique<ClockWeatherApp>());
    shell.Register(std::make_unique<PhotosApp>());
    shell.Register(std::make_unique<AiChatApp>(board));
    shell.Register(std::make_unique<AiInkApp>(board));
    shell.Register(std::make_unique<TokenBoardApp>());
    shell.Register(std::make_unique<WifiSetupApp>());
    shell.Register(std::make_unique<EpdDemoApp>());
    network.Init([] { shell.Wake(); }, board.rtc());
    std::thread([] { shell.Run(); }).detach();

    // Splash (1.2 s), then the first Home draw.
    while (sim::panel_stats().full < 2) std::this_thread::sleep_for(std::chrono::milliseconds(20));
    WaitQuiet();
    std::printf("READY\n");
    std::fflush(stdout);

    int frame = 0;
    std::string line;
    while (std::getline(std::cin, line)) {
        std::istringstream in(line);
        std::string cmd, arg;
        in >> cmd >> arg;
        if (cmd == "quit") break;
        if (cmd == "key") {
            using B = ZectrixButton;
            using A = ZectrixButtonAction;
            if (arg == "up") board.Press(B::kUp, A::kClick);
            else if (arg == "down") board.Press(B::kDown, A::kClick);
            else if (arg == "ok") board.Press(B::kOk, A::kClick);
            else if (arg == "okhold") board.Press(B::kOk, A::kLongPress);
            else if (arg == "uphold") board.Press(B::kUp, A::kHold);
            else if (arg == "downhold") board.Press(B::kDown, A::kHold);
            else if (arg == "downlong") board.Press(B::kDown, A::kLongPress);
            else { std::printf("ERR unknown key\n"); std::fflush(stdout); continue; }
            WaitQuiet();
            ReplyFrame(out_dir.c_str(), &frame);
        } else if (cmd == "screen") {
            ReplyFrame(out_dir.c_str(), &frame);
        } else if (cmd == "net") {
            sim::net_set(arg == "up");
            WaitQuiet();
            ReplyFrame(out_dir.c_str(), &frame);
        } else if (cmd == "battery") {
            std::string charging;
            in >> charging;
            board.SetBattery(std::atoi(arg.c_str()), charging == "charging");
            // The header only refreshes with the next screen change.
            board.Press(ZectrixButton::kUp, ZectrixButtonAction::kClick);
            WaitQuiet();
            ReplyFrame(out_dir.c_str(), &frame);
        } else if (cmd == "status") {
            std::printf("JSON %s\n", zos::web_api::StatusJson(shell).c_str());
        } else if (cmd == "apps") {
            std::printf("JSON %s\n", zos::web_api::AppsJson(shell).c_str());
        } else if (cmd == "photosettings") {  // photosettings [json]: read, or write when a body follows
            std::string body;
            std::getline(in, body);
            if (arg.empty()) {
                std::printf("JSON %s\n", photos::web::SettingsJson().c_str());
            } else {
                std::string error;
                const int code = photos::web::SetSettings(arg + body, &error);
                std::printf("HTTP %d %s\n", code, error.c_str());
            }
        } else if (cmd == "photolist") {
            std::printf("JSON %s\n", photos::web::ListJson().c_str());
        } else if (cmd == "photoput") {  // photoput <urlencoded title> <base64 of the 60000 bytes>
            std::string data;
            in >> data;
            const std::string bytes = Base64Decode(data);
            std::string error;
            const int code = photos::web::Put(arg, reinterpret_cast<const uint8_t*>(bytes.data()), bytes.size(), &error);
            std::printf("HTTP %d %s\n", code, error.c_str());
        } else if (cmd == "photodel") {
            std::string error;
            const int code = photos::web::Delete(std::atoi(arg.c_str()), &error);
            std::printf("HTTP %d %s\n", code, error.c_str());
        } else if (cmd == "photoget") {
            std::vector<uint8_t> image;
            if (!photos::web::Get(std::atoi(arg.c_str()), &image)) {
                std::printf("HTTP 404 no such photo\n");
            } else {
                std::printf("B64 %s\n", ai::Base64Encode(std::string(image.begin(), image.end())).c_str());
            }
        } else if (cmd == "config") {
            std::string body;
            std::getline(in, body);
            std::string error;
            const int code = zos::web_api::ApplyConfig(shell, arg, body.data(), body.size(), &error);
            WaitQuiet();
            std::printf("HTTP %d %s\n", code, error.c_str());
        } else {
            std::printf("ERR unknown command\n");
        }
        std::fflush(stdout);
    }
    return 0;
}
