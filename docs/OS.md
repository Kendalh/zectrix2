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
| DOWN hold 3 s | power off |

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
3. `shell.Register(std::make_unique<MyApp>())` in `main/app_main.cc` and add
   the source to `main/CMakeLists.txt`.

## Web page

Once Wi-Fi is connected, browse to `http://<device ip>/`. Wi-Fi itself is set
up from the WI-FI SETUP app (hotspot + captive portal).

## Apps

| App | Notes |
| --- | --- |
| CLOCK | HH:MM, partial refresh once a minute, full refresh on the hour; zone, 12/24 h and date are web-configurable; NTP over Wi-Fi, kept in the RTC |
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
