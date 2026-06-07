#!/usr/bin/env python3
"""
Raspberry Pi SPI master + web oscilloscope for the ESP32 ADS131M08 emulator.

Reads frames from the ESP32 over SPI:
  STATUS_WORD, CH0, CH1, CH2, CH3, CH4, CH5, CH6, CH7, optional CRC

Also forwards text commands to the ESP32 over USB serial, using the command API
implemented by esp32_ads131m08_spi_slave_emulator.ino.
"""

from __future__ import annotations

import argparse
import glob
import json
import math
import threading
import time
from collections import deque
from http import HTTPStatus
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from typing import Deque, Dict, List, Optional
from urllib.parse import urlparse

import serial
import spidev

try:
    import RPi.GPIO as GPIO
except Exception:  # Allows desktop syntax checks.
    GPIO = None


ADC24_FULL_SCALE = 8388607.0
NUM_CHANNELS = 8


INDEX_HTML = r"""<!doctype html>
<html lang="es">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>ESP32 ADS131M08 Scope</title>
  <style>
    :root {
      color-scheme: dark;
      --bg:#0f1215; --panel:#171c20; --panel2:#11161a; --line:#303a42;
      --text:#edf2f5; --muted:#9cabB6; --blue:#64a6ff; --green:#42c587;
      --yellow:#e3ba4f; --red:#ef6a63; --cyan:#55d4d8; --mag:#d083ff;
    }
    * { box-sizing:border-box; }
    body { margin:0; background:var(--bg); color:var(--text); font-family:ui-sans-serif,system-ui,"Segoe UI",sans-serif; }
    header { min-height:58px; display:flex; align-items:center; justify-content:space-between; gap:14px; padding:12px 18px; border-bottom:1px solid var(--line); background:#12171b; }
    h1 { margin:0; font-size:18px; font-weight:700; }
    main { max-width:1440px; margin:0 auto; padding:14px; display:grid; gap:12px; }
    section { border:1px solid var(--line); background:var(--panel); border-radius:8px; overflow:hidden; }
    h2 { margin:0; padding:10px 12px; color:var(--muted); border-bottom:1px solid var(--line); font-size:12px; text-transform:uppercase; }
    .metrics { display:grid; grid-template-columns:repeat(6,minmax(110px,1fr)); gap:8px; padding:10px; }
    .metric { min-height:66px; display:grid; align-content:space-between; padding:9px; border:1px solid #263039; border-radius:7px; background:var(--panel2); }
    .metric span { color:var(--muted); font-size:12px; }
    .metric strong { font-size:19px; overflow-wrap:anywhere; }
    .scope-wrap { padding:10px; display:grid; gap:8px; }
    canvas { width:100%; height:450px; border:1px solid #263039; border-radius:7px; background:#0b1014; }
    .legend { display:flex; gap:12px; flex-wrap:wrap; color:var(--muted); font-size:12px; }
    .legend label { display:flex; align-items:center; gap:6px; cursor:pointer; user-select:none; }
    .swatch { width:18px; height:3px; background:var(--c); display:inline-block; }
    .controls { padding:10px; display:grid; grid-template-columns:repeat(6,minmax(120px,1fr)); gap:8px; align-items:end; }
    .field { display:grid; gap:5px; }
    .field label { color:var(--muted); font-size:12px; }
    select,input,button { min-height:34px; border:1px solid #34414a; border-radius:7px; background:#10161b; color:var(--text); padding:0 10px; font:inherit; }
    button { cursor:pointer; background:#1b2730; }
    button.primary { background:#1f5f46; border-color:#2b7f5e; }
    button.stop { background:#5d2424; border-color:#853131; }
    .dot { width:10px; height:10px; border-radius:50%; background:var(--red); display:inline-block; margin-right:8px; }
    .dot.ok { background:var(--green); box-shadow:0 0 12px #42c58788; }
    @media (max-width:980px) { .metrics,.controls { grid-template-columns:repeat(2,minmax(120px,1fr)); } canvas { height:360px; } }
  </style>
</head>
<body>
  <header>
    <h1>ESP32 ADS131M08 Scope</h1>
    <div><span id="dot" class="dot"></span><span id="state">Conectando</span></div>
  </header>
  <main>
    <section>
      <h2>Mediciones</h2>
      <div class="metrics">
        <div class="metric"><span>SPI FPS</span><strong id="fps">--</strong></div>
        <div class="metric"><span>Frames</span><strong id="frames">--</strong></div>
        <div class="metric"><span>Errores</span><strong id="errors">--</strong></div>
        <div class="metric"><span>Status</span><strong id="status">--</strong></div>
        <div class="metric"><span>Bits</span><strong id="bits">--</strong></div>
        <div class="metric"><span>Serial</span><strong id="serial">--</strong></div>
      </div>
    </section>

    <section>
      <h2>Osciloscopio</h2>
      <div class="scope-wrap">
        <div class="legend" id="legend"></div>
        <canvas id="scope" width="1280" height="450"></canvas>
      </div>
    </section>

    <section>
      <h2>Control ESP32</h2>
      <div class="controls">
        <div class="field"><label>Bits</label><select id="wordBits"><option>24</option><option>16</option><option>32</option></select></div>
        <div class="field"><label>Rate SPS</label><select id="rate"><option>4000</option><option>8000</option><option>16000</option><option>32000</option><option>1000</option></select></div>
        <div class="field"><label>SPI Hz</label><select id="spiHz"><option>10000000</option><option>5000000</option><option>1000000</option></select></div>
        <div class="field"><label>Modo</label><select id="mode"><option>SINE</option><option>COUNTER</option><option>TRIANGLE</option><option>RANDOM</option><option>CONSTANT</option></select></div>
        <button class="primary" onclick="applyConfig()">Aplicar</button>
        <button class="stop" onclick="sendCommand('STOP')">STOP</button>
        <div class="field"><label>Canal</label><select id="channel"><option>0</option><option>1</option><option>2</option><option>3</option><option>4</option><option>5</option><option>6</option><option>7</option></select></div>
        <div class="field"><label>Modo canal</label><select id="chMode"><option>SINE</option><option>COUNTER</option><option>TRIANGLE</option><option>RANDOM</option><option>CONSTANT</option></select></div>
        <div class="field"><label>Valor/Freq</label><input id="chValue" value="60"></div>
        <button onclick="applyChannel()">CH</button>
        <button onclick="sendCommand('START')">START</button>
        <button onclick="sendCommand('CONFIG')">CONFIG</button>
      </div>
    </section>
  </main>
  <script>
    const colors = ["#64a6ff","#42c587","#e3ba4f","#ef6a63","#55d4d8","#d083ff","#f58b4c","#a4d66d"];
    let enabled = new Array(8).fill(true);
    let last = null;

    function makeLegend() {
      const root = document.getElementById("legend");
      root.innerHTML = "";
      for (let i = 0; i < 8; i++) {
        const label = document.createElement("label");
        label.innerHTML = `<input type="checkbox" checked onchange="enabled[${i}]=this.checked"><span class="swatch" style="--c:${colors[i]}"></span>CH${i}`;
        root.appendChild(label);
      }
    }

    function fmt(n, digits=0) {
      n = Number(n);
      return Number.isFinite(n) ? n.toFixed(digits) : "--";
    }

    function draw(samples) {
      const canvas = document.getElementById("scope");
      const ctx = canvas.getContext("2d");
      ctx.clearRect(0, 0, canvas.width, canvas.height);
      ctx.strokeStyle = "#263039";
      ctx.lineWidth = 1;
      for (let i = 1; i < 8; i++) {
        const y = i * canvas.height / 8;
        ctx.beginPath(); ctx.moveTo(0, y); ctx.lineTo(canvas.width, y); ctx.stroke();
      }
      for (let ch = 0; ch < 8; ch++) {
        if (!enabled[ch]) continue;
        ctx.strokeStyle = colors[ch];
        ctx.lineWidth = ch < 2 ? 2 : 1.4;
        ctx.beginPath();
        samples.forEach((s, i) => {
          const x = i * canvas.width / Math.max(1, samples.length - 1);
          const y = canvas.height * 0.5 - s.channels[ch] * canvas.height * 0.44;
          if (i === 0) ctx.moveTo(x, y); else ctx.lineTo(x, y);
        });
        ctx.stroke();
      }
    }

    async function sendCommand(command) {
      await fetch("/api/command", {method:"POST", headers:{"Content-Type":"application/json"}, body:JSON.stringify({command})});
      await poll();
    }

    async function applyConfig() {
      const commands = [
        `BITS ${document.getElementById("wordBits").value}`,
        `RATE ${document.getElementById("rate").value}`,
        `SPI ${document.getElementById("spiHz").value}`,
        `MODE ${document.getElementById("mode").value}`,
        "START"
      ];
      await fetch("/api/config", {method:"POST", headers:{"Content-Type":"application/json"}, body:JSON.stringify({commands})});
      await poll();
    }

    async function applyChannel() {
      const ch = document.getElementById("channel").value;
      const mode = document.getElementById("chMode").value;
      const value = document.getElementById("chValue").value;
      await sendCommand(`CH ${ch} ${mode} ${value}`);
    }

    async function poll() {
      try {
        const res = await fetch("/api/state");
        const data = await res.json();
        last = data;
        document.getElementById("dot").className = data.connected ? "dot ok" : "dot";
        document.getElementById("state").textContent = data.connected ? "SPI online" : (data.error || "Sin frames");
        document.getElementById("fps").textContent = fmt(data.actual_fps);
        document.getElementById("frames").textContent = data.frames;
        document.getElementById("errors").textContent = data.errors;
        document.getElementById("status").textContent = data.status_hex;
        document.getElementById("bits").textContent = data.word_bits;
        document.getElementById("serial").textContent = data.serial_port || "--";
        draw(data.samples);
      } catch (err) {
        document.getElementById("dot").className = "dot";
        document.getElementById("state").textContent = "HTTP error";
      }
    }
    makeLegend();
    setInterval(poll, 150);
    poll();
  </script>
</body>
</html>
"""


