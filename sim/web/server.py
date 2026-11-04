#!/usr/bin/env python3
"""Local web front-end for the zos PC simulator.

    python3 sim/web/server.py [--port 8000] [--device-port 8080]

Runs one long-lived build/sim_device (the real shell, chrome, display logic and
apps) and serves:
  :8000  simulator UI (screen image, UP/DOWN/OK buttons, Wi-Fi/battery controls)
  :8080  the device's own web page (apps + settings), answering only while the
         simulated Wi-Fi is connected, like the firmware
"""
import argparse
import atexit
import base64
import json
import re
import subprocess
import sys
import threading
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from io import BytesIO
from pathlib import Path
from urllib.parse import parse_qs, quote, urlparse

from PIL import Image

SIM = Path(__file__).resolve().parent.parent
ROOT = SIM.parent
MIC_FILE = "/tmp/zectrix_sim_mic.wav"  # keep in sync with kMicFile in sim_ai_platform.cc
KEYS = {"up", "down", "ok", "okhold", "uphold", "downhold", "downlong"}


def photo_page():
    text = (ROOT / "main/apps/photo/photo_web_ui.h").read_text()
    return re.search(r'R"HTML\((.*)\)HTML"', text, re.S).group(1).encode()


def device_page():
    text = (ROOT / "components/zos/web_ui.h").read_text()
    return re.search(r'R"HTML\((.*)\)HTML"', text, re.S).group(1).encode()


class Device:
    """Owns the sim_device subprocess; commands are serialised."""

    def __init__(self):
        self.lock = threading.Lock()
        self.net_up = True
        self.start()
        atexit.register(self.close)

    def start(self):
        self.proc = subprocess.Popen([str(SIM / "build" / "sim_device"), "--out", "/tmp"],
                                     stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                     text=True, bufsize=1)
        if self.proc.stdout.readline().strip() != "READY":
            raise RuntimeError("sim_device failed to start")
        self.net_up = True

    def raw(self, line):
        with self.lock:
            if self.proc.poll() is not None:
                return "ERR sim_device exited"
            self.proc.stdin.write(line + "\n")
            self.proc.stdin.flush()
            return self.proc.stdout.readline().rstrip("\n")

    def frame(self, line):
        """Run a command that answers FRAME; returns (png, info dict)."""
        reply = self.raw(line)
        if not reply.startswith("FRAME"):
            raise ValueError(reply)
        _, path, full, partial, gray, errors, off, rec, last = reply.split("\t")
        buf = BytesIO()
        Image.open(path).convert("L").save(buf, "PNG")
        return buf.getvalue(), {"X-Full": full, "X-Partial": partial, "X-Gray": gray,
                                "X-Errors": errors, "X-Off": off, "X-Rec": rec, "X-Last": last}

    def close(self):
        if self.proc.poll() is None:
            self.proc.terminate()

    def restart(self):
        self.close()
        self.proc.wait()
        self.start()


DEVICE = None


def send(h, code, body, ctype, extra=None):
    h.send_response(code)
    h.send_header("Content-Type", ctype)
    h.send_header("Content-Length", str(len(body)))
    h.send_header("Cache-Control", "no-store")
    for k, v in (extra or {}).items():
        h.send_header(k, v)
    h.end_headers()
    h.wfile.write(body)


class UiHandler(BaseHTTPRequestHandler):
    def _frame(self, line):
        try:
            png, info = DEVICE.frame(line)
        except ValueError as e:
            return send(self, 400, str(e).encode(), "text/plain")
        send(self, 200, png, "image/png", info)

    def do_GET(self):
        url = urlparse(self.path)
        q = parse_qs(url.query)
        if url.path == "/":
            return send(self, 200, (Path(__file__).parent / "index.html").read_bytes(),
                        "text/html; charset=utf-8")
        if url.path == "/key":
            name = q.get("name", [""])[0]
            if name not in KEYS:
                return send(self, 400, b"unknown key", "text/plain")
            return self._frame("key " + name)
        if url.path == "/screen":
            return self._frame("screen")
        if url.path == "/net":
            up = q.get("state", ["up"])[0] == "up"
            DEVICE.net_up = up
            return self._frame("net " + ("up" if up else "down"))
        if url.path == "/battery":
            pct = int(q.get("percent", ["85"])[0])
            charging = " charging" if q.get("charging", ["0"])[0] == "1" else ""
            return self._frame(f"battery {pct}{charging}")
        if url.path == "/reset":
            DEVICE.restart()
            return self._frame("screen")
        send(self, 404, b"not found", "text/plain")

    def do_POST(self):
        # PC microphone: a mono WAV the page recorded while an app was recording. The simulated recorder picks it
        # up when the recording ends (kMicFile in sim_ai_platform.cc).
        if urlparse(self.path).path != "/voice":
            return send(self, 404, b"not found", "text/plain")
        n = int(self.headers.get("Content-Length", "0"))
        if n <= 0 or n > 8 * 1024 * 1024:
            return send(self, 400, b"bad size", "text/plain")
        Path(MIC_FILE).write_bytes(self.rfile.read(n))
        send(self, 200, b"ok", "text/plain")

    def log_message(self, *args):
        pass


