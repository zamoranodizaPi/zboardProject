#!/usr/bin/env python3
import argparse
import csv
import os
import queue
import sys
import threading
import time
from typing import Dict

import serial


def parse_tel(line: str) -> Dict[str, str]:
    fields: Dict[str, str] = {}
    for token in line.strip().split()[1:]:
        if "=" in token:
            key, value = token.split("=", 1)
            fields[key] = value
    return fields


def send_command(port: serial.Serial, command: str) -> None:
    port.write((command.strip() + "\n").encode("ascii"))
    port.flush()


def reader(port: serial.Serial, out: "queue.Queue[str]", stop: threading.Event) -> None:
    while not stop.is_set():
        try:
            raw = port.readline()
        except serial.SerialException as exc:
            out.put(f"ERR serial_read {exc}")
            stop.set()
            return
        if raw:
            out.put(raw.decode("ascii", errors="replace").strip())


def input_worker(commands: "queue.Queue[str]", stop: threading.Event) -> None:
    while not stop.is_set():
        try:
            line = input()
        except EOFError:
            stop.set()
            return
        commands.put(line.strip())


def normalize_user_command(line: str) -> str:
    if not line:
        return ""
    parts = line.split()
    cmd = parts[0].lower()
    arg = parts[1] if len(parts) > 1 else ""
    if cmd == "s":
        return f"SPEED {arg}"
    if cmd == "a":
        return f"ANGLE {arg}"
    if cmd == "e":
        return f"ENABLE {arg}"
    if cmd == "f":
        return f"FAULT {arg}"
    if cmd == "r":
        return f"ACCEL {arg}"
    if cmd == "q":
        return "QUIT"
    return line.upper()


def print_dashboard(fields: Dict[str, str]) -> None:
    theta = fields.get("theta", "--")
    speed = fields.get("speed", "--")
    target = fields.get("target", "--")
    angle = fields.get("set", "--")
    hall = fields.get("hall", "---")
    sector = fields.get("sector", "-")
    enable = fields.get("en", "-")
    fault = fields.get("fault", "-")
    gate = fields.get("gate", "-")
    zc = fields.get("zc", "-")
    idx = fields.get("idx", "-")
    sys.stdout.write(
        "\r"
        f"theta={theta:>7} deg  speed={speed:>8} target={target:>8}  "
        f"angle={angle:>6}  hall={hall} sector={sector}  "
        f"en={enable} fault={fault} gate={gate} zc={zc} idx={idx}      "
    )
    sys.stdout.flush()


def open_csv(path: str):
    if not path:
        return None, None
    directory = os.path.dirname(path)
    if directory:
        os.makedirs(directory, exist_ok=True)
    handle = open(path, "w", newline="", encoding="utf-8")
    fieldnames = [
        "host_time",
        "t_ms",
        "en",
        "fault",
        "theta",
        "speed",
        "target",
        "accel",
        "set",
        "sector",
        "hall",
        "gate",
        "zc",
        "idx",
    ]
    writer = csv.DictWriter(handle, fieldnames=fieldnames)
    writer.writeheader()
    return handle, writer


def main() -> int:
    parser = argparse.ArgumentParser(description="Monitor serial para ESP32 angle simulator.")
    parser.add_argument("--port", required=True, help="Puerto serial, por ejemplo /dev/ttyUSB0 o COM3.")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--speed", type=float, default=30.0, help="Velocidad electrica inicial en deg/s.")
    parser.add_argument("--accel", type=float, default=180.0, help="Aceleracion inicial en deg/s2.")
    parser.add_argument("--angle", type=float, default=90.0, help="Angulo de disparo inicial 0..180.")
    parser.add_argument("--stream-ms", type=int, default=100, help="Periodo de telemetria.")
    parser.add_argument("--log", default="", help="Ruta CSV opcional.")
    args = parser.parse_args()

    stop = threading.Event()
    lines: "queue.Queue[str]" = queue.Queue()
    user_commands: "queue.Queue[str]" = queue.Queue()

    try:
        port = serial.Serial(args.port, args.baud, timeout=0.2)
    except serial.SerialException as exc:
        print(f"No pude abrir {args.port}: {exc}", file=sys.stderr)
        return 1

    csv_handle, csv_writer = open_csv(args.log)

    rx_thread = threading.Thread(target=reader, args=(port, lines, stop), daemon=True)
    in_thread = threading.Thread(target=input_worker, args=(user_commands, stop), daemon=True)
    rx_thread.start()
    in_thread.start()

    time.sleep(1.0)
    for command in (
        f"STREAM {args.stream_ms}",
        f"ACCEL {args.accel}",
        f"ANGLE {args.angle}",
        f"SPEED {args.speed}",
        "ENABLE 1",
        "STATUS",
    ):
        send_command(port, command)
        time.sleep(0.05)

    print("Monitor listo. Comandos: s <speed>, a <angle>, e <0|1>, f <0|1>, r <accel>, q")

    try:
        while not stop.is_set():
            while not user_commands.empty():
                command = normalize_user_command(user_commands.get())
                if command == "QUIT":
                    stop.set()
                    break
                if command:
                    send_command(port, command)

            try:
                line = lines.get(timeout=0.1)
            except queue.Empty:
                continue

            if line.startswith("TEL "):
                fields = parse_tel(line)
                print_dashboard(fields)
                if csv_writer is not None:
                    row = {"host_time": f"{time.time():.3f}"}
                    row.update(fields)
                    csv_writer.writerow(row)
            elif line.startswith("OK") or line.startswith("ERR"):
                print(f"\n{line}")
    except KeyboardInterrupt:
        stop.set()
    finally:
        print()
        try:
            send_command(port, "ENABLE 0")
        except serial.SerialException:
            pass
        port.close()
        if csv_handle is not None:
            csv_handle.close()

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