def sign_extend(value: int, bits: int) -> int:
    sign_bit = 1 << (bits - 1)
    mask = (1 << bits) - 1
    value &= mask
    return value - (1 << bits) if value & sign_bit else value


def crc16_ccitt(data: bytes) -> int:
    crc = 0xFFFF
    for byte in data:
        crc ^= byte << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) & 0xFFFF if crc & 0x8000 else (crc << 1) & 0xFFFF
    return crc


def decode_word(frame: bytes, offset: int, word_bits: int, signed: bool) -> int:
    word_bytes = word_bits // 8
    raw = int.from_bytes(frame[offset : offset + word_bytes], "big", signed=False)
    if word_bits == 16:
        value = sign_extend(raw, 16) << 8 if signed else raw
    elif word_bits == 24:
        value = sign_extend(raw, 24) if signed else raw
    else:
        value = sign_extend(raw, 32) // 256 if signed else raw
    return value


class SerialBridge:
    def __init__(self, port: str, baud: int) -> None:
        self.requested_port = port
        self.baud = baud
        self.port_name = ""
        self.lock = threading.Lock()
        self.serial: Optional[serial.Serial] = None
        self.last_response = ""

    def connect(self) -> bool:
        with self.lock:
            if self.serial and self.serial.is_open:
                return True
            candidates = [self.requested_port] if self.requested_port else []
            candidates += sorted(glob.glob("/dev/ttyUSB*") + glob.glob("/dev/ttyACM*"))
            for name in candidates:
                if not name:
                    continue
                try:
                    self.serial = serial.Serial(name, self.baud, timeout=0.15)
                    self.port_name = name
                    time.sleep(0.25)
                    return True
                except serial.SerialException:
                    self.serial = None
            self.port_name = ""
            return False

    def send(self, command: str) -> str:
        command = command.strip()
        if not command:
            return ""
        if not self.connect():
            self.last_response = "serial unavailable"
            return self.last_response
        with self.lock:
            assert self.serial is not None
            self.serial.write((command + "\n").encode("ascii", errors="ignore"))
            self.serial.flush()
            time.sleep(0.04)
            data = self.serial.read_all().decode("ascii", errors="replace").strip()
            self.last_response = data or "sent"
            return self.last_response


