export const stateColor = {
  running: "#22C55E",
  ready: "#3B82F6",
  starting: "#EAB308",
  sync: "#38BDF8",
  fault: "#EF4444",
  disabled: "#64748B"
};

export const seriesColor = {
  voltage: "#EAB308",
  current: "#38BDF8",
  power: "#22C55E",
  frequency: "#A78BFA",
  angle: "#F97316",
  field: "#60A5FA"
};

export function operationalColor(ctrl?: number, fault?: number) {
  if (fault === 1 || ctrl === 7 || ctrl === 8) return stateColor.fault;
  if (ctrl === 6) return stateColor.running;
  if (ctrl === 5) return stateColor.sync;
  if (ctrl === 2 || ctrl === 3 || ctrl === 4) return stateColor.starting;
  if (ctrl === 1) return stateColor.ready;
  return stateColor.disabled;
}
