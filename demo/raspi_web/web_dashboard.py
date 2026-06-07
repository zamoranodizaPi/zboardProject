#!/usr/bin/env python3
import argparse
import json
import threading
import time
from http import HTTPStatus
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from typing import Dict
from urllib.parse import urlparse

import serial


INDEX_HTML = r"""<!doctype html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>ZBoard Motor Lab</title>
  <style>
    :root {
      color-scheme: dark;
      --bg: #101418;
      --panel: #171d22;
      --panel-2: #1f272e;
      --line: #34414b;
      --text: #f3f7fa;
      --muted: #9fb0bd;
      --accent: #39b980;
      --warn: #e7b84d;
      --bad: #ee6b63;
      --blue: #6aa6ff;
    }
    * { box-sizing: border-box; }
    body {
      margin: 0;
      font-family: ui-sans-serif, system-ui, -apple-system, BlinkMacSystemFont, "Segoe UI", sans-serif;
      background: var(--bg);
      color: var(--text);
    }
    header {
      min-height: 64px;
      display: flex;
      align-items: center;
      justify-content: space-between;
      gap: 16px;
      padding: 14px 22px;
      border-bottom: 1px solid var(--line);
      background: #12181d;
    }
    h1 {
      margin: 0;
      font-size: 20px;
      font-weight: 650;
      letter-spacing: 0;
    }
    .status {
      display: flex;
      align-items: center;
      gap: 10px;
      color: var(--muted);
      font-size: 14px;
      white-space: nowrap;
    }
    .dot {
      width: 10px;
      height: 10px;
      border-radius: 50%;
      background: var(--bad);
      box-shadow: 0 0 12px color-mix(in srgb, var(--bad), transparent 45%);
    }
    .dot.ok {
      background: var(--accent);
      box-shadow: 0 0 12px color-mix(in srgb, var(--accent), transparent 45%);
    }
    main {
      max-width: 1400px;
      margin: 0 auto;
      padding: 18px;
      display: grid;
      grid-template-columns: minmax(300px, 380px) 1fr;
      gap: 16px;
    }
    section {
      background: var(--panel);
      border: 1px solid var(--line);
      border-radius: 8px;
      overflow: hidden;
    }
    section h2 {
      margin: 0;
      padding: 12px 14px;
      font-size: 13px;
      text-transform: uppercase;
      color: var(--muted);
      border-bottom: 1px solid var(--line);
      letter-spacing: .08em;
    }
    .controls {
      display: grid;
      gap: 12px;
      padding: 14px;
    }
    .control {
      display: grid;
      grid-template-columns: 1fr 92px;
      gap: 10px;
      align-items: center;
    }
    label {
      color: var(--muted);
      font-size: 13px;
    }
    input[type="number"] {
      width: 92px;
      height: 34px;
      border-radius: 6px;
      border: 1px solid var(--line);
      background: #0f1418;
      color: var(--text);
      padding: 0 8px;
    }
    input[type="range"] {
      width: 100%;
      accent-color: var(--accent);
    }
    .toggles {
      display: grid;
      grid-template-columns: 1fr 1fr;
      gap: 10px;
    }
    button {
      height: 38px;
      border: 1px solid var(--line);
      border-radius: 6px;
      background: var(--panel-2);
      color: var(--text);
      font-weight: 650;
      cursor: pointer;
    }
    button:hover { border-color: var(--blue); }
    button.primary {
      background: color-mix(in srgb, var(--accent), #0f1418 68%);
      border-color: color-mix(in srgb, var(--accent), #ffffff 10%);
    }
    button.danger {
      background: color-mix(in srgb, var(--bad), #0f1418 70%);
      border-color: color-mix(in srgb, var(--bad), #ffffff 10%);
    }
    .grid {
      display: grid;
      grid-template-columns: repeat(4, minmax(120px, 1fr));
      gap: 10px;
      padding: 14px;
    }
    .metric {
      min-height: 78px;
      padding: 11px;
      border-radius: 8px;
      background: #12181d;
      border: 1px solid #26313a;
      display: grid;
      align-content: space-between;
    }
    .metric span {
      font-size: 12px;
      color: var(--muted);
    }
    .metric strong {
      font-size: 23px;
      font-weight: 700;
      overflow-wrap: anywhere;
    }
    .metric.small strong { font-size: 17px; }
    .strip {
      padding: 14px;
      display: grid;
      gap: 10px;
    }
    .legend {
      display: flex;
      flex-wrap: wrap;
      gap: 12px;
      color: var(--muted);
      font-size: 12px;
    }
    .legend span::before {
      content: "";
      display: inline-block;
      width: 18px;
      height: 3px;
      margin-right: 6px;
      vertical-align: middle;
      background: var(--c);
    }
    canvas {
      width: 100%;
      height: 210px;
      background: #0d1216;
      border: 1px solid #26313a;
      border-radius: 8px;
    }
    .log {
      height: 120px;
      overflow: auto;
      padding: 10px 14px;
      background: #0d1216;
      color: var(--muted);
      font: 12px ui-monospace, SFMono-Regular, Consolas, monospace;
      border-top: 1px solid var(--line);
      white-space: pre-wrap;
    }
    @media (max-width: 900px) {
      main { grid-template-columns: 1fr; padding: 12px; }
      .grid { grid-template-columns: repeat(2, minmax(120px, 1fr)); }
      header { align-items: flex-start; flex-direction: column; }
    }
  </style>
</head>
<body>
  <header>
    <h1>ZBoard Motor Lab</h1>
    <div class="status"><span id="dot" class="dot"></span><span id="link">Connecting</span><span id="age"></span></div>
  </header>
  <main>
    <section>
      <h2>Configuration</h2>
      <div class="controls">
        <div class="toggles">
          <button id="enable" class="primary">Enable</button>
          <button id="fault" class="danger">Fault</button>
        </div>
        <div class="control"><label>Speed ref deg/s</label><input id="speed" type="number" value="60" step="1"></div>
        <input id="speedRange" type="range" min="-720" max="720" value="60" step="1">
        <div class="control"><label>Trigger angle deg</label><input id="angle" type="number" value="90" min="0" max="180" step="1"></div>
        <input id="angleRange" type="range" min="0" max="180" value="90" step="1">
        <div class="control"><label>Line frequency Hz</label><input id="linehz" type="number" value="1" step="0.1"></div>
        <div class="control"><label>Voltage peak V</label><input id="vpeak" type="number" value="170" step="1"></div>
        <div class="control"><label>Current peak A</label><input id="ipeak" type="number" value="10" step="0.1"></div>
        <div class="control"><label>DC bus V</label><input id="vdcSet" type="number" value="325" step="1"></div>
        <div class="control"><label>PF angle deg</label><input id="pf" type="number" value="25" step="1"></div>
        <div class="control"><label>Noise fraction</label><input id="noise" type="number" value="0.002" step="0.001"></div>
        <button id="apply" class="primary">Apply Parameters</button>
      </div>
      <div id="events" class="log"></div>
    </section>
    <section>
      <h2>Measurements</h2>
      <div class="grid">
        <div class="metric"><span>Theta</span><strong id="theta">--</strong></div>
        <div class="metric"><span>Speed</span><strong id="speedLive">--</strong></div>
        <div class="metric"><span>Set Angle</span><strong id="set">--</strong></div>
        <div class="metric small"><span>State</span><strong id="state">--</strong></div>
        <div class="metric"><span>VA</span><strong id="va">--</strong></div>
        <div class="metric"><span>VB</span><strong id="vb">--</strong></div>
        <div class="metric"><span>VC</span><strong id="vc">--</strong></div>
        <div class="metric"><span>VDC</span><strong id="vdc">--</strong></div>
        <div class="metric"><span>VMA</span><strong id="vma">--</strong></div>
        <div class="metric"><span>VMB</span><strong id="vmb">--</strong></div>
        <div class="metric"><span>VMC</span><strong id="vmc">--</strong></div>
        <div class="metric"><span>Gate</span><strong id="gateLive">--</strong></div>
        <div class="metric"><span>IA</span><strong id="ia">--</strong></div>
        <div class="metric"><span>IB</span><strong id="ib">--</strong></div>
        <div class="metric"><span>IC</span><strong id="ic">--</strong></div>
        <div class="metric"><span>Temp</span><strong id="temp">--</strong></div>
      </div>
      <div class="strip">
        <div class="legend">
          <span style="--c:#6aa6ff">VA input</span>
          <span style="--c:#39b980">VMA motor</span>
          <span style="--c:#e7b84d">Gate pulse</span>
        </div>
        <canvas id="wave" width="900" height="260"></canvas>
        <div class="metric small"><span>Raw ADS131M08 codes</span><strong id="raw">--</strong></div>
      </div>
    </section>
  </main>
  <script>
    const history = { va: [], vma: [], gate: [] };
    const maxPoints = 240;
    const ids = ["theta","speedLive","set","state","va","vb","vc","vdc","ia","ib","ic","temp","raw"];

    function num(v, digits=2) {
      const n = Number(v);
      return Number.isFinite(n) ? n.toFixed(digits) : "--";
    }
    function log(line) {
      const el = document.getElementById("events");
      el.textContent = `${new Date().toLocaleTimeString()} ${line}\n` + el.textContent;
    }
    async function command(cmd) {
      const res = await fetch("/api/command", {
        method: "POST",
        headers: {"Content-Type": "application/json"},
        body: JSON.stringify({command: cmd})
      });
      const data = await res.json();
      log(data.ok ? `> ${cmd}` : `ERR ${data.error || cmd}`);
    }
    function syncPair(a, b) {
      const x = document.getElementById(a);
      const y = document.getElementById(b);
      x.addEventListener("input", () => y.value = x.value);
      y.addEventListener("input", () => x.value = y.value);
    }
    syncPair("speed", "speedRange");
    syncPair("angle", "angleRange");
    document.getElementById("enable").onclick = () => command("ENABLE 1");
    document.getElementById("fault").onclick = () => command("FAULT 1");
    document.getElementById("apply").onclick = async () => {
      const commands = [
        `SPEED ${document.getElementById("speed").value}`,
        `ANGLE ${document.getElementById("angle").value}`,
        `LINEHZ ${document.getElementById("linehz").value}`,
        `VPEAK ${document.getElementById("vpeak").value}`,
        `IPEAK ${document.getElementById("ipeak").value}`,
        `VDC ${document.getElementById("vdcSet").value}`,
        `PFDEG ${document.getElementById("pf").value}`,
        `NOISE ${document.getElementById("noise").value}`,
        "ENABLE 1"
      ];
      for (const c of commands) await command(c);
    };
    document.getElementById("fault").addEventListener("dblclick", () => command("FAULT 0"));

    function draw() {
      const canvas = document.getElementById("wave");
      const ctx = canvas.getContext("2d");
      ctx.clearRect(0, 0, canvas.width, canvas.height);
      ctx.strokeStyle = "#26313a";
      ctx.lineWidth = 1;
      for (let i = 1; i < 4; i++) {
        const y = i * canvas.height / 4;
        ctx.beginPath(); ctx.moveTo(0, y); ctx.lineTo(canvas.width, y); ctx.stroke();
      }
      const series = [
        ["va", "#6aa6ff", 220],
        ["vma", "#39b980", 220]
      ];
      for (const [key, color, scale] of series) {
        ctx.strokeStyle = color;
        ctx.lineWidth = 2;
        ctx.beginPath();
        history[key].forEach((value, index) => {
          const x = index * canvas.width / Math.max(1, maxPoints - 1);
          const y = canvas.height / 2 - (value / scale) * (canvas.height * 0.42);
          if (index === 0) ctx.moveTo(x, y); else ctx.lineTo(x, y);
        });
        ctx.stroke();
      }
      ctx.strokeStyle = "#e7b84d";
      ctx.lineWidth = 2;
      ctx.beginPath();
      history.gate.forEach((value, index) => {
        const x = index * canvas.width / Math.max(1, maxPoints - 1);
        const y = value > 0 ? canvas.height * 0.08 : canvas.height * 0.18;
        if (index === 0) ctx.moveTo(x, y); else ctx.lineTo(x, y);
      });
      ctx.stroke();
    }
    function pushHistory(fields) {
      for (const key of Object.keys(history)) {
        const value = Number(fields[key]);
        if (Number.isFinite(value)) {
          history[key].push(value);
          if (history[key].length > maxPoints) history[key].shift();
        }
      }
      draw();
    }
    async function poll() {
      try {
        const res = await fetch("/api/status");
        const data = await res.json();
        const ok = data.connected && data.age_s < 2.5;
        document.getElementById("dot").className = ok ? "dot ok" : "dot";
        document.getElementById("link").textContent = ok ? "ESP32 online" : "Waiting for telemetry";
        document.getElementById("age").textContent = data.age_s == null ? "" : `${data.age_s.toFixed(1)}s`;
        const f = data.fields || {};
        document.getElementById("theta").textContent = `${num(f.theta)} deg`;
        document.getElementById("speedLive").textContent = `${num(f.speed)} deg/s`;
        document.getElementById("set").textContent = `${num(f.set)} deg`;
        document.getElementById("state").textContent = `EN ${f.en || "-"} FLT ${f.fault || "-"} H ${f.hall || "---"}`;
        document.getElementById("va").textContent = `${num(f.va)} V`;
        document.getElementById("vb").textContent = `${num(f.vb)} V`;
        document.getElementById("vc").textContent = `${num(f.vc)} V`;
        document.getElementById("vdc").textContent = `${num(f.vdc)} V`;
        document.getElementById("vma").textContent = `${num(f.vma)} V`;
        document.getElementById("vmb").textContent = `${num(f.vmb)} V`;
        document.getElementById("vmc").textContent = `${num(f.vmc)} V`;
        document.getElementById("gateLive").textContent = f.gate || "--";
        document.getElementById("ia").textContent = `${num(f.ia, 3)} A`;
        document.getElementById("ib").textContent = `${num(f.ib, 3)} A`;
        document.getElementById("ic").textContent = `${num(f.ic, 3)} A`;
        document.getElementById("temp").textContent = `${num(f.temp)} C`;
        document.getElementById("raw").textContent =
          [0,1,2,3,4,5,6,7].map(i => f[`adc${i}`] || "--").join("  ");
        pushHistory(f);
      } catch (err) {
        document.getElementById("dot").className = "dot";
        document.getElementById("link").textContent = "HTTP error";
      }
    }
    setInterval(poll, 50);
    poll();
  </script>
</body>
</html>
"""