class Ads131Scope:
    def __init__(
        self,
        spi_bus: int,
        spi_device: int,
        spi_hz: int,
        spi_mode: int,
        word_bits: int,
        sample_rate: int,
        enable_crc: bool,
        history: int,
        drdy_gpio: int,
        serial_bridge: SerialBridge,
    ) -> None:
        self.spi_bus = spi_bus
        self.spi_device = spi_device
        self.spi_hz = spi_hz
        self.spi_mode = spi_mode
        self.word_bits = word_bits
        self.sample_rate = sample_rate
        self.enable_crc = enable_crc
        self.history: Deque[Dict[str, object]] = deque(maxlen=history)
        self.rate_times: Deque[float] = deque(maxlen=1000)
        self.drdy_gpio = drdy_gpio
        self.serial = serial_bridge
        self.lock = threading.Lock()
        self.stop_event = threading.Event()
        self.thread = threading.Thread(target=self._loop, daemon=True)
        self.spi = spidev.SpiDev()
        self.frames = 0
        self.errors = 0
        self.status = 0
        self.error = ""
        self.last_seen = 0.0
        self.gpio_ready = False

    def start(self) -> None:
        self.spi.open(self.spi_bus, self.spi_device)
        self.spi.mode = self.spi_mode
        self.spi.max_speed_hz = self.spi_hz
        self._setup_gpio()
        self.thread.start()

    def close(self) -> None:
        self.stop_event.set()
        self.thread.join(timeout=1.0)
        try:
            self.spi.close()
        finally:
            if GPIO and self.gpio_ready:
                GPIO.cleanup(self.drdy_gpio)

    def _setup_gpio(self) -> None:
        if self.drdy_gpio < 0 or GPIO is None:
            return
        GPIO.setmode(GPIO.BCM)
        GPIO.setup(self.drdy_gpio, GPIO.IN, pull_up_down=GPIO.PUD_UP)
        self.gpio_ready = True

    def frame_bytes(self) -> int:
        return (1 + NUM_CHANNELS + (1 if self.enable_crc else 0)) * (self.word_bits // 8)

    def configure_from_command(self, command: str) -> None:
        parts = command.strip().split()
        if len(parts) >= 2:
            key = parts[0].upper()
            value = parts[1].upper()
            with self.lock:
                if key == "BITS" and value in ("16", "24", "32"):
                    self.word_bits = int(value)
                elif key == "RATE":
                    self.sample_rate = max(1, int(float(value)))
                elif key == "SPI":
                    self.spi_hz = max(1, int(float(value)))
                    self.spi.max_speed_hz = self.spi_hz
                elif key == "SPIMODE" and value in ("0", "1", "2", "3"):
                    self.spi_mode = int(value)
                    self.spi.mode = self.spi_mode
                elif key == "CRC":
                    self.enable_crc = value in ("ON", "TRUE", "1")

    def snapshot(self) -> Dict[str, object]:
        with self.lock:
            age = time.time() - self.last_seen if self.last_seen else 999.0
            actual = self._actual_fps_locked()
            return {
                "connected": age < 1.0 and not self.error,
                "frames": self.frames,
                "errors": self.errors,
                "actual_fps": actual,
                "status": self.status,
                "status_hex": f"0x{self.status:06X}",
                "word_bits": self.word_bits,
                "sample_rate": self.sample_rate,
                "spi_hz": self.spi_hz,
                "spi_mode": self.spi_mode,
                "enable_crc": self.enable_crc,
                "frame_bytes": self.frame_bytes(),
                "serial_port": self.serial.port_name,
                "serial_response": self.serial.last_response,
                "error": self.error,
                "samples": list(self.history),
            }

    def _actual_fps_locked(self) -> float:
        if len(self.rate_times) < 2:
            return 0.0
        elapsed = self.rate_times[-1] - self.rate_times[0]
        return 0.0 if elapsed <= 0 else (len(self.rate_times) - 1) / elapsed

    def _wait_for_drdy(self) -> None:
        if self.gpio_ready and GPIO is not None:
            GPIO.wait_for_edge(self.drdy_gpio, GPIO.FALLING, timeout=100)
        else:
            time.sleep(1.0 / max(1, self.sample_rate))

    def _decode(self, frame: bytes) -> Optional[Dict[str, object]]:
        with self.lock:
            word_bits = self.word_bits
            crc_enabled = self.enable_crc
        word_bytes = word_bits // 8
        expected_len = (1 + NUM_CHANNELS + (1 if crc_enabled else 0)) * word_bytes
        if len(frame) != expected_len:
            self.errors += 1
            return None
        if crc_enabled:
            payload = frame[:-word_bytes]
            crc_word = decode_word(frame, len(payload), word_bits, signed=False) & 0xFFFF
            if crc16_ccitt(payload) != crc_word:
                self.errors += 1
                return None

        status = decode_word(frame, 0, word_bits, signed=False)
        channels: List[int] = []
        for ch in range(NUM_CHANNELS):
            offset = (ch + 1) * word_bytes
            channels.append(decode_word(frame, offset, word_bits, signed=True))
        normalized = [max(-1.0, min(1.0, value / ADC24_FULL_SCALE)) for value in channels]
        return {"t": time.time(), "status": status, "channels": normalized, "raw": channels}

    def _loop(self) -> None:
        while not self.stop_event.is_set():
            try:
                self._wait_for_drdy()
                with self.lock:
                    nbytes = self.frame_bytes()
                frame = bytes(self.spi.xfer2([0] * nbytes))
                decoded = self._decode(frame)
                if decoded:
                    now = time.perf_counter()
                    with self.lock:
                        self.status = int(decoded["status"])
                        self.history.append(decoded)
                        self.rate_times.append(now)
                        self.frames += 1
                        self.last_seen = time.time()
                        self.error = ""
            except Exception as exc:
                with self.lock:
                    self.errors += 1
                    self.error = str(exc)
                time.sleep(0.1)


def make_handler(scope: Ads131Scope):
    class Handler(BaseHTTPRequestHandler):
        def log_message(self, fmt: str, *args: object) -> None:
            return

        def _send(self, status: HTTPStatus, content_type: str, body: bytes) -> None:
            self.send_response(status.value)
            self.send_header("Content-Type", content_type)
            self.send_header("Cache-Control", "no-store")
            self.end_headers()
            self.wfile.write(body)

        def _read_json(self) -> Dict[str, object]:
            length = int(self.headers.get("Content-Length", "0"))
            if length <= 0:
                return {}
            return json.loads(self.rfile.read(length).decode("utf-8"))

        def do_GET(self) -> None:
            path = urlparse(self.path).path
            if path == "/":
                self._send(HTTPStatus.OK, "text/html; charset=utf-8", INDEX_HTML.encode("utf-8"))
            elif path == "/api/state":
                self._send(HTTPStatus.OK, "application/json", json.dumps(scope.snapshot()).encode("utf-8"))
            else:
                self._send(HTTPStatus.NOT_FOUND, "text/plain", b"not found")

        def do_POST(self) -> None:
            path = urlparse(self.path).path
            try:
                data = self._read_json()
                commands = []
                if path == "/api/command":
                    commands = [str(data.get("command", ""))]
                elif path == "/api/config":
                    commands = [str(cmd) for cmd in data.get("commands", [])]
                else:
                    self._send(HTTPStatus.NOT_FOUND, "text/plain", b"not found")
                    return
                responses = []
                for command in commands:
                    if command.strip():
                        responses.append({"command": command, "response": scope.serial.send(command)})
                        scope.configure_from_command(command)
                self._send(HTTPStatus.OK, "application/json", json.dumps({"ok": True, "responses": responses}).encode("utf-8"))
            except Exception as exc:
                self._send(HTTPStatus.BAD_REQUEST, "application/json", json.dumps({"ok": False, "error": str(exc)}).encode("utf-8"))

    return Handler


def main() -> int:
    parser = argparse.ArgumentParser(description="Raspberry Pi master scope for ESP32 ADS131M08 emulator.")
    parser.add_argument("--spi-bus", type=int, default=0)
    parser.add_argument("--spi-device", type=int, default=0)
    parser.add_argument("--spi-hz", type=int, default=10_000_000)
    parser.add_argument("--spi-mode", type=int, default=0, choices=(0, 1, 2, 3))
    parser.add_argument("--word-bits", type=int, default=24, choices=(16, 24, 32))
    parser.add_argument("--sample-rate", type=int, default=4000)
    parser.add_argument("--crc", action="store_true")
    parser.add_argument("--history", type=int, default=900)
    parser.add_argument("--drdy-gpio", type=int, default=4, help="BCM GPIO. Use -1 to poll by sample-rate instead.")
    parser.add_argument("--serial-port", default="", help="Example: /dev/ttyUSB0. Empty = auto-detect.")
    parser.add_argument("--serial-baud", type=int, default=115200)
    parser.add_argument("--host", default="0.0.0.0")
    parser.add_argument("--http-port", type=int, default=8091)
    args = parser.parse_args()

    serial_bridge = SerialBridge(args.serial_port, args.serial_baud)
    scope = Ads131Scope(
        args.spi_bus,
        args.spi_device,
        args.spi_hz,
        args.spi_mode,
        args.word_bits,
        args.sample_rate,
        args.crc,
        args.history,
        args.drdy_gpio,
        serial_bridge,
    )

    serial_bridge.connect()
    for command in (
        f"BITS {args.word_bits}",
        f"RATE {args.sample_rate}",
        f"SPI {args.spi_hz}",
        f"SPIMODE {args.spi_mode}",
        f"CRC {'ON' if args.crc else 'OFF'}",
        "START",
    ):
        serial_bridge.send(command)

    scope.start()
    server = ThreadingHTTPServer((args.host, args.http_port), make_handler(scope))
    print(
        f"Serving http://{args.host}:{args.http_port} "
        f"SPI{args.spi_bus}.{args.spi_device} mode={args.spi_mode} hz={args.spi_hz} bits={args.word_bits}",
        flush=True,
    )
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()
        scope.close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
