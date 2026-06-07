#!/usr/bin/env python3
import argparse
import json
import math
import glob
import threading
import time
from collections import deque
from http import HTTPStatus
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from typing import Deque, Dict, List
from urllib.parse import urlparse

import spidev
import serial


FRAME_BYTES = 32
SYNC = (0xA5, 0x5A)
COMMAND_SYNC = (0xC3, 0x3C)
ADC_FS = 8388607.0


INDEX_HTML = r"""<!doctype html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>ADS131M08 Virtual Scope</title>
  <style>
    :root { color-scheme: dark; --bg:#101418; --panel:#171d22; --line:#34414b; --text:#f3f7fa; --muted:#9fb0bd; --vin:#6aa6ff; --vmot:#39b980; --gate:#e7b84d; --bad:#ee6b63; }
    * { box-sizing: border-box; }
    body { margin:0; font-family:ui-sans-serif,system-ui,"Segoe UI",sans-serif; background:var(--bg); color:var(--text); }
    header { height:64px; display:flex; align-items:center; justify-content:space-between; padding:14px 22px; background:#12181d; border-bottom:1px solid var(--line); }
    h1 { margin:0; font-size:20px; }
    main { max-width:1400px; margin:0 auto; padding:18px; display:grid; gap:14px; }
    section { background:var(--panel); border:1px solid var(--line); border-radius:8px; overflow:hidden; }
    h2 { margin:0; padding:12px 14px; border-bottom:1px solid var(--line); color:var(--muted); font-size:13px; text-transform:uppercase; letter-spacing:.08em; }
    .metrics { padding:14px; display:grid; grid-template-columns:repeat(6,minmax(120px,1fr)); gap:10px; }
    .metric { min-height:74px; padding:11px; background:#12181d; border:1px solid #26313a; border-radius:8px; display:grid; align-content:space-between; }
    .metric span { color:var(--muted); font-size:12px; }
    .metric strong { font-size:22px; overflow-wrap:anywhere; }
    .scope { padding:14px; display:grid; gap:10px; }
    canvas { width:100%; height:430px; background:#0d1216; border:1px solid #26313a; border-radius:8px; }
    .legend { display:flex; gap:14px; color:var(--muted); font-size:13px; flex-wrap:wrap; }
    .legend span::before { content:""; display:inline-block; width:20px; height:3px; margin-right:6px; vertical-align:middle; background:var(--c); }
    .dot { width:10px; height:10px; border-radius:50%; background:var(--bad); display:inline-block; margin-right:8px; }
    .dot.ok { background:#39b980; box-shadow:0 0 12px #39b98088; }
    @media (max-width:900px) { .metrics { grid-template-columns:repeat(2,minmax(120px,1fr)); } }
  </style>
</head>
<body>
  <header><h1>ADS131M08 Virtual Scope</h1><div><span id="dot" class="dot"></span><span id="state">Connecting</span></div></header>
  <main>
    <section>
      <h2>Measurements</h2>
      <div class="metrics">
        <div class="metric"><span>Target Rate</span><strong id="fs">--</strong></div>
        <div class="metric"><span>Actual Rate</span><strong id="actualfs">--</strong></div>
        <div class="metric"><span>Frames</span><strong id="frames">--</strong></div>
        <div class="metric"><span>Dropped</span><strong id="dropped">--</strong></div>
        <div class="metric"><span>Status</span><strong id="status">--</strong></div>
        <div class="metric"><span>VIN RMS</span><strong id="vinrms">--</strong></div>
        <div class="metric"><span>VMOT RMS</span><strong id="vmotrms">--</strong></div>
      </div>
    </section>
    <section>
      <h2>Scope</h2>
      <div class="scope">
        <div class="legend"><span style="--c:var(--vin)">VIN input</span><span style="--c:var(--vmot)">VMOT motor</span><span style="--c:var(--gate)">GATE</span></div>
        <canvas id="scope" width="1200" height="430"></canvas>
      </div>
    </section>
  </main>
  <script>
    function rms(values) {
      if (!values.length) return 0;
      return Math.sqrt(values.reduce((acc, x) => acc + x*x, 0) / values.length);
    }
    function fmt(value, digits=1) {
      const n = Number(value);
      return Number.isFinite(n) ? n.toFixed(digits) : "--";
    }
    function draw(samples) {
      const canvas = document.getElementById("scope");
      const ctx = canvas.getContext("2d");
      ctx.clearRect(0, 0, canvas.width, canvas.height);
      ctx.strokeStyle = "#26313a";
      ctx.lineWidth = 1;
      for (let i = 1; i < 6; i++) {
        const y = i * canvas.height / 6;
        ctx.beginPath(); ctx.moveTo(0, y); ctx.lineTo(canvas.width, y); ctx.stroke();
      }
      const series = [["vin", "#6aa6ff", 220], ["vmot", "#39b980", 220]];
      for (const [key, color, scale] of series) {
        ctx.strokeStyle = color;
        ctx.lineWidth = 2;
        ctx.beginPath();
        let open = false;
        samples.forEach((s, i) => {
          const x = i * canvas.width / Math.max(1, samples.length - 1);
          const y = canvas.height * 0.50 - (s[key] / scale) * canvas.height * 0.38;
          const prev = samples[i - 1];
          const thetaJump = prev ? Math.abs((s.theta || 0) - (prev.theta || 0)) : 0;
          const wrapped = prev && (prev.theta || 0) > 330 && (s.theta || 0) < 30;
          const discontinuity = i === 0 || (thetaJump > 35 && !wrapped);
          if (discontinuity || !open) { ctx.moveTo(x, y); open = true; } else ctx.lineTo(x, y);
        });
        ctx.stroke();
      }
      ctx.strokeStyle = "#e7b84d";
      ctx.lineWidth = 2;
      ctx.beginPath();
      samples.forEach((s, i) => {
        const x = i * canvas.width / Math.max(1, samples.length - 1);
        const y = s.gate > 0.5 ? canvas.height * 0.08 : canvas.height * 0.18;
        if (i === 0) ctx.moveTo(x, y); else ctx.lineTo(x, y);
      });
      ctx.stroke();
    }
    async function poll() {
      try {
        const res = await fetch("/api/samples");
        const data = await res.json();
        document.getElementById("dot").className = data.connected ? "dot ok" : "dot";
        document.getElementById("state").textContent = data.connected ? "SPI online" : (data.error || "Waiting");
        document.getElementById("fs").textContent = `${data.sample_rate} S/s`;
        document.getElementById("actualfs").textContent = `${fmt(data.actual_sample_rate, 0)} S/s`;
        document.getElementById("frames").textContent = data.frames;
        document.getElementById("dropped").textContent = data.dropped;
        document.getElementById("status").textContent = data.status_hex;
        document.getElementById("vinrms").textContent = `${fmt(rms(data.samples.map(s => s.vin)))} V`;
        document.getElementById("vmotrms").textContent = `${fmt(rms(data.samples.map(s => s.vmot)))} V`;
        draw(data.samples);
      } catch (err) {
        document.getElementById("dot").className = "dot";
        document.getElementById("state").textContent = "HTTP error";
      }
    }
    setInterval(poll, 200);
    poll();
  </script>
</body>
</html>
"""


