export type Telemetry = Record<string, number>;

export interface BackendState {
  telemetry: Telemetry;
  plant?: PlantTelemetry;
  last_line: string;
  age_ms: number;
  serial_port: string;
  ads_serial_port: string;
  response: string;
}

export interface PlantTelemetry {
  plant_state?: string;
  plant_state_id?: number;
  plant_scenario?: string;
  speed_pct?: number;
  slip_hz?: number;
  load_pct?: number;
  stator_current_scale?: number;
  motor_torque_pu?: number;
  load_torque_pu?: number;
  field_current?: number;
  field_voltage?: number;
  discharge_current?: number;
  pullout_risk?: number;
  field_enable?: number;
  field_pwm_seen?: number;
  breaker_closed?: number;
  speed_ok?: number;
  field_current_fb?: number;
  discharge_current_fb?: number;
  thermal_ok?: number;
  plant_fault?: number;
  rectifier_ready?: number;
  rf3_enabled?: number;
  pf_reg_disabled?: number;
  scr_angle_deg?: number;
  scr_firing_deg?: number;
  scr_gate_width_deg?: number;
  scr_active_pair?: number;
  dc_bus_voltage?: number;
  rectifier_avg_v?: number;
  rectifier_ripple_v?: number;
  rectifier_inst_v?: number;
  rectifier_va?: number;
  rectifier_vb?: number;
  rectifier_vc?: number;
  comm_overlap_deg?: number;
  field_voltage_xdcr?: number;
  field_current_xdcr?: number;
  discharge_current_xdcr?: number;
  pf_signal?: number;
  bleed_active?: number;
  discharge_active?: number;
  crowbar_active?: number;
  sk10?: number;
  sk11?: number;
  sk12?: number;
  sk13?: number;
  g1?: number;
  g2?: number;
  g3?: number;
  g4?: number;
  g5?: number;
  g6?: number;
  g1_exec?: number;
  g2_exec?: number;
  g3_exec?: number;
  g4_exec?: number;
  g5_exec?: number;
  g6_exec?: number;
  k1?: number;
  k2?: number;
  k3?: number;
  k4?: number;
  k5?: number;
  k6?: number;
  gate_fail_1?: number;
  gate_fail_2?: number;
  gate_fail_3?: number;
  gate_fail_4?: number;
  gate_fail_5?: number;
  gate_fail_6?: number;
  scr_open_1?: number;
  scr_open_2?: number;
  scr_open_3?: number;
  scr_open_4?: number;
  scr_open_5?: number;
  scr_open_6?: number;
  scr_short_1?: number;
  scr_short_2?: number;
  scr_short_3?: number;
  scr_short_4?: number;
  scr_short_5?: number;
  scr_short_6?: number;
}

export interface TrendPoint {
  ts: number;
  va: number;
  ia: number;
  f: number;
  p: number;
  q: number;
  pf: number;
  angle: number;
  fielda: number;
}

export interface ProviderStatus {
  loading: boolean;
  error?: string;
  connected: boolean;
  lastUpdateMs: number;
}

export type CommandResult = { response?: string; error?: string };

export type AdsProfile =
  | "MANUAL"
  | "GRID_NORMAL"
  | "PHASE_LOSS_A"
  | "PHASE_LOSS_B"
  | "PHASE_LOSS_C"
  | "VOLTAGE_SAG"
  | "UNBALANCE"
  | "LOW_PF"
  | "FREQ_STEP"
  | "START_PROFILE"
  | "PULLOUT"
  | "NO_SIGNAL";

export interface AdsChannelConfig {
  index: number;
  freq: number;
  amp: number;
  phase: number;
}

export interface ComtradeRecordMeta {
  id: string;
  event: string;
  reason?: string;
  start?: string;
  trigger?: string;
  samples: number;
  sample_rate?: number;
  cfg?: string;
  dat?: string;
  active?: boolean;
}

export interface ComtradeRecord {
  meta: ComtradeRecordMeta;
  samples: Array<Record<string, number>>;
}