def parse_tel(line: str) -> Dict[str, str]:
    fields: Dict[str, str] = {}
    for token in line.strip().split()[1:]:
        if "=" in token:
            key, value = token.split("=", 1)
            fields[key] = value
    return fields


class SerialBridge:
    def __init__(self, port_name: str, baud: int) -> None:
        self.port_name = port_name
        self.baud = baud
        self.lock = threading.Lock()
        self.fields: Dict[str, str] = {}
        self.last_line = ""
        self.last_seen = 0.0
        self.error = ""
        self.serial_port = serial.Serial(port_name, baud, timeout=0.2)
        self.stop_event = threading.Event()
        self.thread = threading.Thread(target=self._read_loop, daemon=True)

    def start(self) -> None:
        self.thread.start()
        for command in ("STREAM 20", "LINEHZ 1", "ENABLE 1", "SPEED 60", "ANGLE 90", "STATUS"):
            self.send(command)
            time.sleep(0.05)

    def close(self) -> None:
        self.stop_event.set()
        try:
            self.send("ENABLE 0")
        except serial.SerialException:
            pass
        self.serial_port.close()

    def send(self, command: str) -> None:
        clean = command.strip().upper()
        if not clean:
            return
        self.serial_port.write((clean + "\n").encode("ascii"))
        self.serial_port.flush()

    def snapshot(self) -> Dict[str, object]:
        with self.lock:
            age = None if self.last_seen == 0.0 else time.time() - self.last_seen
            return {
                "connected": self.last_seen > 0.0 and age is not None and age < 5.0,
                "age_s": age,
                "fields": dict(self.fields),
                "last_line": self.last_line,
                "error": self.error,
            }

    def _read_loop(self) -> None:
        while not self.stop_event.is_set():
            try:
                raw = self.serial_port.readline()
            except serial.SerialException as exc:
                with self.lock:
                    self.error = str(exc)
                time.sleep(0.5)
                continue
            if not raw:
                continue
            line = raw.decode("ascii", errors="replace").strip()
            if line.startswith("TEL "):
                with self.lock:
                    self.fields = parse_tel(line)
                    self.last_line = line
                    self.last_seen = time.time()


