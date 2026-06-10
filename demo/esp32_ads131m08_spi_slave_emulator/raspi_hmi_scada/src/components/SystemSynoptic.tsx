import { Zap } from "lucide-react";
import type { Telemetry } from "../types/telemetry";
import { averageCurrent, averageVoltage, isFault, isRunning, n } from "../utils/domain";
import { operationalColor, stateColor } from "../utils/design";

export function SystemSynoptic({ telemetry, compact = false }: { telemetry: Telemetry; compact?: boolean }) {
  const running = isRunning(telemetry);
  const fault = isFault(telemetry);
  const breakerClosed = running || (telemetry.ctrl ?? 0) >= 2;
  const fieldOn = (telemetry.fs ?? 0) === 1;
  const flow = breakerClosed && !fault;
  const state = operationalColor(telemetry.ctrl, telemetry.fault);
  const voltage = averageVoltage(telemetry);
  const current = averageCurrent(telemetry);

  return (
    <div className={`relative ${compact ? "h-full min-h-[180px]" : "h-full min-h-[260px]"} overflow-hidden rounded-sm bg-[#080F14]`}>
      <svg viewBox="0 0 1180 330" className="h-full w-full">
        <defs>
          <marker id="power-arrow" viewBox="0 0 10 10" refX="8" refY="5" markerWidth="7" markerHeight="7" orient="auto">
            <path d="M 0 0 L 10 5 L 0 10 z" fill="#38BDF8" />
          </marker>
          <linearGradient id="steel" x1="0" x2="1">
            <stop offset="0" stopColor="#657587" />
            <stop offset="0.45" stopColor="#CBD5E1" />
            <stop offset="1" stopColor="#475569" />
          </linearGradient>
          <linearGradient id="motorSkin" x1="0" x2="1">
            <stop offset="0" stopColor="#1E3A5F" />
            <stop offset="0.5" stopColor="#3B82F6" />
            <stop offset="1" stopColor="#1D4ED8" />
          </linearGradient>
        </defs>

        <rect x="18" y="20" width="1144" height="286" rx="4" fill="#0B141C" stroke="#203649" />

        <g transform="translate(56 58)">
          <text x="0" y="-18" fill="#DCE7EF" fontSize="16" fontWeight="800">RED MT</text>
          <path d="M30 22 L0 126 H60 Z" fill="none" stroke="#AFC4D8" strokeWidth="3" />
          <path d="M8 95 H52 M15 72 H45 M22 49 H38 M30 22 V126" stroke="#AFC4D8" strokeWidth="2" />
          <text x="-4" y="150" fill="#6E879B" fontSize="13">13.8 kV</text>
        </g>

        <PowerLine y={112} color="#EAB308" flow={flow} />
        <PowerLine y={152} color="#22C55E" flow={flow} />
        <PowerLine y={192} color="#EF4444" flow={flow} />
        <text x="210" y="88" fill="#AFC4D8" fontSize="13">V LINEA {n(voltage, 1)} V</text>
        <text x="665" y="88" fill="#AFC4D8" fontSize="13">I ESTATOR {n(current, 2)} A</text>

        <EquipmentLabel x={256} y={64} label="PT" />
        <rect x="242" y="92" width="46" height="122" rx="4" fill="#101B25" stroke="#AFC4D8" />
        {[112, 152, 192].map((y) => <path key={y} d={`M250 ${y} C260 ${y - 18} 272 ${y + 18} 282 ${y}`} fill="none" stroke="#DCE7EF" strokeWidth="2" />)}
        <path d="M265 214 V246 M248 246 H282 M255 256 H275" stroke="#AFC4D8" strokeWidth="2" />

        <EquipmentLabel x={366} y={64} label="BREAKER" />
        <rect x="356" y="92" width="78" height="122" rx="4" fill="url(#steel)" stroke="#203649" strokeWidth="2" />
        <line x1="372" y1="126" x2="418" y2={breakerClosed ? 110 : 140} stroke="#080F14" strokeWidth="5" strokeLinecap="round" />
        <circle cx="378" cy="178" r="8" fill={breakerClosed ? stateColor.running : stateColor.disabled} stroke="#080F14" />
        <circle cx="412" cy="178" r="8" fill={fault ? stateColor.fault : stateColor.disabled} stroke="#080F14" />
        <text x="352" y="238" fill={breakerClosed ? stateColor.running : stateColor.disabled} fontSize="12" fontWeight="800">
          {breakerClosed ? "CERRADO" : "ABIERTO"}
        </text>

        <EquipmentLabel x={500} y={64} label="CT" />
        {[486, 512, 538].map((x) => <rect key={x} x={x} y="92" width="18" height="122" rx="4" fill="#111C26" stroke="#AFC4D8" />)}
        {[112, 152, 192].map((y) => <circle key={y} cx="521" cy={y} r="8" fill="none" stroke="#DCE7EF" strokeWidth="2" />)}

        <g transform="translate(660 76)">
          <text x="88" y="-16" fill="#DCE7EF" fontSize="18" fontWeight="900">MOTOR SINCRONO</text>
          <rect x="64" y="58" width="230" height="112" rx="48" fill="url(#motorSkin)" stroke="#AFC4D8" strokeWidth="3" />
          {[88, 118, 148, 178, 208, 238].map((x) => <line key={x} x1={x} y1="72" x2={x} y2="156" stroke="#1E293B" strokeWidth="3" opacity="0.55" />)}
          <rect x="116" y="26" width="96" height="38" rx="5" fill="#2563EB" stroke="#AFC4D8" />
          <rect x="42" y="88" width="32" height="48" fill="#1E293B" stroke="#AFC4D8" />
          <line x1="294" y1="114" x2="360" y2="114" stroke="url(#steel)" strokeWidth="14" strokeLinecap="round" />
          <circle cx="322" cy="114" r="30" fill="none" stroke={running ? stateColor.running : stateColor.disabled} strokeWidth="8" strokeDasharray="34 18" className={running ? "spin" : ""} />
          <text x="132" y="204" fill={state} fontSize="14" fontWeight="900">{fault ? "FAULT" : running ? "RUNNING" : "READY"}</text>
        </g>

        <g transform="translate(1006 84)">
          <EquipmentLabel x="8" y="-20" label="EXCITADOR DC" />
          <rect x="18" y="38" width="76" height="128" rx="4" fill="url(#steel)" stroke="#203649" strokeWidth="2" />
          <rect x="34" y="56" width="42" height="34" fill="#0D1823" stroke="#38BDF8" />
          <circle cx="56" cy="116" r="5" fill={fieldOn ? stateColor.running : stateColor.disabled} />
          <path d="M94 80 H140 V170 H94" stroke={fieldOn ? stateColor.fault : stateColor.disabled} strokeWidth="5" fill="none" />
          <rect x="136" y="102" width="48" height="72" rx="18" fill="#101820" stroke="#AFC4D8" />
          <path d="M142 112 C156 130 158 150 178 166" fill="none" stroke={fieldOn ? "#EF4444" : "#64748B"} strokeWidth="4" />
          <text x="122" y="94" fill="#DCE7EF" fontSize="12" fontWeight="800">ROTOR</text>
        </g>

        <path d="M342 268 H450" stroke="#38BDF8" strokeWidth="3" strokeDasharray="10 8" markerEnd="url(#power-arrow)" opacity="0.9" />
        <path d="M1018 240 V270 H606" stroke="#38BDF8" strokeWidth="3" strokeDasharray="10 8" markerEnd="url(#power-arrow)" opacity="0.9" />
        <rect x="456" y="238" width="150" height="54" rx="3" fill="#13202B" stroke="#203649" />
        <text x="484" y="270" fill="#22C55E" fontSize="15" fontFamily="monospace" fontWeight="900">NEXUS SYNC</text>

        {!compact ? (
          <>
            <text x="350" y="292" fill="#6E879B" fontSize="12">flujo energia</text>
            <text x="892" y="292" fill="#6E879B" fontSize="12">control excitacion</text>
            <Zap x={28} y={268} width={22} height={22} color={flow ? stateColor.running : stateColor.disabled} />
          </>
        ) : null}
      </svg>
    </div>
  );
}

function EquipmentLabel({ x, y, label }: { x: number | string; y: number | string; label: string }) {
  return <text x={x} y={y} fill="#DCE7EF" fontSize="15" fontWeight="900">{label}</text>;
}

function PowerLine({ y, color, flow }: { y: number; color: string; flow: boolean }) {
  return <path d={`M118 ${y} H610 H646`} stroke={color} strokeWidth="5" strokeLinecap="round" className={flow ? "flow-line" : ""} />;
}
