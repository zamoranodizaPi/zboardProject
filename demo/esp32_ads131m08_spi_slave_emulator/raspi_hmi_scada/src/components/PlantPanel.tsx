import type { BackendState } from "../types/telemetry";

export function PlantPanel({ state, compact = false }: { state?: BackendState; compact?: boolean }) {
  const p = state?.plant ?? {};
  const t = state?.telemetry ?? {};
  const plantState = p.plant_state ?? "--";
  const scenario = p.plant_scenario ?? "NORMAL";
  const rows: Array<[string, number | undefined, string]> = [
    ["Velocidad", p.speed_pct ?? t.speed_pct, "%"],
    ["Slip", p.slip_hz ?? t.slip_hz, "Hz"],
    ["Carga", p.load_pct ?? t.load_pct, "%"],
    ["I Scale", p.stator_current_scale ?? t.stator_current_scale, "pu"],
    ["T Motor", p.motor_torque_pu ?? t.motor_torque_pu, "pu"],
    ["T Carga", p.load_torque_pu ?? t.load_torque_pu, "pu"],
    ["FIELD_CURRENT", p.field_current ?? t.field_current, "A"],
    ["FIELD_VOLTAGE", p.field_voltage ?? t.field_voltage, "V"],
    ["Descarga", p.discharge_current ?? t.discharge_current, "A"],
    ["Pullout", p.pullout_risk ?? t.pullout_risk, ""],
    ["SCR Disparo", p.scr_firing_deg, "deg"],
    ["DC Bus", p.dc_bus_voltage, "V"],
    ["Rect AVG", p.rectifier_avg_v, "V"],
    ["Ripple", p.rectifier_ripple_v, "V"],
    ["Overlap", p.comm_overlap_deg, "deg"]
  ];
  const feedbackRows: Array<[string, number | undefined]> = [
    ["BREAKER", p.breaker_closed],
    ["SPEED_OK", p.speed_ok],
    ["FIELD_FB", p.field_current_fb],
    ["DISCH_FB", p.discharge_current_fb],
    ["THERMAL", p.thermal_ok],
    ["PLANT_FLT", p.plant_fault]
  ];
  const fieldEnable = p.field_enable ?? t.field_enable ?? 0;
  const fieldPwm = p.field_pwm_seen ?? t.field_pwm_seen ?? 0;
  const gates = [p.g1, p.g2, p.g3, p.g4, p.g5, p.g6];
  const executedGates = [p.g1_exec ?? p.g1, p.g2_exec ?? p.g2, p.g3_exec ?? p.g3, p.g4_exec ?? p.g4, p.g5_exec ?? p.g5, p.g6_exec ?? p.g6];
  const commandedGates = [t.scrcmd_g1, t.scrcmd_g2, t.scrcmd_g3, t.scrcmd_g4, t.scrcmd_g5, t.scrcmd_g6];
  const conduction = [p.k1, p.k2, p.k3, p.k4, p.k5, p.k6];
  const gateFaults = [p.gate_fail_1, p.gate_fail_2, p.gate_fail_3, p.gate_fail_4, p.gate_fail_5, p.gate_fail_6];
  const scrOpen = [p.scr_open_1, p.scr_open_2, p.scr_open_3, p.scr_open_4, p.scr_open_5, p.scr_open_6];
  const scrShort = [p.scr_short_1, p.scr_short_2, p.scr_short_3, p.scr_short_4, p.scr_short_5, p.scr_short_6];

  return (
    <div className={compact ? "grid h-full grid-cols-[1fr_1.45fr] gap-2 text-[11px]" : "grid gap-3"}>
      <div className="rounded-sm border border-grid bg-[#08131b] p-3">
        <div className="text-[10px] font-black uppercase tracking-[0.08em] text-[#6E879B]">Planta simulada</div>
        <div className="mt-1 font-mono text-lg font-black text-[#38BDF8]">{plantState}</div>
        <div className="mt-1 text-xs font-bold text-[#AFC4D8]">{scenario}</div>
        <div className="mt-2 grid grid-cols-2 gap-2 text-[10px] font-black uppercase">
          <StatusPill label="FIELD_ENABLE" active={fieldEnable === 1} />
          <StatusPill label="FIELD_PWM" active={fieldPwm === 1} warn />
          <StatusPill label="RF3" active={(p.rf3_enabled ?? 0) === 1} />
          <StatusPill label="RECT_READY" active={(p.rectifier_ready ?? 0) === 1} />
        </div>
      </div>
      <div className={`grid ${compact ? "grid-cols-1 content-start gap-1" : "grid-cols-2 gap-2"}`}>
        {rows.map(([label, value, unit]) => (
          <div key={label} className={`rounded-sm border border-grid bg-panel2 ${compact ? "grid grid-cols-[1fr_auto_auto] items-baseline gap-2 px-2 py-1" : "px-3 py-2"}`}>
            <div className={`font-black uppercase text-[#6E879B] ${compact ? "truncate text-[9px] tracking-[0.04em]" : "text-[10px] tracking-[0.08em]"}`}>{label}</div>
            <div className={`font-mono font-black text-[#DCE7EF] ${compact ? "text-sm" : "text-lg"}`}>
              {typeof value === "number" ? value.toFixed(label === "Pullout" ? 2 : 1) : "--"}
            </div>
            <span className={`${compact ? "text-[10px]" : "ml-1 text-xs"} text-[#AFC4D8]`}>{unit}</span>
          </div>
        ))}
      </div>
      <div className={compact ? "col-span-2 grid grid-cols-3 gap-2" : "grid grid-cols-3 gap-2"}>
        <ScrBank title="FPGA Gate Cmd" values={commandedGates} activeColor="ok" />
        <ScrBank title="Exciter Gates" values={gates} activeColor="ok" />
        <ScrBank title="Gate Exec" values={executedGates} activeColor="ok" />
        <ScrBank title="Conducting K1-K6" values={conduction} activeColor="warn" />
        <ScrBank title="Gate Fail" values={gateFaults} activeColor="bad" />
        <ScrBank title="SCR Open" values={scrOpen} activeColor="bad" />
        <ScrBank title="SCR Short" values={scrShort} activeColor="bad" />
      </div>
      {!compact ? (
        <div className="grid grid-cols-2 gap-3">
          <div className="grid grid-cols-3 gap-2">
            {feedbackRows.map(([label, value]) => <StatusPill key={label} label={label} active={(value ?? 0) === 1} />)}
          </div>
          <div className="grid grid-cols-4 gap-2">
            <StatusPill label="BLEED" active={(p.bleed_active ?? 0) === 1} warn />
            <StatusPill label="DISCH" active={(p.discharge_active ?? 0) === 1} warn />
            <StatusPill label="CROWBAR" active={(p.crowbar_active ?? 0) === 1} warn />
            <StatusPill label="PF DIS" active={(p.pf_reg_disabled ?? 0) === 1} warn />
            <StatusPill label="SK10" active={(p.sk10 ?? 0) === 1} />
            <StatusPill label="SK11" active={(p.sk11 ?? 0) === 1} />
            <StatusPill label="SK12" active={(p.sk12 ?? 0) === 1} />
            <StatusPill label="SK13" active={(p.sk13 ?? 0) === 1} />
          </div>
        </div>
      ) : null}
    </div>
  );
}