def sign_extend_24(value: int) -> int:
    value &= 0xFFFFFF
    if value & 0x800000:
        value -= 0x1000000
    return value


def read_i24(frame: List[int], offset: int) -> int:
    return sign_extend_24((frame[offset] << 16) | (frame[offset + 1] << 8) | frame[offset + 2])


def checksum16(frame: List[int]) -> int:
    return sum(frame[:30]) & 0xFFFF


def put_u16(frame: List[int], offset: int, value: int) -> None:
    value = max(0, min(0xFFFF, int(value)))
    frame[offset] = (value >> 8) & 0xFF
    frame[offset + 1] = value & 0xFF


class AdsReader:
    def __init__(
        self,
        bus: int,
        device: int,
        sample_rate: int,
        spi_hz: int,
        history: int,
        line_hz: float,
        angle_deg: float,
        gate_deg: float,
        voltage_peak: float,
        current_peak: float,
        dc_bus: float,
        noise: float,
    ) -> None:
        self.bus = bus
        self.device = device
        self.sample_rate = sample_rate
        self.spi_hz = spi_hz
        self.line_hz = line_hz
        self.angle_deg = angle_deg
        self.gate_deg = gate_deg
        self.voltage_peak = voltage_peak
        self.current_peak = current_peak
        self.dc_bus = dc_bus
        self.noise = noise
        self.samples: Deque[Dict[str, float]] = deque(maxlen=history)
        self.rate_times: Deque[float] = deque(maxlen=512)
        self.lock = threading.Lock()
        self.frames = 0
        self.dropped = 0
        self.status = 0
        self.error = ""
        self.last_seen = 0.0
        self.stop_event = threading.Event()
        self.thread = threading.Thread(target=self._loop, daemon=True)
        self.spi = spidev.SpiDev()

    def start(self) -> None:
        self.spi.open(self.bus, self.device)
        self.spi.mode = 0b01
        self.spi.max_speed_hz = self.spi_hz
        self.thread.start()

    def close(self) -> None:
        self.stop_event.set()
        self.thread.join(timeout=1.0)
        self.spi.close()

    def snapshot(self) -> Dict[str, object]:
        with self.lock:
            age = None if self.last_seen == 0.0 else time.time() - self.last_seen
            return {
                "connected": self.last_seen > 0.0 and age is not None and age < 1.0 and not self.error,
                "sample_rate": self.sample_rate,
                "actual_sample_rate": self._actual_rate_locked(),
                "frames": self.frames,
                "dropped": self.dropped,
                "status": self.status,
                "status_hex": f"0x{self.status:04X}",
                "error": self.error,
                "samples": list(self.samples),
            }

    def _actual_rate_locked(self) -> float:
        if len(self.rate_times) < 2:
            return 0.0
        elapsed = self.rate_times[-1] - self.rate_times[0]
        return 0.0 if elapsed <= 0 else (len(self.rate_times) - 1) / elapsed

    def _decode(self, frame: List[int]) -> Dict[str, float] | None:
        if len(frame) != FRAME_BYTES or tuple(frame[:2]) != SYNC:
            self.dropped += 1
            return None
        expected = (frame[30] << 8) | frame[31]
        if checksum16(frame) != expected:
            self.dropped += 1
            return None
        self.status = (frame[4] << 8) | frame[5]
        gate_state = 1.0 if (self.status & 0x0004) else 0.0
        return {
            "vin": read_i24(frame, 6) / ADC_FS * 250.0,
            "vmot": read_i24(frame, 9) / ADC_FS * 250.0,
            "iload": read_i24(frame, 12) / ADC_FS * 25.0,
            "vdc": read_i24(frame, 15) / ADC_FS * 500.0,
            "gate": gate_state,
            "theta": read_i24(frame, 21) / ADC_FS * 180.0 + 180.0,
            "temp": read_i24(frame, 24) / ADC_FS * 150.0,
            "fault": read_i24(frame, 27) / ADC_FS,
        }

    def _loop(self) -> None:
        period = 1.0 / self.sample_rate
        next_time = time.perf_counter()
        command = self._build_command_frame()
        while not self.stop_event.is_set():
            try:
                frame = self.spi.xfer2(command)
                decoded = self._decode(frame)
                if decoded is not None:
                    now = time.perf_counter()
                    with self.lock:
                        self.samples.append(decoded)
                        self.rate_times.append(now)
                        self.frames += 1
                        self.last_seen = time.time()
                        self.error = ""
            except Exception as exc:
                with self.lock:
                    self.error = str(exc)
                time.sleep(0.2)
            next_time += period
            sleep_time = next_time - time.perf_counter()
            if sleep_time > 0:
                time.sleep(sleep_time)
            else:
                next_time = time.perf_counter()

    def _build_command_frame(self) -> List[int]:
        frame = [0] * FRAME_BYTES
        frame[0], frame[1] = COMMAND_SYNC
        frame[2] = 0x01
        put_u16(frame, 4, self.sample_rate)
        put_u16(frame, 6, round(self.line_hz * 100.0))
        put_u16(frame, 8, round(self.angle_deg * 100.0))
        put_u16(frame, 10, round(self.gate_deg * 100.0))
        put_u16(frame, 12, round(self.voltage_peak * 10.0))
        put_u16(frame, 14, round(self.current_peak * 100.0))
        put_u16(frame, 16, round(self.dc_bus * 10.0))
        put_u16(frame, 18, round(self.noise * 100000.0))
        put_u16(frame, 30, checksum16(frame))
        return frame


