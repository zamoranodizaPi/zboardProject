#!/usr/bin/env python3
"""
Nexus Sync hardware-in-the-loop smoke/regression runner.

Runs against the Raspberry web backend, which in turn drives:
  Raspberry plant simulator <-> FakeFPGA ESP32 <-> FakeADS ESP32

No third-party Python packages are required. The runner intentionally checks
observable system behavior instead of implementation details.
"""

from __future__ import annotations

import argparse
import json
import time
import urllib.parse
import urllib.request
from dataclasses import dataclass, field
from pathlib import Path
from typing import Callable, Dict, List, Optional, Tuple


FAULT_NONE = 0
FAULT_NO_DISCHARGE_CURRENT = 1
FAULT_NO_FIELD_CURRENT = 2
FAULT_LOW_POWER_FACTOR = 3
FAULT_THERMAL = 4
FAULT_INCOMPLETE_SEQUENCE = 5
FAULT_PULLOUT = 6


def now_iso() -> str:
    return time.strftime("%Y-%m-%dT%H:%M:%S%z")


class NexusApi:
    def __init__(self, base_url: str, timeout_s: float = 3.0) -> None:
        self.base_url = base_url.rstrip("/")
        self.timeout_s = timeout_s

    def command(self, command: str) -> Dict[str, object]:
        body = urllib.parse.urlencode({"cmd": command}).encode("ascii")
        req = urllib.request.Request(
            f"{self.base_url}/api/command",
            data=body,
            headers={"Content-Type": "application/x-www-form-urlencoded"},
            method="POST",
        )
        with urllib.request.urlopen(req, timeout=self.timeout_s) as resp:
            return json.loads(resp.read().decode("utf-8"))

    def state(self) -> Dict[str, object]:
        with urllib.request.urlopen(f"{self.base_url}/api/state", timeout=self.timeout_s) as resp:
            return json.loads(resp.read().decode("utf-8"))


@dataclass
class Sample:
    t: float
    telemetry: Dict[str, float]
    plant: Dict[str, object]


@dataclass
class TestResult:
    name: str
    passed: bool
    message: str
    duration_s: float
    samples: List[Sample] = field(default_factory=list)
    commands: List[Tuple[str, Dict[str, object]]] = field(default_factory=list)

    def to_json(self) -> Dict[str, object]:
        return {
            "name": self.name,
            "passed": self.passed,
            "message": self.message,
            "duration_s": round(self.duration_s, 3),
            "commands": [{"cmd": c, "response": r} for c, r in self.commands],
            "last_sample": sample_to_json(self.samples[-1]) if self.samples else None,
            "samples": [sample_to_json(s) for s in self.samples],
        }


def as_float(value: object, default: float = 0.0) -> float:
    try:
        return float(value)
    except Exception:
        return default


def sample_to_json(sample: Sample) -> Dict[str, object]:
    t = sample.telemetry
    p = sample.plant
    keys = [
        "ctrl", "fault", "faultcode", "run", "bad", "drops", "seqerr", "f", "pf",
        "speed_pct", "field_current", "field_voltage", "discharge_current",
        "scrcmd_en", "scrcmd_g1", "scrcmd_g2", "scrcmd_g3", "scrcmd_g4", "scrcmd_g5", "scrcmd_g6",
    ]
    return {
        "t": round(sample.t, 3),
        "telemetry": {k: t.get(k) for k in keys if k in t},
        "plant": {
            "plant_state": p.get("plant_state"),
            "plant_scenario": p.get("plant_scenario"),
            "speed_pct": p.get("speed_pct"),
            "field_current": p.get("field_current"),
            "field_voltage": p.get("field_voltage"),
            "discharge_current": p.get("discharge_current"),
            "thermal_ok": p.get("thermal_ok"),
            "plant_fault": p.get("plant_fault"),
        },
    }