function StatusPill({ label, active, warn = false }: { label: string; active: boolean; warn?: boolean }) {
  return (
    <div className={`rounded-sm border px-2 py-1 text-center font-mono text-[10px] font-black ${active ? warn ? "border-[#854D0E] bg-[#3B2504] text-warn" : "border-[#166534] bg-[#052e16] text-ok" : "border-grid bg-panel2 text-[#64748B]"}`}>
      {label}
    </div>
  );
}

function ScrBank({ title, values, activeColor }: { title: string; values: Array<number | undefined>; activeColor: "ok" | "warn" | "bad" }) {
  return (
    <div className="rounded-sm border border-grid bg-[#08131b] p-2">
      <div className="mb-1 truncate text-[9px] font-black uppercase tracking-[0.08em] text-[#6E879B]">{title}</div>
      <div className="grid grid-cols-6 gap-1">
        {values.map((value, index) => {
          const active = (value ?? 0) === 1;
          const activeClass = activeColor === "bad"
            ? "border-[#7f1d1d] bg-[#3f1111] text-bad"
            : activeColor === "warn"
              ? "border-[#854D0E] bg-[#3B2504] text-warn"
              : "border-[#166534] bg-[#052e16] text-ok";
          return (
            <div key={index} className={`rounded-sm border py-1 text-center font-mono text-[10px] font-black ${active ? activeClass : "border-grid bg-panel2 text-[#64748B]"}`}>
              {index + 1}
            </div>
          );
        })}
      </div>
    </div>
  );
}
