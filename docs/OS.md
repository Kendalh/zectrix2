# ZECTRIX OS

The demo firmware is now a small app platform. The panel driver
(`zectrix_epd`) and board code (`zectrix_board`) are unchanged; the OS layer is
the `zos` component and apps live in `main/apps/`.

## Screen layout

```text
+--------------------------------------------+
| TITLE                      [wifi] 87% [bat]|  header   (shell)
+--------------------------------------------+
|                                            |
|              app content                   |  content  (app)
|                                            |
+--------------------------------------------+
| OK Run                    Hold OK: Home    |  footer   (shell)
+--------------------------------------------+
```

The footer lists only keys whose meaning is not the obvious UP/DOWN
move; apps provide them through `App::hints()`.

## Keys (same on every page)

| Key | Action |
| --- | --- |
| UP / DOWN click | app-defined (Home: move selection) |
| OK click | app-defined (Home: open app) |
| OK hold 1.5 s | back to Home |
| DOWN hold 3 s | power off, on the app list only (inside an app the hold is ignored) |
| UP / DOWN hold 1 s | optional `Key::kUpHold` / `kDownHold` for apps (AI chat: new session / session list); return true from `OnKey` to take it |

## Refresh policy

Apps draw into a 1bpp canvas and call `ctx.Invalidate(rect)`. The shell
refreshes that rectangle partially (x/width widened to 8 px). **Design rule:
use partial refresh whenever possible**, including page switches (the whole
screen is sent as one partial). A full refresh happens only at boot, after
`Display::partial_limit` partials (default 8, the clock raises it) to clear
ghosting, when a full-screen app returns, or when the app calls
`ctx.RequestFullRefresh()`. Full-screen apps (the
display demo scenes) own the panel while `fullscreen()` is true; the shell
repaints everything when they return.

## Adding an app

1. Subclass `zos::App` (`zos/app.h`): `id`, `title`, `Render`, plus any of
   `OnKey`, `NextTickMs`/`OnTick`, `OnWake`, `hints`.
2. Optionally return `ConfigField`s; the device web page and
   `POST /api/apps/<id>/config` appear automatically, values live in NVS
   namespace `zos_<id>` (`ctx.settings()`).
   A `kSecret` field (API keys) is never sent back to the web page; saving it
   empty keeps the stored value.
3. `shell.Register(std::make_unique<MyApp>())` in `main/app_main.cc` and add
   the source to `main/CMakeLists.txt`.

## Web page

Once Wi-Fi is connected, browse to `http://<device ip>/`. The first view lists
only the apps that declare settings; apps without any are not shown. Opening
one shows its form (`#/app/<id>`). Wi-Fi itself is set up from the 配网 app
(hotspot + captive portal).

## Apps