class WebHandler(BaseHTTPRequestHandler):
    """The device web page. Down while Wi-Fi is not connected, like the firmware."""

    def _guard(self):
        if not DEVICE.net_up:
            send(self, 503, b"Wi-Fi not connected", "text/plain")
            return False
        return True

    def do_GET(self):
        if not self._guard():
            return
        if self.path == "/":
            return send(self, 200, device_page(), "text/html")
        if self.path == "/photos":
            return send(self, 200, photo_page(), "text/html")
        if self.path == "/api/photo-settings":
            return send(self, 200, DEVICE.raw("photosettings")[5:].encode(), "application/json")
        if self.path == "/api/photos":
            return send(self, 200, DEVICE.raw("photolist")[5:].encode(), "application/json")
        m = re.fullmatch(r"/api/photos/(\d+)", self.path)
        if m:
            reply = DEVICE.raw("photoget " + m.group(1))
            if not reply.startswith("B64 "):
                return send(self, 404, b"no such photo", "text/plain")
            return send(self, 200, base64.b64decode(reply[4:]), "application/octet-stream")
        if self.path in ("/api/status", "/api/apps"):
            reply = DEVICE.raw(self.path.split("/")[-1])
            return send(self, 200, reply[5:].encode(), "application/json")
        send(self, 404, b"not found", "text/plain")

    def do_DELETE(self):
        m = re.fullmatch(r"/api/photos/(\d+)", self.path)
        if not m or not self._guard():
            return send(self, 404, b"not found", "text/plain")
        _, code, *error = DEVICE.raw("photodel " + m.group(1)).split(" ", 2)
        send(self, int(code), (error[0] if error else "ok").encode() or b"ok", "text/plain")

    def do_POST(self):
        u = urlparse(self.path)
        if u.path == "/api/photo-settings":
            if not self._guard():
                return
            body = self.rfile.read(int(self.headers.get("Content-Length", "0")))
            try:
                payload = json.dumps(json.loads(body))
            except ValueError:
                return send(self, 400, b"bad json", "text/plain")
            _, code, *error = DEVICE.raw(f"photosettings {payload}").split(" ", 2)
            return send(self, int(code), (error[0] if error else "ok").encode() or b"ok", "text/plain")
        if u.path == "/api/photos":
            if not self._guard():
                return
            body = self.rfile.read(int(self.headers.get("Content-Length", "0")))
            title = parse_qs(u.query).get("title", [""])[0]
            reply = DEVICE.raw(f"photoput {quote(title) or '-'} {base64.b64encode(body).decode()}")
            _, code, *error = reply.split(" ", 2)
            return send(self, int(code), (error[0] if error else "ok").encode() or b"ok", "text/plain")
        m = re.fullmatch(r"/api/apps/([a-z0-9_]+)/config", self.path)
        if not m or not self._guard():
            return send(self, 404, b"not found", "text/plain")
        body = self.rfile.read(int(self.headers.get("Content-Length", "0")))
        try:
            payload = json.dumps(json.loads(body))  # one line
        except ValueError:
            return send(self, 400, b"bad json", "text/plain")
        _, code, *error = DEVICE.raw(f"config {m.group(1)} {payload}").split(" ", 2)
        send(self, int(code), (error[0] if error else "ok").encode() or b"ok", "text/plain")

    def log_message(self, *args):
        pass


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", type=int, default=8000)
    ap.add_argument("--device-port", type=int, default=8080)
    ap.add_argument("--host", default="127.0.0.1")
    args = ap.parse_args()
    if not (SIM / "build" / "sim_device").exists():
        sys.exit("sim_device not built. Run: make -C sim")
    global DEVICE
    DEVICE = Device()
    web = ThreadingHTTPServer((args.host, args.device_port), WebHandler)
    threading.Thread(target=web.serve_forever, daemon=True).start()
    print(f"Simulator UI:        http://{args.host}:{args.port}/")
    print(f"Device web page:     http://{args.host}:{args.device_port}/")
    ThreadingHTTPServer((args.host, args.port), UiHandler).serve_forever()


if __name__ == "__main__":
    main()