class HilRunner:
    def __init__(self, api: NexusApi, poll_s: float = 0.5) -> None:
        self.api = api
        self.poll_s = poll_s

    def send(self, result: TestResult, command: str, wait_s: float = 0.25) -> None:
        response = self.api.command(command)
        result.commands.append((command, response))
        time.sleep(wait_s)

    def capture(self) -> Sample:
        payload = self.api.state()
        telemetry = payload.get("telemetry", {})
        plant = payload.get("plant", {})
        return Sample(time.monotonic(), telemetry if isinstance(telemetry, dict) else {}, plant if isinstance(plant, dict) else {})

    def reset_to_ready(self, result: TestResult, scenario: str = "NORMAL") -> None:
        self.send(result, "STOPSEQ")
        self.send(result, "PLANT RESET")
        self.send(result, "PLANT SCENARIO NORMAL")
        self.send(result, "SCENARIO NORMAL")
        self.send(result, "ACK")
        time.sleep(0.5)
        self.send(result, "RESET")
        self.wait_until(
            result,
            lambda s: as_float(s.telemetry.get("fault")) == 0
            and as_float(s.telemetry.get("run")) == 0
            and as_float(s.plant.get("speed_pct")) <= 1.0,
            timeout_s=18.0,
            keep_every=6,
        )
        self.send(result, "RESET")
        self.wait_until(
            result,
            lambda s: int(as_float(s.telemetry.get("ctrl"))) == 1
            and as_float(s.telemetry.get("fault")) == 0,
            timeout_s=10.0,
            keep_every=6,
        )
        if scenario != "NORMAL":
            self.send(result, f"PLANT SCENARIO {scenario}")
            self.send(result, f"SCENARIO {scenario}")
            time.sleep(0.8)
            self.wait_until(
                result,
                lambda s: int(as_float(s.telemetry.get("ctrl"))) == 1
                and as_float(s.telemetry.get("fault")) == 0,
                timeout_s=8.0,
                keep_every=6,
            )

    def wait_until(
        self,
        result: TestResult,
        predicate: Callable[[Sample], bool],
        timeout_s: float,
        keep_every: int = 2,
    ) -> Optional[Sample]:
        deadline = time.monotonic() + timeout_s
        count = 0
        last = None
        while time.monotonic() < deadline:
            sample = self.capture()
            last = sample
            count += 1
            if count % keep_every == 0 or predicate(sample):
                result.samples.append(sample)
            if predicate(sample):
                return sample
            time.sleep(self.poll_s)
        if last:
            result.samples.append(last)
        return None

    def run_normal_start_stop(self) -> TestResult:
        result = TestResult("normal_start_stop", False, "", 0.0)
        start = time.monotonic()
        try:
            self.reset_to_ready(result, "NORMAL")
            self.send(result, "STARTSEQ")
            running = self.wait_until(
                result,
                lambda s: as_float(s.telemetry.get("ctrl")) >= 5 and as_float(s.telemetry.get("fault")) == 0,
                timeout_s=55.0,
            )
            if not running:
                result.message = "did not reach synchronized/running without fault"
                return result
            if as_float(running.telemetry.get("bad")) > 0 or as_float(running.telemetry.get("drops")) > 0:
                result.message = "link counters bad/drops are nonzero during normal start"
                return result
            self.send(result, "STOPSEQ")
            stopped = self.wait_until(
                result,
                lambda s: as_float(s.telemetry.get("run")) == 0 and str(s.plant.get("plant_state")) in ("STOPPED", "COASTING"),
                timeout_s=24.0,
            )
            result.passed = stopped is not None
            result.message = "normal start/stop completed" if result.passed else "normal stop did not complete"
        except Exception as exc:
            result.message = f"exception: {exc}"
        finally:
            result.duration_s = time.monotonic() - start
        return result

    def run_fault_case(self, scenario: str, expected_fault: int, timeout_s: float = 36.0) -> TestResult:
        result = TestResult(f"fault_{scenario.lower()}", False, "", 0.0)
        start = time.monotonic()
        try:
            self.reset_to_ready(result, scenario)
            self.send(result, "STARTSEQ")
            started = self.wait_until(
                result,
                lambda s: as_float(s.telemetry.get("run")) == 1 or as_float(s.plant.get("speed_pct")) > 3.0,
                timeout_s=22.0,
            )
            if not started:
                last = result.samples[-1] if result.samples else self.capture()
                result.samples.append(last)
                result.message = (
                    "scenario did not start; "
                    f"ctrl={last.telemetry.get('ctrl')} fault={last.telemetry.get('fault')} "
                    f"faultcode={last.telemetry.get('faultcode')} plant={last.plant.get('plant_state')}"
                )
                return result
            faulted = self.wait_until(
                result,
                lambda s: as_float(s.telemetry.get("fault")) == 1 and int(as_float(s.telemetry.get("faultcode"))) == expected_fault,
                timeout_s=timeout_s,
            )
            if not faulted:
                last = result.samples[-1] if result.samples else self.capture()
                result.samples.append(last)
                result.message = (
                    f"expected faultcode {expected_fault}, got "
                    f"{int(as_float(last.telemetry.get('faultcode')))} fault={last.telemetry.get('fault')}"
                )
                return result
            result.passed = True
            result.message = f"{scenario} produced expected faultcode {expected_fault}"
        except Exception as exc:
            result.message = f"exception: {exc}"
        finally:
            self.send(result, "ACK")
            time.sleep(0.4)
            self.send(result, "RESET")
            self.send(result, "PLANT SCENARIO NORMAL")
            self.send(result, "SCENARIO NORMAL")
            result.duration_s = time.monotonic() - start
        return result

    def run_all(self, include_faults: bool = True) -> List[TestResult]:
        tests = [self.run_normal_start_stop]
        if include_faults:
            tests += [
                lambda: self.run_fault_case("NO_DISCHARGE", FAULT_NO_DISCHARGE_CURRENT),
                lambda: self.run_fault_case("NO_FIELD", FAULT_NO_FIELD_CURRENT, timeout_s=30.0),
                lambda: self.run_fault_case("THERMAL_TRIP", FAULT_THERMAL),
                lambda: self.run_fault_case("PULLOUT", FAULT_PULLOUT, timeout_s=60.0),
            ]
        return [test() for test in tests]


def main() -> int:
    parser = argparse.ArgumentParser(description="Run Nexus Sync hardware-in-the-loop tests")
    parser.add_argument("--base-url", default="http://127.0.0.1:8092")
    parser.add_argument("--report", default="/tmp/nexus_hil_report.json")
    parser.add_argument("--no-faults", action="store_true", help="Run only normal start/stop")
    args = parser.parse_args()

    runner = HilRunner(NexusApi(args.base_url))
    results = runner.run_all(include_faults=not args.no_faults)
    passed = sum(1 for r in results if r.passed)
    payload = {
        "created_at": now_iso(),
        "base_url": args.base_url,
        "passed": passed,
        "total": len(results),
        "ok": passed == len(results),
        "results": [r.to_json() for r in results],
    }
    Path(args.report).write_text(json.dumps(payload, indent=2), encoding="utf-8")
    for r in results:
        print(f"{'PASS' if r.passed else 'FAIL'} {r.name}: {r.message}")
    print(f"report={args.report}")
    return 0 if payload["ok"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
