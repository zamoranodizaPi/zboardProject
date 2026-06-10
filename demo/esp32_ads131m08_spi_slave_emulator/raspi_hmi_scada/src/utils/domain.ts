import type { Telemetry } from "../types/telemetry";

export const ctrlNames = ["IDLE", "READY", "STARTING", "ACCELERATION", "FIELD APPLY", "SYNCHRONIZING", "RUNNING", "FAULT", "LOCKOUT"];

export const faultNames: Record<number, string> = {
  0: "NONE",
  1: "NO DISCHARGE CURRENT",
  2: "NO FIELD CURRENT",
  3: "LOW POWER FACTOR",
  4: "THERMAL FAULT",
  5: "INCOMPLETE SEQUENCE",
  6: "PULLOUT",
  16: "UNDERVOLTAGE",
  17: "OVERVOLTAGE",
  18: "UNDERFREQUENCY",
  19: "OVERFREQUENCY",
  20: "PHASE LOSS",
  21: "VOLTAGE UNBALANCE",
  22: "PHASE SEQUENCE",
  23: "LOSS OF SIGNAL"
};

export function n(value: number | undefined, digits = 2) {
  return Number.isFinite(value) ? Number(value).toFixed(digits) : "--";
}

export function ctrlName(t: Telemetry) {
  return ctrlNames[t.ctrl ?? 0] ?? "--";
}

export function faultName(t: Telemetry) {
  return faultNames[t.faultcode ?? 0] ?? "--";
}

export function isRunning(t: Telemetry) {
  return (t.ctrl ?? 0) === 6;
}

export function isFault(t: Telemetry) {
  return (t.fault ?? 0) === 1 || (t.ctrl ?? 0) === 7;
}

export function statusColor(t: Telemetry) {
  if (isFault(t)) return "text-bad";
  if (isRunning(t)) return "text-ok";
  return "text-warn";
}

export function averageVoltage(t: Telemetry) {
  return ((t.va ?? 0) + (t.vb ?? 0) + (t.vc ?? 0)) / 3;
}

export function averageCurrent(t: Telemetry) {
  return ((t.ia ?? 0) + (t.ib ?? 0) + (t.ic ?? 0)) / 3;
}

export function loadAngle(t: Telemetry) {
  return Math.abs(t.aia ?? 0);
}

export function syncDeltaF(t: Telemetry) {
  return (t.f ?? 60) - 60;
}

export function syncWindow(t: Telemetry) {
  return Math.abs(syncDeltaF(t)) < 0.1 && Math.abs(loadAngle(t) - 30) < 10 && !isFault(t);
}