| App | Notes |
| --- | --- |
| 时钟天气 (CLOCK) | big HH:MM with one partial refresh a minute (full on the hour); current weather and a 4-day forecast shown as icons (Font Awesome bitmaps, `tools/generate_weather_icons.py`) from Open-Meteo (no key), fetched on a worker task only while Wi-Fi is up and the data is older than the interval; city by name (geocoded once), zone, 12/24 h, date and interval are web-configurable; OK refreshes |
| AI 问答 (AICHAT) | Alibaba Bailian Qwen, ported from the youn-ink project. Spoken questions: OK starts the ES8311 microphone (16 kHz mono, up to 20 s, into PSRAM), OK again stops; `qwen3-asr-flash` transcribes, `qwen-flash` answers (non-streaming, whole answer at once). Sessions with remembered context (`apps/ai/ai_chat.*`, hardware-free, shared with the simulator); hold UP = new session, hold DOWN = session list (new / switch / delete). The Bailian API Key is a secret field on the web page, listed there as "AI 问答及绘图" (`App::web_title()`) because AI 水墨 uses the same key |
| AI 水墨 (INKPAINT) | speak a subject (OK starts/stops the microphone, same as AI Q&A): `qwen3-asr-flash` transcribes, `qwen-flash` plans the scene as an ink wash painting prompt, `qwen-image-2.0-pro` paints it (800x476 PNG, ~20 s), the PNG is downloaded, decoded (`apps/ai/png_decode.*`) and converted to 400x238 16-gray 4 bpp (`PngToGray16`: auto-levels, contrast, serpentine Floyd-Steinberg) and shown in the content area with header and footer kept (`Context::ShowContentImage`). While working the page shows the phase. On the picture (footer: UP 重画, DOWN 保存, OK 说话; the footer shows 已保存 afterwards): OK speaks a new subject, UP paints it again, DOWN saves it to the album (no paging on the picture); leave with hold OK, and back on the page UP shows the last picture. Shares the Bailian key of AI Q&A (no settings of its own) |
| 相册 (PHOTOS) | 照片播放. Photos are uploaded from a phone on the device web page `/photos` (linked from the web home), which also has the slideshow interval (1m / 5m / 10m, setting `interval` of this app). The browser crops/fits to 400x300, converts to gray (auto-levels, contrast slider, 16-level Floyd-Steinberg), shows the exact 16-gray result and uploads the packed 4 bpp bytes (60000). On the device: list + preview of the selected photo; OK plays it full screen in 16 grays (`Display::Show4bpp`, no header or footer), the next photo follows by itself after the interval, UP/DOWN previous/next, OK back. Stored in 64 KB slots of the raw `photos` flash partition (10 MB, 160 photos, `apps/photo/photo_store.*`); AI ink paintings (400x238) are saved there centred on white paper |
| TOKEN 看板 (TOKEN) | AI balance and cost (`apps/token/token_board.*`): Alibaba Bailian (BSS OpenAPI: balance, yesterday/today by model; today/this hour are estimated from the balance) and DeepSeek (balance only); UP/DOWN switch provider, OK refreshes; data is fetched on entering when older than the interval (10/30/60 min), never in the background. Progress bars are dithered grays. AccessKey ID/Secret, DeepSeek key and interval on the web page |
| WI-FI SETUP | shows the connection; OK starts the provisioning hotspot |
| E-PAPER DEMO | the original full / partial / 16-gray scenes |

The old hardware self-tests and device-info pages are not ported yet
(`zectrix_self_test` is still in the tree).

## PC simulator

`sim/` builds the real `zos` shell, chrome, display logic and the apps on the
host (ESP-IDF pieces are shimmed in `sim/shim/`), so UI and refresh behaviour
can be checked without a device.

```sh
make -C sim -j8                 # needs g++ and Python 3 with Pillow
python3 sim/web/server.py       # UI on :8000, device web page on :8080
```

- `:8000` shows the 400x300 panel with UP / DOWN / OK buttons (mouse or
  keyboard; hold for the long presses), Wi-Fi and battery controls, and counters
  for full / partial / 16-gray refreshes with the last refresh rectangle.
- `:8080` is the device web page, answering only while the simulated Wi-Fi is
  connected. It uses `zos/web_api.h`, the same JSON code as the firmware.
- The simulated panel rejects what the driver would: partial refresh without a
  1bpp base image (e.g. right after 16-gray), x/width not a multiple of 8,
  out-of-range rectangles. These show up as "driver error" counts.
- Not simulated: refresh latency (the demo shows 0 ms), real Wi-Fi/captive
  portal, NTP/RTC, power use. Settings live in memory and reset on reboot.
- `sim/` reads cJSON from `managed_components/`, which the first firmware build
  downloads.

## Chinese text

UI text is Chinese. `ZectrixCanvas` decodes UTF-8 and draws ASCII from the
8x16 font and everything else from `font/zectrix_cjk_font_16.h`, a 16x16 table
of only the characters the sources use (GNU Unifont, SIL OFL; downloaded on first run). After adding
Chinese text to an app, regenerate it:

```sh
python3 tools/generate_cjk_font.py [--font path/to/unifont.otf]
```

A character missing from the table draws as `?`. Chinese is 16 px wide, so a
400 px line holds about 24 characters.
