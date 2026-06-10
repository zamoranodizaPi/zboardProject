#!/usr/bin/env python3
"""
Raspberry Pi web HMI for the FakeFPGA synchronous motor controller.

Reads `FPGA key=value` telemetry from the FakeFPGA ESP32 over USB serial and
serves a compact industrial control interface. It intentionally does not render
an oscilloscope; this is an operator/control panel for measurements, phasors,
link health, and motor commands.
"""

from __future__ import annotations

import argparse
import datetime as _dt
import glob
import json
import mimetypes
import math
import os
import pathlib
import queue
import re
import threading
import time
import uuid
import zipfile
from io import BytesIO
from collections import deque
from http import HTTPStatus
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from typing import Dict, List, Optional
from urllib.parse import parse_qs

import serial


TELEMETRY_RE = re.compile(r"([A-Za-z_][A-Za-z0-9_]*)=(-?[0-9.]+)")
STATIC_DIST = pathlib.Path(__file__).resolve().parent / "raspi_hmi_scada" / "dist"
PLANT_TELEMETRY_PATH = pathlib.Path("/tmp/nexus_motor_plant_state.json")
PLANT_COMMAND_FIFO = pathlib.Path("/tmp/nexus_motor_plant_cmd")
FPGA_CONTROL_STATE_PATH = pathlib.Path("/tmp/nexus_fpga_control_state.json")
COMTRADE_DIR = pathlib.Path("/tmp/nexus_comtrade")


def read_plant_state() -> Dict[str, object]:
    try:
        return json.loads(PLANT_TELEMETRY_PATH.read_text(encoding="utf-8"))
    except Exception:
        return {}


def send_plant_command(command: str) -> str:
    command = command.strip()
    if not command:
        return "empty"
    try:
        fd = os.open(str(PLANT_COMMAND_FIFO), os.O_WRONLY | os.O_NONBLOCK)
        try:
            os.write(fd, (command + "\n").encode("ascii", errors="ignore"))
        finally:
            os.close(fd)
        return "OK"
    except Exception as exc:
        return f"ERR plant fifo: {exc}"


class ComtradeRecorder:
    analog_channels = [
        ("VA", "V", "va"),
        ("VB", "V", "vb"),
        ("VC", "V", "vc"),
        ("IA", "A", "ia"),
        ("IB", "A", "ib"),
        ("IC", "A", "ic"),
        ("FREQ", "Hz", "f"),
        ("PF", "pu", "pf"),
        ("P", "W", "p"),
        ("Q", "var", "q"),
        ("SPEED", "%", "speed_pct"),
        ("SLIP", "Hz", "slip_hz"),
        ("FIELD_I", "A", "field_current"),
        ("FIELD_V", "V", "field_voltage"),
        ("DISCH_I", "A", "discharge_current"),
        ("DC_BUS", "V", "dc_bus_voltage"),
        ("RECT_AVG", "V", "rectifier_avg_v"),
        ("RECT_RIPPLE", "V", "rectifier_ripple_v"),
        ("RECT_INST", "V", "rectifier_inst_v"),
    ]
    digital_channels = [
        ("RUN_CMD", "run"),
        ("FIELD_ENABLE", "field_enable"),
        ("RF3_ENABLED", "rf3_enabled"),
        ("G1_CMD", "scrcmd_g1"),
        ("G2_CMD", "scrcmd_g2"),
        ("G3_CMD", "scrcmd_g3"),
        ("G4_CMD", "scrcmd_g4"),
        ("G5_CMD", "scrcmd_g5"),
        ("G6_CMD", "scrcmd_g6"),
        ("G1_EXEC", "g1"),
        ("G2_EXEC", "g2"),
        ("G3_EXEC", "g3"),
        ("G4_EXEC", "g4"),
        ("G5_EXEC", "g5"),
        ("G6_EXEC", "g6"),
        ("BREAKER", "breaker_closed"),
        ("SPEED_OK", "speed_ok"),
        ("FIELD_FB", "field_current_fb"),
        ("DISCH_FB", "discharge_current_fb"),
        ("CROWBAR", "crowbar_active"),
        ("FAULT", "fault"),
        ("PLANT_FAULT", "plant_fault"),
        ("GATE_FAIL_1", "gate_fail_1"),
        ("SCR_OPEN_2", "scr_open_2"),
        ("SCR_SHORT_3", "scr_short_3"),
        ("FWT", "fwt"),
        ("DST", "dst"),
    ]

    def __init__(self, directory: pathlib.Path) -> None:
        self.dir = directory
        self.dir.mkdir(parents=True, exist_ok=True)
        self.lock = threading.Lock()
        self.pretrigger = deque(maxlen=300)
        self.active: Optional[Dict[str, object]] = None
        self.last_ctrl: Optional[int] = None
        self.last_fault = 0
        self.last_run = 0

    def _sample(self, telemetry: Dict[str, float], plant: Dict[str, object]) -> Dict[str, object]:
        merged: Dict[str, object] = dict(telemetry)
        merged.update(plant)
        return {"ts": time.time(), "values": merged}

    def add_sample(self, telemetry: Dict[str, float], plant: Dict[str, object]) -> None:
        sample = self._sample(telemetry, plant)
        with self.lock:
            self.pretrigger.append(sample)
            if self.active:
                self.active["samples"].append(sample)
                if len(self.active["samples"]) >= int(self.active["max_samples"]):
                    self._finish_locked("complete")
            ctrl = int(telemetry.get("ctrl", -1))
            fault = int(telemetry.get("fault", 0))
            run = int(telemetry.get("run", 0))
            if self.last_ctrl is None:
                self.last_ctrl = ctrl
                self.last_fault = fault
                self.last_run = run
                return
            self.last_ctrl = ctrl
            self.last_fault = fault
            self.last_run = run

    def start(self, event: str, source: str = "manual") -> str:
        with self.lock:
            return self.start_locked(event, source)

    def start_locked(self, event: str, source: str) -> str:
        if self.active:
            return str(self.active["id"])
        rec_id = _dt.datetime.now().strftime("%Y%m%d_%H%M%S_") + uuid.uuid4().hex[:6]
        samples = list(self.pretrigger)
        self.active = {
            "id": rec_id,
            "event": event,
            "source": source,
            "start_ts": time.time(),
            "samples": samples,
            "max_samples": len(samples) + 900,
        }
        return rec_id

    def finish(self, reason: str = "manual") -> Optional[str]:
        with self.lock:
            return self._finish_locked(reason)

    def _finish_locked(self, reason: str) -> Optional[str]:
        if not self.active:
            return None
        record = self.active
        self.active = None
        samples = record["samples"]
        if len(samples) < 3:
            return None
        return self._write_record(record, reason)

    def _write_record(self, record: Dict[str, object], reason: str) -> str:
        rec_id = str(record["id"])
        event = str(record["event"])
        samples: List[Dict[str, object]] = record["samples"]  # type: ignore[assignment]
        base = self.dir / rec_id
        start_dt = _dt.datetime.fromtimestamp(float(samples[0]["ts"]))
        trigger_dt = _dt.datetime.fromtimestamp(float(record["start_ts"]))
        sample_count = len(samples)
        duration = max(0.001, float(samples[-1]["ts"]) - float(samples[0]["ts"]))
        sample_rate = max(1.0, (sample_count - 1) / duration)

        cfg_lines = [
            "NEXUS_SYNC,MotorPlant,1999",
            f"{len(self.analog_channels) + len(self.digital_channels)},{len(self.analog_channels)}A,{len(self.digital_channels)}D",
        ]
        for idx, (name, unit, _key) in enumerate(self.analog_channels, start=1):
            cfg_lines.append(f"{idx},{name},,,{unit},1,0,0,-999999,999999,1,1,P")
        for idx, (name, _key) in enumerate(self.digital_channels, start=1):
            cfg_lines.append(f"{idx},{name},,,0")
        cfg_lines += [
            "60",
            "1",
            f"{sample_rate:.3f},{sample_count}",
            start_dt.strftime("%d/%m/%Y,%H:%M:%S.%f"),
            trigger_dt.strftime("%d/%m/%Y,%H:%M:%S.%f"),
            "ASCII",
            "1",
        ]
        (base.with_suffix(".cfg")).write_text("\n".join(cfg_lines) + "\n", encoding="ascii")

        dat_lines = []
        t0 = float(samples[0]["ts"])
        for index, sample in enumerate(samples, start=1):
            values = sample["values"]  # type: ignore[index]
            usec = int((float(sample["ts"]) - t0) * 1_000_000)
            row: List[str] = [str(index), str(usec)]
            for _name, _unit, key in self.analog_channels:
                row.append(str(int(round(float(values.get(key, 0) or 0) * 1000.0))))
            for _name, key in self.digital_channels:
                row.append("1" if int(float(values.get(key, 0) or 0)) != 0 else "0")
            dat_lines.append(",".join(row))
        (base.with_suffix(".dat")).write_text("\n".join(dat_lines) + "\n", encoding="ascii")

        view_samples = []
        for sample in samples:
            values = sample["values"]  # type: ignore[index]
            view_samples.append({
                "t": round(float(sample["ts"]) - t0, 4),
                **{key: float(values.get(key, 0) or 0) for _n, _u, key in self.analog_channels},
                **{key: int(float(values.get(key, 0) or 0)) for _n, key in self.digital_channels},
            })
        meta = {
            "id": rec_id,
            "event": event,
            "reason": reason,
            "start": start_dt.isoformat(),
            "trigger": trigger_dt.isoformat(),
            "samples": sample_count,
            "sample_rate": sample_rate,
            "cfg": f"/comtrade/{rec_id}.cfg",
            "dat": f"/comtrade/{rec_id}.dat",
            "analog": self.analog_channels,
            "digital": self.digital_channels,
        }
        (base.with_suffix(".meta.json")).write_text(json.dumps(meta), encoding="utf-8")
        (base.with_suffix(".json")).write_text(json.dumps({"meta": meta, "samples": view_samples}), encoding="utf-8")
        return rec_id

    def list_records(self) -> List[Dict[str, object]]:
        records: List[Dict[str, object]] = []
        meta_paths = sorted(self.dir.glob("*.meta.json"), reverse=True)
        for path in meta_paths:
            try:
                payload = json.loads(path.read_text(encoding="utf-8"))
                rec_id = str(payload.get("id", ""))
                if (self.dir / f"{rec_id}.cfg").exists() and (self.dir / f"{rec_id}.dat").exists():
                    records.append(payload)
            except Exception:
                pass
        if not records:
            for path in sorted(self.dir.glob("*.json"), reverse=True)[:10]:
                try:
                    payload = json.loads(path.read_text(encoding="utf-8"))
                    meta = payload.get("meta", {})
                    rec_id = str(meta.get("id", ""))
                    if (self.dir / f"{rec_id}.cfg").exists() and (self.dir / f"{rec_id}.dat").exists():
                        records.append(meta)
                except Exception:
                    pass
        with self.lock:
            if self.active:
                records.insert(0, {
                    "id": self.active["id"],
                    "event": self.active["event"],
                    "reason": "recording",
                    "start": _dt.datetime.fromtimestamp(float(self.active["start_ts"])).isoformat(),
                    "samples": len(self.active["samples"]),
                    "sample_rate": 0,
                    "active": True,
                })
        return records[:40]

    def get_record(self, rec_id: str) -> Dict[str, object]:
        safe_id = re.sub(r"[^A-Za-z0-9_-]", "", rec_id)
        path = self.dir / f"{safe_id}.json"
        if not path.exists():
            return {}
        return json.loads(path.read_text(encoding="utf-8"))