def make_handler(bridge: SerialBridge):
    class Handler(BaseHTTPRequestHandler):
        def log_message(self, format: str, *args) -> None:
            return

        def _send(self, status: HTTPStatus, content_type: str, body: bytes) -> None:
            self.send_response(status.value)
            self.send_header("Content-Type", content_type)
            self.send_header("Cache-Control", "no-store")
            self.end_headers()
            self.wfile.write(body)

        def do_GET(self) -> None:
            path = urlparse(self.path).path
            if path == "/":
                self._send(HTTPStatus.OK, "text/html; charset=utf-8", INDEX_HTML.encode("utf-8"))
            elif path == "/api/status":
                body = json.dumps(bridge.snapshot()).encode("utf-8")
                self._send(HTTPStatus.OK, "application/json", body)
            else:
                self._send(HTTPStatus.NOT_FOUND, "text/plain; charset=utf-8", b"not found")

        def do_POST(self) -> None:
            path = urlparse(self.path).path
            if path != "/api/command":
                self._send(HTTPStatus.NOT_FOUND, "application/json", b'{"ok": false}')
                return
            length = int(self.headers.get("Content-Length", "0"))
            payload = self.rfile.read(length).decode("utf-8")
            try:
                data = json.loads(payload)
                command = str(data.get("command", "")).strip()
                bridge.send(command)
                body = json.dumps({"ok": True, "command": command}).encode("utf-8")
                self._send(HTTPStatus.OK, "application/json", body)
            except (json.JSONDecodeError, serial.SerialException) as exc:
                body = json.dumps({"ok": False, "error": str(exc)}).encode("utf-8")
                self._send(HTTPStatus.BAD_REQUEST, "application/json", body)

    return Handler


def main() -> int:
    parser = argparse.ArgumentParser(description="Web dashboard para ESP32 angle simulator.")
    parser.add_argument("--port", default="/dev/ttyUSB0")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--host", default="0.0.0.0")
    parser.add_argument("--http-port", type=int, default=8080)
    args = parser.parse_args()

    bridge = SerialBridge(args.port, args.baud)
    bridge.start()
    server = ThreadingHTTPServer((args.host, args.http_port), make_handler(bridge))
    print(f"Serving http://{args.host}:{args.http_port} using {args.port}", flush=True)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        bridge.close()
        server.server_close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
