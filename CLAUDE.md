# Project notes (zectrix-note4-epd-demo + zos)

Firmware for the ZECTRIX NOTE4 (ESP32-S3 N16R8, 400x300 B/W SSD2683 e-paper). The original repo was a hardware
demo; this branch adds **zos**, a small app platform (`components/zos`, apps in `main/apps`). Design and
how-to-add-an-app are in `docs/OS.md`: keep it in sync when you change keys, refresh rules or apps.
**Nothing has been run on a real device yet** (hotspot flow, SNTP/RTC, web page, partial-refresh look and the
Chinese font on the panel are unverified); the PC simulator is the only runtime check.

## Build
- ESP-IDF v6.0 at `~/esp/esp-idf` (project says >= 5.4). Export it with a clean PATH (`/usr/bin:...`, libusb in
  `LD_LIBRARY_PATH`), then `idf.py set-target esp32s3 && idf.py build`. `build/`, `sdkconfig`, `managed_components/`,
  `dist/` are git-ignored; `dependencies.lock` is tracked.
- One flashable image (bootloader + partitions + app, like the vendor's prebuilt): `idf.py merge-bin -o <ABSOLUTE
  path>.bin` (a relative path is resolved from `build/` and fails). Flash it at offset `0x0`. 3 MB factory app
  partition only, no OTA; the app is ~1.2 MB.
- GCC 15.2 (IDF 6) bug: copying a large constexpr `std::array`/array and modifying the copy mutates the source
  during constant evaluation. `ssd2683_waveform.h` copies element-wise for that reason; keep it that way.
- `zectrix_epd` needs `esp_driver_gpio` + `esp_driver_spi` in REQUIRES on IDF 6.
- `components/esp-wifi-connect` is vendored from the youn project (MIT): the registry version needs the `json`
  component that IDF 6 removed. Do not switch back to the registry version.

## Stacks and memory (real-device crashes)
- The shell UI loop runs in the main task. IDF defaults (main 3584, lwIP 3072, event 2304 bytes) made the device reboot
  on the Wi-Fi setup and weather pages and with USB connected (charging branch of the header). `sdkconfig.defaults`
  now sets main 12288, lwIP 8192, event 4096, WDT 10 s, mbedTLS buffers in PSRAM, and youn's Wi-Fi/PSRAM values.
  The weather worker task has a 16 KB stack. `sdkconfig.defaults` only applies to a NEW `sdkconfig`: delete
  `sdkconfig` (and rebuild) after changing it.
- FreeRTOS tasks must never return: end with `vTaskDelete(nullptr)` (weather worker did not and aborted the device).
- The config AP (esp-wifi-connect) starts its own httpd on port 80 and `ESP_ERROR_CHECK`s it: `Network::StartConfigAp`
  stops `zos::WebServer` first; the shell restarts it on the next wake when not in config mode.
- Page changes log `UI stack free N bytes`; if it drops below ~2000, raise the main stack before adding features.
- Hardware crashes cannot be reproduced here: ask the user for the serial log (`idf.py monitor`, 115200, USB-C)
  and decode the backtrace with the matching `build/*.elf`.

## Architecture rules
- Shell is single-threaded: an event queue (Key / PowerOff / Wake / Config). Other threads only call
  `Context::Wake()` or `Shell::PostConfigChanged()`. Never block in the Wi-Fi/SNTP callback; they set a flag and
  the shell does the work in `PollTimeSync()`.
- Keys are identical everywhere: UP/DOWN/OK clicks go to the app; hold OK 1.5 s = Home; hold DOWN 3 s = power off.
  Apps never see the holds. Footer shows only non-obvious key notes.
- **Design rule: partial refresh wherever possible**, including page switches (`Shell::Open` sends the whole
  screen as one partial). Full refresh only at boot, every `partial_limit` partials (ghosting), after full-screen
  apps, or on explicit `RequestFullRefresh()`. Do not add new unconditional full refreshes.
- The shell draws header (battery, Wi-Fi) and footer; an app draws only the content area and calls
  `ctx.Invalidate(rect)`. Display decides partial vs full: x/width widened to 8 px, a full 1bpp refresh must
  come first (and after any 4bpp image), full refresh after `partial_limit` partials (the clock raises it to 255
  and forces a full refresh on the hour).
- Web config page is generic: apps only declare `ConfigField`s (settings in NVS namespace `zos_<id>`). The JSON is
  in `zos/web_api.*` (no HTTP dependency) so the firmware and the simulator share it. Time is kept as UTC; the
  zone is the `TZ` env var.
- Light sleep / power management is not implemented (button task polls, Wi-Fi stays on): the clock only saves panel
  power.

## Clock + weather app (`main/apps/clock_weather_app.*`, `main/apps/weather/`)
- Open-Meteo (no key), adapted from the youn project's parser. `weather.cc` is pure (URLs, parsing, WMO tables) and is
  shared with the simulator; only `WeatherHttpGet` differs (`weather_http_esp.cc` = esp_http_client + cert bundle,
  `sim/sim_http.cc` = system curl, so the sim shows real weather). Uses `timezone=auto`.
- Fetching is blocking TLS, so it runs on a worker task (`xTaskCreate`, 8 KB stack) and ends with `ctx.Wake()`; the shell
  thread never waits. Worker and shell share state under `mutex_`; settings are only touched on the shell thread
  (the worker hands the geocoded lat/lon back and `OnWake()` saves it). A `generation_` counter drops results of
  an old city after a config change. Failed fetches retry after 5 min; no polling while Wi-Fi is down.
- Icons are 1bpp bitmaps from Font Awesome Free (OFL), generated by `python3 tools/generate_weather_icons.py` into
  `weather/weather_icons.h` (48 px current, 32 px forecast; day/night variants via Open-Meteo `is_day`). The enum
  order in `weather.h` must match the generator. The font has no degree sign, so `°` is drawn (`DrawTemp`); do not
  use the `℃` glyph (half width, looks squeezed). Layout: time left, weather card right, four forecast cards, one info line.
- The city is a typed name, shown as typed (the geocoded name can use characters the font lacks, e.g. 東京).

## Web management page
- `components/zos/web_ui.h`: hash routes; `#/` lists only apps with `config_fields` (apps without settings are not
  shown), `#/app/<id>` is that app's form. The API (`/api/apps`) still returns every app.

## Chinese UI
- The canvas decodes UTF-8: ASCII from the 8x16 font, everything else from `font/zectrix_cjk_font_16.h`
  (the characters used plus GB2312 level 1, 3755 common characters, so user-typed text such as city names works; GNU Unifont 16x16, OFL; ~570 KB source, ~130 KB flash). After adding Chinese text run
  `python3 tools/generate_cjk_font.py` (downloads Unifont once to `~/.cache/zectrix/`). Missing glyph = `?`.
- Noto Sans SC at 16 px looked blurry/heavy; Ark Pixel only exists at 10/12 px. Unifont at 16 px was the best look.
- A 400 px line holds ~24 Chinese characters. Stored choice values (e.g. time zones) are Chinese strings too.

## PC simulator (`sim/`)
- `make -C sim -j8`, then `python3 sim/web/server.py`: UI on **:8000**, the device web page on **:8080**
  (only answers while the simulated Wi-Fi is on). Always use these ports; kill a stale instance by PID
  (`ps aux | grep -E "sim_device|web/server"`) instead of picking other ports, and not with `pkill -f`.
- It compiles the real `shell.cc`, `chrome.cc`, `display.cc`, `settings.cc`, `web_api.cc` and the apps against
  shims in `sim/shim/` (FreeRTOS, NVS, esp_timer, the SSD2683 API, board buttons). The fake panel returns errors
  for what the driver would reject, and the UI shows a "driver error" count: watch it.
- Not simulated: refresh latency, real Wi-Fi/captive portal, NTP/RTC, power use. Settings reset on reboot.
- Holding DOWN powers the simulated device off (blank screen); use the "Power on" button. Do not leave it off.
- Needs `managed_components/espressif__cjson` from a prior firmware build.

## Publishing a build
- `scripts/upload_firmware.sh [dist/zectrix-os-vX.Y.Z.bin]` uploads to the Alibaba Cloud nginx host (same one the
  youn project uses) as `zectrix-os-vX.Y.Z.bin` (the file name carries the version, no timestamp; bump the version for every build) + `.sha256`, never overwriting, and verifies through
  nginx on the server. Host, key and directory are NOT stored in this repo; the script reads them from
  `~/workspace/aifin/sync_reports_to_cloud.sh`. Only add files under `/fin/firmware/`; never touch the nginx
  config or aifin's `index.html`/`report/`. Files are world-readable; the dev machine's network blocks the public
  domain, so do not try to download the link from here. Always give the user the SHA-256.

## Git
- Work on `feature`; `main` is protected and PRs are opened by the user on GitHub. Remotes: `origin` =
  itopinion/zectrix-note4-epd-demo (upstream), `kendalh` = Kendalh/zectrix2 (push `feature` there; SSH alias
  `github-personal`). The sibling `youn-ink-fourcolor-firmware` keeps its own original remote; do not change it.
