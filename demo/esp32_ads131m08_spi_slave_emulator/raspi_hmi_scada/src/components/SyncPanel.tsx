import type { Telemetry } from "../types/telemetry";
import { loadAngle, n, syncDeltaF, syncWindow } from "../utils/domain";

export function SyncPanel({ telemetry, compact = false }: { telemetry: Telemetry; compact?: boolean }) {
  const deltaTheta = loadAngle(telemetry) - 30;
  const df = syncDeltaF(telemetry);
  const ok = syncWindow(telemetry);
  const vRatio = (telemetry.va ?? 0) > 1 ? (telemetry.va ?? 0) / 120 : 0;
  const gridAngle = -50;
  const motorAngle = -50 + deltaTheta;

  return (
    <div className={`grid h-full grid-cols-[0.9fr_1.1fr] ${compact ? "gap-2" : "gap-3"}`}>
      <div className={`industrial-panel-strong rounded-sm ${compact ? "p-2" : "p-3"}`}>
        <div className={`grid ${compact ? "gap-1 text-xs" : "gap-2 text-sm"}`}>
          <Metric label="Delta theta" value={`${n(deltaTheta, 1)} deg`} ok={Math.abs(deltaTheta) < 10} />
          <Metric label="Delta f" value={`${n(df, 3)} Hz`} ok={Math.abs(df) < 0.1} />
          <Metric label="Vmotor/Vred" value={`${n(vRatio, 2)} pu`} ok={Math.abs(vRatio - 1) < 0.08} />
        </div>
        <div className={`${compact ? "mt-2 px-2 py-1 text-[10px]" : "mt-3 px-3 py-2 text-sm"} rounded-sm border text-center font-black uppercase ${ok ? "border-[#22C55E] bg-[#052e16] text-ok" : "border-[#EAB308] bg-[#3a2c08] text-warn"}`}>
          {ok ? "DENTRO DE TOLERANCIA" : "FUERA DE TOLERANCIA"}
        </div>
      </div>
      <svg viewBox="0 0 220 170" className="h-full w-full rounded-sm border border-grid bg-[#080F14]">
        <defs>
          <marker id="sync-arrow" viewBox="0 0 10 10" refX="8" refY="5" markerWidth="6" markerHeight="6" orient="auto-start-reverse">
            <path d="M 0 0 L 10 5 L 0 10 z" fill="currentColor" />
          </marker>
        </defs>
        <circle cx="100" cy="85" r="68" fill="none" stroke="#203649" />
        <circle cx="100" cy="85" r="42" fill="none" stroke="#203649" strokeDasharray="3 3" />
        <line x1="20" y1="85" x2="180" y2="85" stroke="#203649" />
        <line x1="100" y1="8" x2="100" y2="162" stroke="#203649" />
        <Vector angle={gridAngle} color="#5fd08d" label="Vred" />
        <Vector angle={motorAngle} color="#f6c85f" label="Vmotor" />
        <text x="114" y="78" fill="#AFC4D8" fontSize="11">Delta</text>
      </svg>
    </div>
  );
}

function Metric({ label, value, ok }: { label: string; value: string; ok: boolean }) {
  return (
    <div className="grid grid-cols-[1fr_auto] border-b border-grid/70 pb-1">
      <span className="text-[#AFC4D8]">{label}</span>
      <b className={ok ? "text-ok" : "text-warn"}>{value}</b>
    </div>
  );
}

function Vector({ angle, color, label }: { angle: number; color: string; label: string }) {
  const r = (angle * Math.PI) / 180;
  const x = 100 + Math.cos(r) * 58;
  const y = 85 + Math.sin(r) * 58;
  return (
    <>
      <line x1="100" y1="85" x2={x} y2={y} stroke={color} strokeWidth="4" markerEnd="url(#sync-arrow)" />
      <circle cx="100" cy="85" r="4" fill="#DCE7EF" />
      <text x={x + 4} y={y - 4} fill={color} fontSize="12" fontWeight="700">{label}</text>
    </>
  );
}