class HighRateComtradeSynth:
    analog_channels = [
        ("VA", "V", "va"),
        ("VB", "V", "vb"),
        ("VC", "V", "vc"),
        ("IA", "A", "ia"),
        ("IB", "A", "ib"),
        ("IC", "A", "ic"),
        ("FIELD_V", "V", "field_voltage"),
        ("FIELD_I", "A", "field_current"),
        ("DISCH_I", "A", "discharge_current"),
        ("DC_BUS", "V", "dc_bus_voltage"),
        ("RECT_AVG", "V", "rectifier_avg_v"),
        ("RECT_RIPPLE", "V", "rectifier_ripple_v"),
        ("RECT_INST", "V", "rectifier_inst_v"),
        ("SPEED", "%", "speed_pct"),
        ("SLIP", "Hz", "slip_hz"),
    ]
    digital_channels = [
        ("RUN_CMD", "run"),
        ("FIELD_ENABLE", "field_enable"),
        ("RF3_ENABLED", "rf3_enabled"),
        ("G1_CMD", "scrcmd_g1"),
        ("G2_CMD", "scrcmd_g2"),
        ("G3_CMD", "scrcmd_g3"),
        ("G4_CMD", "scrcmd_g4"),
        ("G5_CMD", "scrcmd_g5"),
        ("G6_CMD", "scrcmd_g6"),
        ("G1_EXEC", "g1"),
        ("G2_EXEC", "g2"),
        ("G3_EXEC", "g3"),
        ("G4_EXEC", "g4"),
        ("G5_EXEC", "g5"),
        ("G6_EXEC", "g6"),
        ("FAULT", "fault"),
        ("CROWBAR", "crowbar_active"),
        ("DST", "dst"),
        ("GATE_FAIL_1", "gate_fail_1"),
        ("SCR_OPEN_2", "scr_open_2"),
        ("SCR_SHORT_3", "scr_short_3"),
    ]

    def __init__(self, directory: pathlib.Path, sample_rate: int = 8000) -> None:
        self.dir = directory
        self.dir.mkdir(parents=True, exist_ok=True)
        self.sample_rate = sample_rate
        self.lock = threading.Lock()
        self.last_trigger: Dict[str, float] = {}

    def trigger(self, event: str, telemetry: Dict[str, float], plant: Dict[str, object]) -> str:
        with self.lock:
            now = time.time()
            if now - self.last_trigger.get(event, 0.0) < 1.0:
                return "debounced"
            self.last_trigger[event] = now
        rec_id = _dt.datetime.now().strftime("%Y%m%d_%H%M%S_") + "HR_" + uuid.uuid4().hex[:6]
        merged: Dict[str, object] = dict(telemetry)
        merged.update(plant)
        thread = threading.Thread(target=self._write_record, args=(rec_id, event, merged, now), daemon=True)
        thread.start()
        return rec_id

    def _write_record(self, rec_id: str, event: str, values: Dict[str, object], trigger_ts: float) -> None:
        happy_path = event == "HAPPY_PATH"
        start_sequence = event.startswith("START")
        pre_s = 1.0 if happy_path else 3.0
        post_s = 29.0 if happy_path else (18.0 if start_sequence else 5.0)
        total_s = pre_s + post_s
        total_samples = int(total_s * self.sample_rate)
        base = self.dir / rec_id
        start_dt = _dt.datetime.fromtimestamp(trigger_ts - pre_s)
        trigger_dt = _dt.datetime.fromtimestamp(trigger_ts)
        freq = float(values.get("f", 60.0) or 60.0)
        if not 45.0 <= freq <= 65.0:
            freq = 60.0
        va_rms = max(1.0, float(values.get("va", 120.0) or 120.0))
        vb_rms = max(1.0, float(values.get("vb", va_rms) or va_rms))
        vc_rms = max(1.0, float(values.get("vc", va_rms) or va_rms))
        ia_rms = max(0.0, float(values.get("ia", 5.0) or 5.0))
        ib_rms = max(0.0, float(values.get("ib", ia_rms) or ia_rms))
        ic_rms = max(0.0, float(values.get("ic", ia_rms) or ia_rms))
        pf_lag = 30.0
        if event.startswith("FAULT"):
            pf_lag = 70.0
        elif event.startswith("STOP"):
            pf_lag = 20.0
        field_i0 = float(values.get("field_current", values.get("fielda", 0.0)) or 0.0)
        field_v0 = float(values.get("field_voltage", values.get("fieldv", 0.0)) or 0.0)
        dc_bus0 = float(values.get("dc_bus_voltage", 0.0) or 0.0)
        speed0 = float(values.get("speed_pct", values.get("speed", 0.0)) or 0.0)
        slip0 = float(values.get("slip_hz", values.get("slip", 60.0)) or 60.0)
        plant_current_scale0 = max(0.0, float(values.get("stator_current_scale", 1.0) or 1.0))

        cfg_lines = [
            "NEXUS_SYNC,HighRateSim,1999",
            f"{len(self.analog_channels) + len(self.digital_channels)},{len(self.analog_channels)}A,{len(self.digital_channels)}D",
        ]
        for idx, (name, unit, _key) in enumerate(self.analog_channels, start=1):
            cfg_lines.append(f"{idx},{name},,,{unit},1,0,0,-999999,999999,1,1,P")
        for idx, (name, _key) in enumerate(self.digital_channels, start=1):
            cfg_lines.append(f"{idx},{name},,,0")
        cfg_lines += [
            "60",
            "1",
            f"{self.sample_rate},{total_samples}",
            start_dt.strftime("%d/%m/%Y,%H:%M:%S.%f"),
            trigger_dt.strftime("%d/%m/%Y,%H:%M:%S.%f"),
            "ASCII",
            "1",
        ]
        base.with_suffix(".cfg").write_text("\n".join(cfg_lines) + "\n", encoding="ascii")

        view_samples: List[Dict[str, float]] = []
        decimation = max(1, self.sample_rate // 250)
        with base.with_suffix(".dat").open("w", encoding="ascii") as dat:
            for idx in range(total_samples):
                t = idx / self.sample_rate - pre_s
                phase = 2.0 * math.pi * freq * t
                event_p = max(0.0, min(1.0, t / post_s))
                if happy_path:
                    tp = max(0.0, t)
                    stop_t = 18.0
                    accel_s = 14.0
                    stop_s = 10.0
                    run = 1 if 0.0 <= tp < stop_t else 0
                    if t < 0.0:
                        speed = 0.0
                    elif tp < accel_s:
                        accel_p = tp / accel_s
                        speed = 100.0 * (1.0 - math.exp(-3.2 * accel_p)) / (1.0 - math.exp(-3.2))
                    elif tp < stop_t:
                        speed = 100.0
                    else:
                        speed = max(0.0, 100.0 * (1.0 - (tp - stop_t) / stop_s))
                    slip = max(0.0, 60.0 * (1.0 - speed / 100.0))
                    if tp < accel_s:
                        current_scale = 1.9 - 0.85 * min(1.0, tp / accel_s)
                    elif tp < stop_t:
                        current_scale = 1.0
                    else:
                        current_scale = max(0.05, 1.0 - (tp - stop_t) / stop_s)
                    field_start_t = 10.5
                    field_on = field_start_t <= tp < stop_t
                    if field_on:
                        field_p = 1.0 - math.exp(-(tp - field_start_t) / 1.8)
                        field_i = max(field_i0, 4.2) * field_p
                        field_v = max(field_v0, 110.0) * min(1.0, field_p * 1.3)
                    elif tp >= stop_t:
                        decay = math.exp(-(tp - stop_t) / 2.4)
                        field_i = max(field_i0, 4.2) * decay
                        field_v = -max(35.0, field_i * 18.0) if field_i > 0.05 else 0.0
                    else:
                        field_i = 0.0
                        field_v = 0.0
                elif start_sequence:
                    tp = max(0.0, t)
                    accel_s = 12.0
                    accel_p = max(0.0, min(1.0, tp / accel_s))
                    speed = 0.0 if t < 0 else min(100.0, 100.0 * (1.0 - (1.0 - accel_p) * (1.0 - accel_p)))
                    slip = max(0.0, 60.0 * (1.0 - speed / 100.0))
                    current_scale = 0.15 if t < 0 else max(1.05, 1.95 - 0.80 * (speed / 100.0))
                    field_start_t = 10.5
                    if tp >= field_start_t:
                        field_p = 1.0 - math.exp(-(tp - field_start_t) / 1.6)
                        field_i = max(field_i0, 4.2) * field_p
                        field_v = max(field_v0, 110.0) * min(1.0, field_p * 1.3)
                    else:
                        field_i = 0.0
                        field_v = 0.0
                    run = 1 if t >= 0 else 0
                elif event.startswith("STOP"):
                    speed = max(0.0, speed0 - max(0.0, t) * 10.0)
                    slip = max(0.0, 60.0 * (1.0 - speed / 100.0))
                    current_scale = 1.0 if t < 0 else max(0.0, 1.0 - event_p)
                    field_i = field_i0 if t < 0 else max(0.0, field_i0 * (1.0 - event_p * 1.5))
                    field_v = field_v0 if t < 0 else max(0.0, field_v0 * (1.0 - event_p * 1.5))
                    run = 1 if t < 0 else 0
                elif event.startswith("FAULT") or "FAULT" in event:
                    speed = speed0 if t < 0 else max(0.0, speed0 - event_p * 24.0)
                    slip = slip0 if t < 0 else max(0.0, 60.0 * (1.0 - speed / 100.0))
                    current_scale = 1.0 if t < 0 else 2.2
                    field_i = field_i0 if t < 0 else max(0.0, field_i0 * (1.0 - event_p))
                    field_v = field_v0 if t < 0 else max(0.0, field_v0 * (1.0 - event_p))
                    run = 1
                else:
                    speed = speed0
                    slip = slip0
                    current_scale = plant_current_scale0 if plant_current_scale0 > 0.0 else 1.0
                    field_i = field_i0
                    field_v = field_v0
                    run = int(float(values.get("run", 0) or 0))
                disch = 0.0
                if happy_path:
                    tp = max(0.0, t)
                    if 0.0 <= tp < 9.5:
                        disch = 4.8 * (1.0 - tp / 9.5)
                    elif tp >= 18.0:
                        disch = max(0.0, field_i * 1.15)
                if start_sequence and 0.0 <= t <= 10.0:
                    disch = max(0.0, 4.8 * (1.0 - t / 10.0))
                if event.startswith("FAULT") and t >= 0:
                    disch = max(disch, field_i0 * 0.8 * (1.0 - event_p))
                va = math.sqrt(2.0) * va_rms * math.sin(phase)
                vb = math.sqrt(2.0) * vb_rms * math.sin(phase - 2.0 * math.pi / 3.0)
                vc = math.sqrt(2.0) * vc_rms * math.sin(phase + 2.0 * math.pi / 3.0)
                ia = math.sqrt(2.0) * ia_rms * current_scale * math.sin(phase - math.radians(pf_lag))
                ib = math.sqrt(2.0) * ib_rms * current_scale * math.sin(phase - 2.0 * math.pi / 3.0 - math.radians(pf_lag))
                ic = math.sqrt(2.0) * ic_rms * current_scale * math.sin(phase + 2.0 * math.pi / 3.0 - math.radians(pf_lag))
                gate_phase = (math.degrees(phase) + 360.0) % 360.0
                gate_bits = [1 if abs(((gate_phase - base_deg + 180.0) % 360.0) - 180.0) < 4.0 and field_i > 0.05 else 0 for base_deg in (78, 138, 198, 258, 318, 18)]
                rf3 = 1 if field_i > 0.05 else 0
                dc_bus = dc_bus0 if rf3 else 0.0
                if rf3 and dc_bus <= 0.01:
                    dc_bus = max(95.0, abs(field_v))
                rect_avg = float(values.get("rectifier_avg_v", dc_bus) or dc_bus) if rf3 else 0.0
                if rf3 and rect_avg <= 0.01:
                    rect_avg = dc_bus * 0.92
                if rf3:
                    ll_peak = max(rect_avg * 1.55, abs(field_v) * 1.25, 120.0)
                    sector_deg = gate_phase % 60.0
                    firing_in_sector = 18.0
                    conduction = 1.0 if sector_deg >= firing_in_sector else 0.18
                    line_pair = max(
                        math.sin(phase),
                        math.sin(phase - math.pi / 3.0),
                        math.sin(phase - 2.0 * math.pi / 3.0),
                        math.sin(phase - math.pi),
                        math.sin(phase - 4.0 * math.pi / 3.0),
                        math.sin(phase - 5.0 * math.pi / 3.0),
                    )
                    raw_rect = max(0.0, ll_peak * line_pair)
                    rect_inst = max(0.0, raw_rect * conduction)
                    rect_ripple = rect_inst - rect_avg
                else:
                    rect_ripple = 0.0
                    rect_inst = 0.0
                fault = 0 if happy_path else (1 if (event.startswith("FAULT") and t >= 0.0) else int(float(values.get("fault", 0) or 0)))
                gate_fail = 0 if happy_path else (1 if "GATE_FAIL" in event else int(float(values.get("gate_fail_1", 0) or 0)))
                scr_open = 0 if happy_path else (1 if "SCR_OPEN" in event else int(float(values.get("scr_open_2", 0) or 0)))
                scr_short = 0 if happy_path else (1 if "SCR_SHORT" in event else int(float(values.get("scr_short_3", 0) or 0)))
                row_values = [va, vb, vc, ia, ib, ic, field_v, field_i, disch, dc_bus, rect_avg, rect_ripple, rect_inst, speed, slip]
                digital_values = [run, rf3, rf3, *gate_bits, *gate_bits, fault, 1 if fault else 0, 1 if disch > 0.2 else 0, gate_fail, scr_open, scr_short]
                dat.write(",".join([str(idx + 1), str(int((t + pre_s) * 1_000_000))] +
                                   [str(int(round(v * 1000.0))) for v in row_values] +
                                   [str(int(v)) for v in digital_values]) + "\n")
                if idx % decimation == 0:
                    view_samples.append({
                        "t": round(t, 5),
                        "va": va,
                        "vb": vb,
                        "vc": vc,
                        "ia": ia,
                        "ib": ib,
                        "ic": ic,
                        "field_voltage": field_v,
                        "field_current": field_i,
                        "discharge_current": disch,
                        "dc_bus_voltage": dc_bus,
                        "rectifier_avg_v": rect_avg,
                        "rectifier_ripple_v": rect_ripple,
                        "rectifier_inst_v": rect_inst,
                        "speed_pct": speed,
                        "slip_hz": slip,
                        "run": run,
                        "field_enable": rf3,
                        "rf3_enabled": rf3,
                        "scrcmd_g1": gate_bits[0],
                        "g1": gate_bits[0],
                        "fault": fault,
                        "gate_fail_1": gate_fail,
                        "scr_open_2": scr_open,
                        "scr_short_3": scr_short,
                    })
        meta = {
            "id": rec_id,
            "event": event,
            "reason": "high_rate_sim",
            "start": start_dt.isoformat(),
            "trigger": trigger_dt.isoformat(),
            "samples": total_samples,
            "sample_rate": self.sample_rate,
            "cfg": f"/comtrade/{rec_id}.cfg",
            "dat": f"/comtrade/{rec_id}.dat",
            "analog": self.analog_channels,
            "digital": self.digital_channels,
            "decimated": len(view_samples),
        }
        base.with_suffix(".meta.json").write_text(json.dumps(meta), encoding="utf-8")
        base.with_suffix(".json").write_text(json.dumps({"meta": meta, "samples": view_samples}), encoding="utf-8")


HTML = r"""<!doctype html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>FakeFPGA Motor Control</title>
  <style>
    :root {
      --bg:#090b0d; --panel:#11161b; --panel2:#151b21; --line:#28323b;
      --text:#e7edf2; --muted:#8a98a5; --ok:#22c55e; --warn:#f59e0b;
      --bad:#ef4444; --cyan:#22d3ee; --yellow:#facc15; --blue:#60a5fa;
    }
    * { box-sizing:border-box; }
    body { margin:0; background:var(--bg); color:var(--text); font:14px/1.35 system-ui,Segoe UI,Roboto,Arial,sans-serif; }
    header { height:58px; display:flex; align-items:center; justify-content:space-between; padding:0 18px; border-bottom:1px solid var(--line); background:#0c1014; }
    h1 { font-size:18px; margin:0; letter-spacing:0; font-weight:700; }
    .status { display:flex; gap:10px; align-items:center; color:var(--muted); }
    .pill { padding:5px 10px; border:1px solid var(--line); border-radius:6px; background:var(--panel); color:var(--muted); font-weight:700; }
    .pill.ok { color:var(--ok); border-color:#1f6f3a; }
    .pill.bad { color:var(--bad); border-color:#7f2525; }
    main { padding:14px; display:grid; grid-template-columns:1.5fr .9fr; gap:14px; }
    .grid { display:grid; gap:12px; }
    .cards { display:grid; grid-template-columns:repeat(4,minmax(0,1fr)); gap:10px; }
    .card, .panel { background:var(--panel); border:1px solid var(--line); border-radius:8px; padding:12px; }
    .label { color:var(--muted); font-size:12px; font-weight:700; text-transform:uppercase; }
    .value { font-size:28px; font-weight:800; margin-top:4px; }
    .unit { color:var(--muted); font-size:13px; margin-left:4px; }
    .panel h2 { margin:0 0 10px; font-size:14px; color:#cbd5df; }
    table { width:100%; border-collapse:collapse; }
    th, td { text-align:right; padding:7px 6px; border-bottom:1px solid #1e2730; }
    th:first-child, td:first-child { text-align:left; }
    th { color:var(--muted); font-size:12px; font-weight:700; }
    td { font-variant-numeric:tabular-nums; }
    .phasors { height:310px; width:100%; background:#080a0c; border:1px solid var(--line); border-radius:8px; }
    .controls { display:grid; gap:10px; }
    .row { display:flex; gap:8px; align-items:center; }
    button { border:1px solid #34424f; background:#17212a; color:var(--text); border-radius:7px; padding:10px 12px; font-weight:800; cursor:pointer; min-width:82px; }
    button.primary { border-color:#1d6a3a; background:#12351f; color:#baf7ce; }
    button.danger { border-color:#7f2525; background:#351515; color:#fecaca; }
    input, select { width:100%; padding:10px; border-radius:7px; border:1px solid #34424f; background:#0d1217; color:var(--text); font:inherit; }
    .log { height:112px; overflow:hidden; white-space:pre-wrap; color:#9aa8b5; font:12px/1.35 ui-monospace,SFMono-Regular,Consolas,monospace; background:#07090b; border:1px solid var(--line); border-radius:8px; padding:8px; }
    .small { font-size:12px; color:var(--muted); }
    .oktxt { color:var(--ok); font-weight:800; }
    .badtxt { color:var(--bad); font-weight:800; }
    .adc-panel { margin:0 14px 14px; background:var(--panel); border:1px solid var(--line); border-radius:8px; padding:12px; }
    .adc-head { display:flex; align-items:center; justify-content:space-between; gap:12px; margin-bottom:10px; }
    .adc-head h2 { margin:0; font-size:14px; color:#cbd5df; }
    .adc-actions { display:flex; gap:8px; align-items:center; flex-wrap:wrap; }
    .adc-table input { padding:8px; text-align:right; min-width:86px; }
    .adc-table td:first-child { font-weight:800; color:#dbe7ef; }
    @media (max-width:900px) { main { grid-template-columns:1fr; } .cards { grid-template-columns:repeat(2,minmax(0,1fr)); } }
  </style>
</head>
<body>
  <header>
    <h1>FakeFPGA Synchronous Motor Control</h1>
    <div class="status">
      <span id="serial" class="pill">SERIAL --</span>
      <span id="link" class="pill">LINK</span>
      <span id="lock" class="pill">PLL</span>
      <span id="fault" class="pill">FAULT</span>
    </div>
  </header>
  <main>
    <section class="grid">
      <div class="cards">
        <div class="card"><div class="label">Frequency</div><div class="value"><span id="f">--</span><span class="unit">Hz</span></div></div>
        <div class="card"><div class="label">Power Factor</div><div class="value"><span id="pf">--</span></div></div>
        <div class="card"><div class="label">Active Power</div><div class="value"><span id="p">--</span><span class="unit">W</span></div></div>
        <div class="card"><div class="label">Reactive Power</div><div class="value"><span id="q">--</span><span class="unit">var</span></div></div>
      </div>
      <div class="panel">
        <h2>Measurements</h2>
        <table>
          <thead><tr><th>Signal</th><th>RMS</th><th>Angle</th></tr></thead>
          <tbody>
            <tr><td>VA</td><td><span id="va">--</span> V</td><td><span id="ava">--</span> deg</td></tr>
            <tr><td>VB</td><td><span id="vb">--</span> V</td><td><span id="avb">--</span> deg</td></tr>
            <tr><td>VC</td><td><span id="vc">--</span> V</td><td><span id="avc">--</span> deg</td></tr>
            <tr><td>IA</td><td><span id="ia">--</span> A</td><td><span id="aia">--</span> deg</td></tr>
            <tr><td>IB</td><td><span id="ib">--</span> A</td><td><span id="aib">--</span> deg</td></tr>
            <tr><td>IC</td><td><span id="ic">--</span> A</td><td><span id="aic">--</span> deg</td></tr>
          </tbody>
        </table>
      </div>
      <div class="panel">
        <h2>Phasors, VA Reference</h2>
        <canvas id="phasors" class="phasors" width="900" height="310"></canvas>
      </div>
    </section>
    <aside class="grid">
      <div class="panel controls">
        <h2>Motor Control</h2>
        <div class="label">Scenario</div>
        <div class="row">
          <select id="scenarioSelect">
            <option value="NORMAL">NORMAL</option>
            <option value="HEAVY_LOAD">HEAVY_LOAD</option>
            <option value="NO_DISCHARGE">NO_DISCHARGE</option>
            <option value="NO_FIELD">NO_FIELD</option>
            <option value="LOW_PF">LOW_PF</option>
            <option value="THERMAL_TRIP">THERMAL_TRIP</option>
            <option value="PULLOUT">PULLOUT</option>
            <option value="GATE_FAIL">GATE_FAIL</option>
            <option value="SCR_OPEN">SCR_OPEN</option>
            <option value="SCR_SHORT">SCR_SHORT</option>
            <option value="FULLV_MISSING">FULLV_MISSING</option>
          </select>
          <button onclick="cmd('SCENARIO '+document.getElementById('scenarioSelect').value)">SET</button>
        </div>
        <div class="row">
          <button class="primary" onclick="cmd('STARTSEQ')">START SEQ</button>
          <button class="danger" onclick="cmd('STOPSEQ')">STOP SEQ</button>
        </div>
        <div class="row">
          <button onclick="cmd('ACK')">ACK</button>
          <button onclick="cmd('RESET')">RESET</button>
        </div>
        <div class="row">
          <button onclick="cmd('FULLV ON')">FULL V ON</button>
          <button onclick="cmd('FULLV OFF')">FULL V OFF</button>
        </div>
        <div class="row">
          <button onclick="cmd('THERMAL ON')">THERMAL OK</button>
          <button class="danger" onclick="cmd('THERMAL OFF')">THERMAL TRIP</button>
        </div>
        <div class="row">
          <button onclick="cmd('AUTO ON')">AUTO FIELD</button>
          <button onclick="cmd('AUTO OFF')">MANUAL</button>
        </div>
        <div class="label">Voltage Setpoint</div>
        <div class="row"><input id="vset" type="number" step="1" value="120"><button onclick="cmd('VSET '+document.getElementById('vset').value)">SET V</button></div>
        <div class="label">Manual Field Duty</div>
        <div class="row"><input id="field" type="number" step="0.01" min="0" max="0.95" value="0.35"><button onclick="cmd('FIELD '+document.getElementById('field').value)">SET</button></div>
        <div class="row">
          <button onclick="cmd('START')">ACQ START</button>
          <button onclick="cmd('STOP')">ACQ STOP</button>
        </div>
        <div class="small" id="response">ready</div>
      </div>
      <div class="panel">
        <h2>Sequence</h2>
        <table><tbody>
          <tr><td>State</td><td id="ctrlName">--</td></tr>
          <tr><td>Scenario</td><td id="scenarioName">--</td></tr>
          <tr><td>Fault Code</td><td id="faultName">--</td></tr>
          <tr><td>Timer</td><td><span id="ctimer">--</span> ms</td></tr>
          <tr><td>Speed</td><td><span id="speed">--</span>%</td></tr>
          <tr><td>Slip</td><td><span id="slip">--</span> Hz</td></tr>
          <tr><td>Load</td><td><span id="load">--</span>%</td></tr>
          <tr><td>Accel Scale</td><td><span id="accels">--</span></td></tr>
          <tr><td>Discharge</td><td><span id="disca">--</span> A</td></tr>
          <tr><td>Field V</td><td><span id="fieldv_proc">--</span> Vdc</td></tr>
          <tr><td>Field A</td><td><span id="fielda">--</span> Adc</td></tr>
        </tbody></table>
      </div>
      <div class="panel">
        <h2>Permissives</h2>
        <table><tbody>
          <tr><td>Voltage</td><td id="p_vok">--</td></tr>
          <tr><td>Frequency</td><td id="p_fok">--</td></tr>
          <tr><td>Balance</td><td id="p_bal">--</td></tr>
          <tr><td>Phase Sequence</td><td id="p_seq">--</td></tr>
          <tr><td>Signal Present</td><td id="p_sig">--</td></tr>
          <tr><td>Power Factor</td><td id="p_pf">--</td></tr>
        </tbody></table>
      </div>
      <div class="panel">
        <h2>Electrical Trips</h2>
        <table><tbody>
          <tr><td>Undervoltage</td><td id="t_uv">--</td></tr>
          <tr><td>Overvoltage</td><td id="t_ov">--</td></tr>
          <tr><td>Underfrequency</td><td id="t_uf">--</td></tr>
          <tr><td>Overfrequency</td><td id="t_of">--</td></tr>
          <tr><td>Phase Loss</td><td id="t_ploss">--</td></tr>
          <tr><td>Voltage Unbalance</td><td id="t_vunb">--</td></tr>
          <tr><td>Low Power Factor</td><td id="t_lpf">--</td></tr>
          <tr><td>Phase Sequence</td><td id="t_pseq">--</td></tr>
          <tr><td>Loss of Signal</td><td id="t_los">--</td></tr>
          <tr><td>Trip Mask</td><td id="tripmask">--</td></tr>
        </tbody></table>
      </div>
      <div class="panel">
        <h2>Nexus Sync I/O</h2>
        <table><tbody>
          <tr><td>FULL VOLTAGE</td><td id="fullv">--</td></tr>
          <tr><td>THERMAL OK</td><td id="thermok">--</td></tr>
          <tr><td>56K OK START</td><td id="r56k">--</td></tr>
          <tr><td>FS FIELD ON</td><td id="fs">--</td></tr>
          <tr><td>FAX SYNC</td><td id="fax">--</td></tr>
          <tr><td>FAL OK/TRIP</td><td id="fal">--</td></tr>
          <tr><td>FWT</td><td id="fwt">--</td></tr>
          <tr><td>DST</td><td id="dst">--</td></tr>
        </tbody></table>
      </div>
      <div class="panel">
        <h2>Link Health</h2>
        <table>
          <tbody>
            <tr><td>Frames</td><td id="seq">--</td></tr>
            <tr><td>FPS</td><td id="fps">--</td></tr>
            <tr><td>Bad</td><td id="bad">--</td></tr>
            <tr><td>Marker</td><td id="mark">--</td></tr>
            <tr><td>Seq Err</td><td id="seqerr">--</td></tr>
            <tr><td>DRDY</td><td id="drdy">--</td></tr>
            <tr><td>Drops</td><td id="drops">--</td></tr>
          </tbody>
        </table>
      </div>
      <div class="panel">
        <h2>Status</h2>
        <table><tbody>
          <tr><td>RUN</td><td id="run">--</td></tr>
          <tr><td>AUTO</td><td id="auto">--</td></tr>
          <tr><td>FIELD</td><td><span id="fieldv">--</span></td></tr>
          <tr><td>Unbalance</td><td><span id="vunb">--</span>%</td></tr>
          <tr><td>ROCOF</td><td><span id="rocof">--</span> Hz/s</td></tr>
        </tbody></table>
      </div>
      <div class="log" id="line"></div>
    </aside>
  </main>
  <section class="adc-panel">
    <div class="adc-head">
      <h2>FakeADS Signal Control</h2>
      <div class="adc-actions">
        <span class="small" id="adsSerial">ADS SERIAL --</span>
        <select id="adsProfile" style="width:190px">
          <option value="MANUAL">MANUAL TABLE</option>
          <option value="GRID_NORMAL">GRID_NORMAL</option>
          <option value="PHASE_LOSS_A">PHASE_LOSS_A</option>
          <option value="PHASE_LOSS_B">PHASE_LOSS_B</option>
          <option value="PHASE_LOSS_C">PHASE_LOSS_C</option>
          <option value="VOLTAGE_SAG">VOLTAGE_SAG</option>
          <option value="UNBALANCE">UNBALANCE</option>
          <option value="LOW_PF">LOW_PF</option>
          <option value="FREQ_STEP">FREQ_STEP</option>
          <option value="START_PROFILE">START_PROFILE</option>
          <option value="PULLOUT">PULLOUT</option>
          <option value="NO_SIGNAL">NO_SIGNAL</option>
        </select>
        <button class="primary" onclick="runAds()">RUN ADS</button>
        <button class="danger" onclick="stopAds()">STOP ADS</button>
      </div>
    </div>
    <table class="adc-table">
      <thead><tr><th>Channel</th><th>Frequency Hz</th><th>Amplitude pu</th><th>Angle deg</th></tr></thead>
      <tbody id="adsRows"></tbody>
    </table>
    <div class="small" id="adsResponse">Edit values locally, then press RUN ADS to apply.</div>
  </section>
  <script>
    const adsDefaults = [
      ["VA",60,0.8,0], ["VB",60,0.8,-120], ["VC",60,0.8,120], ["VAN",60,0.8,0],
      ["IA",60,0.55,-30], ["IB",60,0.55,-150], ["IC",60,0.55,90], ["IN",60,0,0]
    ];
    const ctrlNames = ["IDLE","READY","STARTING","ACCEL","FIELD","VERIFY","RUNNING","FAULT","LOCKOUT"];
    const scenarioNames = ["NORMAL","HEAVY_LOAD","NO_DISCHARGE","NO_FIELD","LOW_PF","THERMAL_TRIP","PULLOUT","FULLV_MISSING","GATE_FAIL","SCR_OPEN","SCR_SHORT"];
    const faultNames = {
      0:"NONE", 1:"NO DISCHARGE", 2:"NO FIELD", 3:"LOW PF", 4:"THERMAL", 5:"INCOMPLETE", 6:"PULLOUT",
      16:"UNDERVOLTAGE", 17:"OVERVOLTAGE", 18:"UNDERFREQ", 19:"OVERFREQ", 20:"PHASE LOSS",
      21:"VOLTAGE UNBALANCE", 22:"PHASE SEQUENCE", 23:"LOSS OF SIGNAL"
    };
    const ids = ["f","pf","p","q","va","vb","vc","ia","ib","ic","ava","avb","avc","aia","aib","aic","seq","fps","bad","mark","seqerr","drdy","drops","run","auto","fieldv","vunb","rocof","ctimer","speed","slip","load","accels","disca","fielda","tripmask","permask"];
    function fmt(v, n=2){ return Number.isFinite(v) ? v.toFixed(n) : "--"; }
    function set(id, value){ const el=document.getElementById(id); if(el) el.textContent=value; }
    function flag(id, value, okText="OK", badText="BAD"){
      const el=document.getElementById(id); if(!el) return;
      const ok = Number(value) === 1;
      el.textContent = ok ? okText : badText;
      el.className = ok ? "oktxt" : "badtxt";
    }
    function trip(id, value){
      const el=document.getElementById(id); if(!el) return;
      const active = Number(value) === 1;
      el.textContent = active ? "TRIP" : "CLEAR";
      el.className = active ? "badtxt" : "oktxt";
    }
    function buildAdsRows(){
      const body=document.getElementById("adsRows");
      body.innerHTML = adsDefaults.map((ch,i)=>`
        <tr>
          <td>${ch[0]}</td>
          <td><input id="ads_f_${i}" type="number" step="0.01" value="${ch[1]}"></td>
          <td><input id="ads_a_${i}" type="number" step="0.01" min="0" max="0.95" value="${ch[2]}"></td>
          <td><input id="ads_p_${i}" type="number" step="0.1" value="${ch[3]}"></td>
        </tr>`).join("");
    }
    async function cmd(command){
      const r = await fetch("/api/command", {method:"POST", headers:{"Content-Type":"application/json"}, body:JSON.stringify({command})});
      const data = await r.json();
      set("response", command + " -> " + (data.response || data.error || "sent"));
    }
      async function adsControl(action){
        const profile = document.getElementById("adsProfile").value;
        const channels = adsDefaults.map((_,i)=>({
          index:i,
          freq:parseFloat(document.getElementById(`ads_f_${i}`).value),
          amp:parseFloat(document.getElementById(`ads_a_${i}`).value),
          phase:parseFloat(document.getElementById(`ads_p_${i}`).value)
        }));
      const r = await fetch("/api/ads-control", {method:"POST", headers:{"Content-Type":"application/json"}, body:JSON.stringify({action, profile, channels})});
      const data = await r.json();
      set("adsResponse", data.response || data.error || "sent");
    }
    function runAds(){ adsControl("run"); }
    function stopAds(){ adsControl("stop"); }
    function pill(id, ok, text){ const el=document.getElementById(id); el.textContent=text; el.className="pill "+(ok?"ok":"bad"); }
    function drawPhasors(t){
      const c=document.getElementById("phasors"), g=c.getContext("2d"), w=c.width, h=c.height;
      g.clearRect(0,0,w,h); g.fillStyle="#080a0c"; g.fillRect(0,0,w,h);
      const cx=w/2, cy=h/2, r=Math.min(w,h)*0.39;
      g.strokeStyle="#24313b"; g.lineWidth=1;
      for(let i=1;i<=3;i++){ g.beginPath(); g.arc(cx,cy,r*i/3,0,Math.PI*2); g.stroke(); }
      g.beginPath(); g.moveTo(cx-r-20,cy); g.lineTo(cx+r+20,cy); g.moveTo(cx,cy-r-20); g.lineTo(cx,cy+r+20); g.stroke();
      const ph=[["VA",t.ava,t.va,"#facc15"],["VB",t.avb,t.vb,"#22d3ee"],["VC",t.avc,t.vc,"#d946ef"],["IA",t.aia,t.ia,"#fb923c"],["IB",t.aib,t.ib,"#60a5fa"],["IC",t.aic,t.ic,"#a78bfa"]];
      for(const [name,deg,mag,color] of ph){
        if(!Number.isFinite(deg)) continue;
        const scale = name[0]==="V" ? Math.min(Math.abs(mag||0)/120,1.15) : Math.min(Math.abs(mag||0)/5,1.15);
        const a=-deg*Math.PI/180, x=cx+Math.cos(a)*r*scale, y=cy+Math.sin(a)*r*scale;
        g.strokeStyle=color; g.fillStyle=color; g.lineWidth=name==="VA"?4:3;
        g.beginPath(); g.moveTo(cx,cy); g.lineTo(x,y); g.stroke();
        g.beginPath(); g.arc(x,y,4,0,Math.PI*2); g.fill();
        g.font="700 13px system-ui"; g.fillText(name, x+7, y-7);
      }
    }
    async function refresh(){
      try{
        const r=await fetch("/api/state"); const d=await r.json(); const t=d.telemetry||{};
        set("serial", d.serial_port || "SERIAL --");
        set("adsSerial", "ADS " + (d.ads_serial_port || "--"));
        pill("link", d.age_ms < 1000, d.age_ms < 1000 ? "LINK OK" : "LINK STALE");
        pill("lock", t.lock===1, t.lock===1 ? "PLL LOCK" : "PLL WAIT");
        pill("fault", t.fault!==1, t.fault===1 ? "FAULT" : "NO FAULT");
        set("f", fmt(t.f,3)); set("pf", fmt(t.pf,3)); set("p", fmt(t.p,1)); set("q", fmt(t.q,1));
        for(const id of ids){ if(t[id]!==undefined) set(id, fmt(t[id], id==="fps"||id==="seq"||id==="bad"||id==="mark"||id==="seqerr"||id==="drdy"||id==="drops"||id==="run"||id==="auto"?0:2)); }
        set("fieldv", fmt(t.field,3)); set("line", d.last_line || "");
        set("fieldv_proc", fmt(t.fieldv,1));
        set("ctrlName", ctrlNames[t.ctrl|0] || "--");
        set("scenarioName", scenarioNames[t.scenario|0] || "--");
        set("faultName", faultNames[t.faultcode|0] || "--");
        for(const id of ["p_vok","p_fok","p_bal","p_seq","p_sig","p_pf"]){ if(t[id]!==undefined) flag(id, t[id]); }
        for(const id of ["t_uv","t_ov","t_uf","t_of","t_ploss","t_vunb","t_lpf","t_pseq","t_los"]){ if(t[id]!==undefined) trip(id, t[id]); }
        for(const id of ["fullv","thermok","r56k","fs","fax","fal","fwt","dst"]){
          if(t[id]!==undefined) set(id, t[id]===1 ? "ON" : "OFF");
        }
        drawPhasors(t);
      }catch(e){ pill("link", false, "WEB ERR"); }
    }
    buildAdsRows(); setInterval(refresh, 250); refresh();
  </script>
</body>
</html>
"""


class SharedState:
    def __init__(self, recorder: ComtradeRecorder, highrate: HighRateComtradeSynth) -> None:
        self.lock = threading.Lock()
        self.telemetry: Dict[str, float] = {}
        self.last_line = ""
        self.last_update = 0.0
        self.serial_port = ""
        self.response = ""
        self.recorder = recorder
        self.highrate = highrate


class SerialWorker:
    def __init__(self, port: str, baud: int, state: SharedState) -> None:
        self.requested_port = port
        self.baud = baud
        self.state = state
        self.ser: Optional[serial.Serial] = None
        self.lock = threading.Lock()
        self.commands: "queue.Queue[tuple[str, threading.Event, Dict[str, str]]]" = queue.Queue(maxsize=128)
        self.stop = threading.Event()

    def auto_port(self) -> str:
        if self.requested_port:
            return self.requested_port
        names = sorted(glob.glob("/dev/ttyUSB*") + glob.glob("/dev/ttyACM*"))
        return names[1] if len(names) > 1 else (names[0] if names else "/dev/ttyUSB1")

    def connect(self) -> None:
        port = self.auto_port()
        ser = serial.Serial()
        ser.port = port
        ser.baudrate = self.baud
        ser.timeout = 0.1
        ser.dtr = False
        ser.rts = False
        ser.open()
        ser.setDTR(False)
        ser.setRTS(False)
        time.sleep(0.2)
        ser.reset_input_buffer()
        with self.lock:
            self.ser = ser
        with self.state.lock:
            self.state.serial_port = port
            self.state.response = "serial connected"

    def close(self) -> None:
        with self.lock:
            if self.ser:
                self.ser.close()
            self.ser = None

    def send(self, command: str, wait_s: float = 1.0) -> str:
        command = command.strip()
        if not command:
            return "empty"
        done = threading.Event()
        result: Dict[str, str] = {"text": "queued"}
        try:
            self.commands.put_nowait((command, done, result))
        except queue.Full:
            return "serial command queue full"
        if wait_s <= 0.0 or not done.wait(wait_s):
            return "queued"
        return result["text"]

    def drain_commands(self) -> None:
        if not self.ser or not self.ser.is_open:
            return
        for _ in range(32):
            try:
                command, done, result = self.commands.get_nowait()
            except queue.Empty:
                return
            try:
                self.ser.write((command + "\n").encode("ascii", errors="ignore"))
                self.ser.flush()
                result["text"] = "OK"
                with self.state.lock:
                    self.state.response = f"sent {command}"
            except Exception as exc:
                result["text"] = f"serial write error: {exc}"
                with self.state.lock:
                    self.state.response = result["text"]
                raise
            finally:
                done.set()
                self.commands.task_done()

    def parse_line(self, line: str) -> None:
        if not line.startswith("FPGA ") or "FPLL" in line:
            return
        values: Dict[str, float] = {}
        for key, value in TELEMETRY_RE.findall(line):
            try:
                values[key] = float(value)
            except ValueError:
                pass
        if not values:
            return
        write_fpga_control_state(values)
        plant = read_plant_state()
        with self.state.lock:
            previous_fault = int(self.state.telemetry.get("fault", 0))
            self.state.telemetry = values
            self.state.last_line = line
            self.state.last_update = time.time()
        self.state.recorder.add_sample(values, plant)
        if int(values.get("fault", 0)) == 1 and previous_fault == 0:
            self.state.highrate.trigger("FAULT", values, plant)

    def run(self) -> None:
        while not self.stop.is_set():
            try:
                if not self.ser or not self.ser.is_open:
                    self.connect()
                assert self.ser is not None
                self.drain_commands()
                line = self.ser.readline().decode("ascii", errors="replace").strip()
                if line:
                    self.parse_line(line)
            except Exception as exc:  # keep the HMI alive across USB resets
                with self.state.lock:
                    self.state.response = f"serial error: {exc}"
                self.close()
                time.sleep(1.0)


class SerialCommandPort:
    def __init__(self, port: str, baud: int) -> None:
        self.port = port
        self.baud = baud
        self.ser: Optional[serial.Serial] = None
        self.lock = threading.Lock()

    def connect(self) -> None:
        if self.ser and self.ser.is_open:
            return
        ser = serial.Serial()
        ser.port = self.port
        ser.baudrate = self.baud
        ser.timeout = 0.15
        ser.dtr = False
        ser.rts = False
        ser.open()
        ser.setDTR(False)
        ser.setRTS(False)
        time.sleep(0.2)
        ser.reset_input_buffer()
        self.ser = ser

    def close(self) -> None:
        if self.ser:
            self.ser.close()
        self.ser = None

    def send(self, command: str, wait: float = 0.06) -> str:
        command = command.strip()
        if not command:
            return "empty"
        with self.lock:
            try:
                self.connect()
                assert self.ser is not None
                self.ser.write((command + "\n").encode("ascii", errors="ignore"))
                self.ser.flush()
                time.sleep(wait)
                data = self.ser.read_all().decode("ascii", errors="replace").strip()
                return data or "OK"
            except Exception as exc:
                self.close()
        return f"ERR {exc}"


def write_fpga_control_state(values: Dict[str, float]) -> None:
    try:
        payload = {
            "ts": time.time(),
            "run": int(float(values.get("run", 0.0)) > 0.5),
            "fs": int(float(values.get("fs", 0.0)) > 0.5),
            "field": float(values.get("field", 0.0) or 0.0),
            "fault": int(float(values.get("fault", 0.0)) > 0.5),
            "ctrl": int(float(values.get("ctrl", 0.0)) if "ctrl" in values else 0),
        }
        tmp = FPGA_CONTROL_STATE_PATH.with_suffix(".json.tmp")
        tmp.write_text(json.dumps(payload), encoding="utf-8")
        tmp.replace(FPGA_CONTROL_STATE_PATH)
    except Exception:
        pass


def make_handler(state: SharedState, worker: SerialWorker, ads_port: SerialCommandPort):
    class Handler(BaseHTTPRequestHandler):
        def log_message(self, fmt: str, *args) -> None:
            return

        def send_body(self, status: HTTPStatus, content_type: str, body: bytes) -> None:
            try:
                self.send_response(status)
                self.send_header("Content-Type", content_type)
                self.send_header("Content-Length", str(len(body)))
                self.end_headers()
                self.wfile.write(body)
            except (BrokenPipeError, ConnectionResetError):
                return

        def do_GET(self) -> None:
            if self.path.startswith("/api/state"):
                plant = read_plant_state()
                with state.lock:
                    telemetry = dict(state.telemetry)
                    last_line = state.last_line
                    last_update = state.last_update
                    serial_port = state.serial_port
                    response = state.response
                numeric_map = {
                    "plant_state_id": "plant_state_id",
                    "speed_pct": "speed_pct",
                    "slip_hz": "slip_hz",
                    "load_pct": "load_pct",
                    "stator_current_scale": "stator_current_scale",
                    "motor_torque_pu": "motor_torque_pu",
                    "load_torque_pu": "load_torque_pu",
                    "field_current": "field_current",
                    "field_voltage": "field_voltage",
                    "discharge_current": "discharge_current",
                    "pullout_risk": "pullout_risk",
                    "field_enable": "field_enable",
                    "field_pwm_seen": "field_pwm_seen",
                    "rectifier_ready": "rectifier_ready",
                    "rf3_enabled": "rf3_enabled",
                    "pf_reg_disabled": "pf_reg_disabled",
                    "scr_angle_deg": "scr_angle_deg",
                    "scr_firing_deg": "scr_firing_deg",
                    "scr_gate_width_deg": "scr_gate_width_deg",
                    "scr_active_pair": "scr_active_pair",
                    "dc_bus_voltage": "dc_bus_voltage",
                    "rectifier_avg_v": "rectifier_avg_v",
                    "rectifier_ripple_v": "rectifier_ripple_v",
                    "rectifier_inst_v": "rectifier_inst_v",
                    "rectifier_va": "rectifier_va",
                    "rectifier_vb": "rectifier_vb",
                    "rectifier_vc": "rectifier_vc",
                    "comm_overlap_deg": "comm_overlap_deg",
                    "field_voltage_xdcr": "field_voltage_xdcr",
                    "field_current_xdcr": "field_current_xdcr",
                    "discharge_current_xdcr": "discharge_current_xdcr",
                    "pf_signal": "pf_signal",
                    "bleed_active": "bleed_active",
                    "discharge_active": "discharge_active",
                    "crowbar_active": "crowbar_active",
                    "sk10": "sk10",
                    "sk11": "sk11",
                    "sk12": "sk12",
                    "sk13": "sk13",
                    "g1": "g1",
                    "g2": "g2",
                    "g3": "g3",
                    "g4": "g4",
                    "g5": "g5",
                    "g6": "g6",
                    "g1_exec": "g1_exec",
                    "g2_exec": "g2_exec",
                    "g3_exec": "g3_exec",
                    "g4_exec": "g4_exec",
                    "g5_exec": "g5_exec",
                    "g6_exec": "g6_exec",
                    "k1": "k1",
                    "k2": "k2",
                    "k3": "k3",
                    "k4": "k4",
                    "k5": "k5",
                    "k6": "k6",
                    "gate_fail_1": "gate_fail_1",
                    "gate_fail_2": "gate_fail_2",
                    "gate_fail_3": "gate_fail_3",
                    "gate_fail_4": "gate_fail_4",
                    "gate_fail_5": "gate_fail_5",
                    "gate_fail_6": "gate_fail_6",
                    "scr_open_1": "scr_open_1",
                    "scr_open_2": "scr_open_2",
                    "scr_open_3": "scr_open_3",
                    "scr_open_4": "scr_open_4",
                    "scr_open_5": "scr_open_5",
                    "scr_open_6": "scr_open_6",
                    "scr_short_1": "scr_short_1",
                    "scr_short_2": "scr_short_2",
                    "scr_short_3": "scr_short_3",
                    "scr_short_4": "scr_short_4",
                    "scr_short_5": "scr_short_5",
                    "scr_short_6": "scr_short_6",
                }
                for source, target in numeric_map.items():
                    try:
                        telemetry[target] = float(plant[source])
                    except Exception:
                        pass
                age_ms = int((time.time() - last_update) * 1000) if last_update else 999999
                payload = {
                    "telemetry": telemetry,
                    "plant": plant,
                    "last_line": last_line,
                    "age_ms": age_ms,
                    "age_s": age_ms / 1000.0,
                    "connected": age_ms < 1200,
                    "serial_port": serial_port,
                    "ads_serial_port": ads_port.port,
                    "response": response,
                }
                self.send_body(HTTPStatus.OK, "application/json", json.dumps(payload).encode("utf-8"))
                return
            if self.path.startswith("/api/comtrade/list"):
                payload = {"records": state.recorder.list_records()}
                self.send_body(HTTPStatus.OK, "application/json", json.dumps(payload).encode("utf-8"))
                return
            if self.path.startswith("/api/comtrade/record"):
                query = {}
                if "?" in self.path:
                    query = parse_qs(self.path.split("?", 1)[1])
                rec_id = query.get("id", [""])[0]
                payload = state.recorder.get_record(rec_id)
                status = HTTPStatus.OK if payload else HTTPStatus.NOT_FOUND
                self.send_body(status, "application/json", json.dumps(payload or {"error": "not found"}).encode("utf-8"))
                return
            if self.path.startswith("/comtrade/"):
                self.serve_comtrade_file()
                return
            if self.serve_static_app():
                return
            if self.path == "/" or self.path.startswith("/index"):
                self.send_body(HTTPStatus.OK, "text/html; charset=utf-8", HTML.encode("utf-8"))
                return
            self.send_body(HTTPStatus.NOT_FOUND, "text/plain", b"not found")

        def serve_static_app(self) -> bool:
            if not STATIC_DIST.exists():
                return False
            path = self.path.split("?", 1)[0]
            if path == "/":
                target = STATIC_DIST / "index.html"
            elif path.startswith("/assets/") or path.startswith("/branding/"):
                target = STATIC_DIST / path.lstrip("/")
            elif path.startswith("/kiosk") or path.startswith("/scada"):
                target = STATIC_DIST / "index.html"
            else:
                return False
            try:
                resolved = target.resolve()
                if not str(resolved).startswith(str(STATIC_DIST.resolve())) or not resolved.exists():
                    self.send_body(HTTPStatus.NOT_FOUND, "text/plain", b"not found")
                    return True
                content_type = mimetypes.guess_type(str(resolved))[0] or "application/octet-stream"
                self.send_body(HTTPStatus.OK, content_type, resolved.read_bytes())
                return True
            except Exception as exc:
                self.send_body(HTTPStatus.INTERNAL_SERVER_ERROR, "text/plain", str(exc).encode("utf-8"))
                return True

        def serve_comtrade_file(self) -> None:
            name = pathlib.Path(self.path.split("?", 1)[0]).name
            if not re.match(r"^[A-Za-z0-9_-]+\.(cfg|dat|json|zip)$", name):
                self.send_body(HTTPStatus.NOT_FOUND, "text/plain", b"not found")
                return
            if name.endswith(".zip"):
                rec_id = name[:-4]
                cfg = COMTRADE_DIR / f"{rec_id}.cfg"
                dat = COMTRADE_DIR / f"{rec_id}.dat"
                if not cfg.exists() or not dat.exists():
                    self.send_body(HTTPStatus.NOT_FOUND, "text/plain", b"not found")
                    return
                bio = BytesIO()
                with zipfile.ZipFile(bio, "w", compression=zipfile.ZIP_DEFLATED) as zf:
                    zf.write(cfg, cfg.name)
                    zf.write(dat, dat.name)
                body = bio.getvalue()
                self.send_response(HTTPStatus.OK)
                self.send_header("Content-Type", "application/zip")
                self.send_header("Content-Length", str(len(body)))
                self.send_header("Content-Disposition", f'attachment; filename="{name}"')
                self.end_headers()
                try:
                    self.wfile.write(body)
                except (BrokenPipeError, ConnectionResetError):
                    pass
                return
            target = COMTRADE_DIR / name
            if not target.exists():
                self.send_body(HTTPStatus.NOT_FOUND, "text/plain", b"not found")
                return
            ctype = "text/plain" if target.suffix in (".cfg", ".dat") else "application/json"
            self.send_body(HTTPStatus.OK, ctype, target.read_bytes())

        def do_POST(self) -> None:
            raw = self.rfile.read(int(self.headers.get("Content-Length", "0") or "0"))
            if self.path.startswith("/api/ads-control"):
                self.do_ads_control(raw)
                return
            if self.path.startswith("/api/comtrade/trigger"):
                try:
                    payload = json.loads(raw.decode("utf-8"))
                except Exception:
                    payload = {}
                action = str(payload.get("action", "start")).lower()
                event = str(payload.get("event", "MANUAL"))
                if action == "finish":
                    rec_id = state.recorder.finish("manual")
                    response = rec_id or "no active record"
                else:
                    response = state.recorder.start(event, "manual")
                self.send_body(HTTPStatus.OK, "application/json", json.dumps({"response": response}).encode("utf-8"))
                return
            if not self.path.startswith("/api/command"):
                self.send_body(HTTPStatus.NOT_FOUND, "text/plain", b"not found")
                return
            command = ""
            try:
                if self.headers.get("Content-Type", "").startswith("application/json"):
                    payload = json.loads(raw.decode("utf-8"))
                    command = payload.get("command") or payload.get("cmd") or ""
                else:
                    fields = parse_qs(raw.decode("utf-8"))
                    command = (fields.get("command") or fields.get("cmd") or [""])[0]
            except Exception:
                command = ""
            upper = command.strip().upper()
            with state.lock:
                telemetry_snapshot = dict(state.telemetry)
            plant_snapshot = read_plant_state()
            highrate_id = ""
            if upper in ("HAPPY_PATH", "HAPPYPATH"):
                highrate_id = state.highrate.trigger("HAPPY_PATH", telemetry_snapshot, plant_snapshot)
                send_plant_command("SCENARIO NORMAL")
                worker.send("RESET")
                worker.send("ACK")
                response = worker.send("STARTSEQ")

                def finish_happy_path() -> None:
                    time.sleep(18.0)
                    worker.send("STOPSEQ")

                threading.Thread(target=finish_happy_path, daemon=True).start()
                with state.lock:
                    state.response = f"{command} -> {response}"
                self.send_body(HTTPStatus.OK, "application/json", json.dumps({
                    "response": f"HAPPY_PATH STARTED -> {response}",
                    "highrate": highrate_id,
                }).encode("utf-8"))
                return
            elif upper.startswith("SCENARIO ") and ("FAULT" in upper or "PULLOUT" in upper or "NO_" in upper):
                highrate_id = state.highrate.trigger(f"SCENARIO_{upper.split(maxsplit=1)[1]}", telemetry_snapshot, plant_snapshot)

            if upper.startswith("PLANT "):
                plant_command = command.strip()[6:].strip()
                response = send_plant_command(plant_command)
            else:
                response = worker.send(command)
                if upper == "STOPSEQ":
                    plant_stop_response = send_plant_command("STOP")
                    run_off_response = worker.send("RUN OFF")
                    response = (
                        f"STOPSEQ {response}; "
                        f"PLANT STOP {plant_stop_response}; "
                        f"RUN OFF {run_off_response}"
                    )
                if upper == "STARTSEQ":
                    highrate_id = state.highrate.trigger("START_SEQUENCE", telemetry_snapshot, plant_snapshot)
                elif upper == "STOPSEQ":
                    highrate_id = state.highrate.trigger("STOP_SEQUENCE", telemetry_snapshot, plant_snapshot)
            with state.lock:
                state.response = f"{command} -> {response}"
            self.send_body(HTTPStatus.OK, "application/json", json.dumps({"response": response, "highrate": highrate_id}).encode("utf-8"))

        def do_ads_control(self, raw: bytes) -> None:
            try:
                payload = json.loads(raw.decode("utf-8"))
            except Exception:
                payload = {}
            action = str(payload.get("action", "")).lower()
            if action == "stop":
                worker.send("STOP")
                response = ads_port.send("STOP")
                worker.send("RESETSTATS")
                self.send_body(HTTPStatus.OK, "application/json", json.dumps({"response": f"ADS STOP -> {response}"}).encode("utf-8"))
                return
            if action != "run":
                self.send_body(HTTPStatus.BAD_REQUEST, "application/json", json.dumps({"error": "action must be run or stop"}).encode("utf-8"))
                return

            worker.send("STOP")
            worker.send("RESETSTATS")

            commands: List[str] = ["STOP", "BITS 16", "RATE 8000"]
            profile = str(payload.get("profile", "MANUAL")).upper()
            allowed_profiles = {
                "GRID_NORMAL", "PHASE_LOSS_A", "PHASE_LOSS_B", "PHASE_LOSS_C",
                "VOLTAGE_SAG", "UNBALANCE", "LOW_PF", "FREQ_STEP",
                "START_PROFILE", "PULLOUT", "NO_SIGNAL",
            }
            if profile in allowed_profiles:
                commands.append(f"PROFILE {profile}")
            else:
                commands.append("MODE SINE")
                for ch in payload.get("channels", []):
                    try:
                        idx = int(ch["index"])
                        freq = float(ch["freq"])
                        amp = float(ch["amp"])
                        phase = float(ch["phase"])
                    except Exception:
                        continue
                    if 0 <= idx < 8:
                        freq = max(0.01, min(400.0, freq))
                        amp = max(0.0, min(0.95, amp))
                        commands.append(f"CHCFG {idx} {freq:.4f} {amp:.4f} {phase:.4f}")
            commands.append("SYNCSTART")

            responses = []
            for command in commands:
                responses.append(f"{command}: {ads_port.send(command)}")
            time.sleep(0.15)
            worker.send("RESETSTATS")
            worker.send("START")
            time.sleep(0.15)
            worker.send("RESETSTATS")
            self.send_body(HTTPStatus.OK, "application/json", json.dumps({"response": " | ".join(responses[-3:])}).encode("utf-8"))

    return Handler


def main() -> None:
    parser = argparse.ArgumentParser(description="FakeFPGA synchronous motor web HMI")
    parser.add_argument("--serial", default="/dev/ttyUSB1")
    parser.add_argument("--ads-serial", default="/dev/ttyUSB0")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--host", default="0.0.0.0")
    parser.add_argument("--port", type=int, default=8092)
    args = parser.parse_args()

    recorder = ComtradeRecorder(COMTRADE_DIR)
    highrate = HighRateComtradeSynth(COMTRADE_DIR, sample_rate=8000)
    state = SharedState(recorder, highrate)
    worker = SerialWorker(args.serial, args.baud, state)
    ads_port = SerialCommandPort(args.ads_serial, args.baud)
    thread = threading.Thread(target=worker.run, daemon=True)
    thread.start()

    server = ThreadingHTTPServer((args.host, args.port), make_handler(state, worker, ads_port))
    print(f"FakeFPGA web HMI on http://{args.host}:{args.port} serial={args.serial} ads={args.ads_serial}", flush=True)
    try:
        server.serve_forever()
    finally:
        worker.stop.set()
        worker.close()
        ads_port.close()


if __name__ == "__main__":
    main()