def make_handler(reader: AdsReader):
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
            elif path == "/api/samples":
                self._send(HTTPStatus.OK, "application/json", json.dumps(reader.snapshot()).encode("utf-8"))
            else:
                self._send(HTTPStatus.NOT_FOUND, "text/plain", b"not found")

    return Handler


def configure_virtual_adc(sample_rate: int, line_hz: float, angle_deg: float, gate_deg: float) -> None:
    ports = sorted(glob.glob("/dev/ttyUSB*") + glob.glob("/dev/ttyACM*"))
    if not ports:
        return
    for port_name in ports:
        try:
            with serial.Serial(port_name, 115200, timeout=0.2) as port:
                time.sleep(0.2)
                for command in (
                    "ENABLE 1",
                    "FAULT 0",
                    f"LINEHZ {line_hz}",
                    f"FS {sample_rate}",
                    f"ANGLE {angle_deg}",
                    f"GATEDEG {gate_deg}",
                    "STATUS",
                ):
                    port.write((command + "\n").encode("ascii"))
                    port.flush()
                    time.sleep(0.03)
            return
        except serial.SerialException:
            continue


def main() -> int:
    parser = argparse.ArgumentParser(description="ADS131M08 virtual SPI scope.")
    parser.add_argument("--spi-bus", type=int, default=0)
    parser.add_argument("--spi-device", type=int, default=0)
    parser.add_argument("--sample-rate", type=int, default=800)
    parser.add_argument("--line-hz", type=float, default=10.0)
    parser.add_argument("--angle-deg", type=float, default=90.0)
    parser.add_argument("--gate-deg", type=float, default=15.0)
    parser.add_argument("--voltage-peak", type=float, default=170.0)
    parser.add_argument("--current-peak", type=float, default=10.0)
    parser.add_argument("--dc-bus", type=float, default=325.0)
    parser.add_argument("--noise", type=float, default=0.001)
    parser.add_argument("--spi-hz", type=int, default=1000000)
    parser.add_argument("--history", type=int, default=600)
    parser.add_argument("--host", default="0.0.0.0")
    parser.add_argument("--http-port", type=int, default=8090)
    args = parser.parse_args()

    configure_virtual_adc(args.sample_rate, args.line_hz, args.angle_deg, args.gate_deg)
    reader = AdsReader(
        args.spi_bus,
        args.spi_device,
        args.sample_rate,
        args.spi_hz,
        args.history,
        args.line_hz,
        args.angle_deg,
        args.gate_deg,
        args.voltage_peak,
        args.current_peak,
        args.dc_bus,
        args.noise,
    )
    reader.start()
    server = ThreadingHTTPServer((args.host, args.http_port), make_handler(reader))
    print(f"Serving http://{args.host}:{args.http_port} SPI{args.spi_bus}.{args.spi_device} {args.sample_rate}S/s", flush=True)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        reader.close()
        server.server_close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
