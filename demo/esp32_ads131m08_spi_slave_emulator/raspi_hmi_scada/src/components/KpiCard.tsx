import { Line, LineChart, ResponsiveContainer } from "recharts";
import type { TrendPoint } from "../types/telemetry";

interface Props {
  label: string;
  value: string;
  unit?: string;
  status?: "ok" | "warn" | "bad";
  trend?: TrendPoint[];
  trendKey?: keyof TrendPoint;
}

const colorMap = { ok: "#22C55E", warn: "#EAB308", bad: "#EF4444" };

export function KpiCard({ label, value, unit, status = "ok", trend = [], trendKey }: Props) {
  return (
    <div className="industrial-panel-strong min-w-0 rounded-sm px-3 py-2 transition-colors duration-150 hover:border-[#3a5e7a]">
      <div className="flex items-center justify-between gap-2">
        <div className="truncate text-[10px] font-black uppercase tracking-[0.08em] text-[#AFC4D8]">{label}</div>
        <span className="h-2 w-2 rounded-full" style={{ backgroundColor: colorMap[status] }} />
      </div>
      <div className="mt-1 flex items-baseline gap-1">
        <span className="font-mono text-2xl font-black leading-none" style={{ color: colorMap[status] }}>{value}</span>
        {unit ? <span className="text-[11px] font-bold text-[#AFC4D8]">{unit}</span> : null}
      </div>
      {trendKey ? (
        <div className="mt-1 h-8">
          <ResponsiveContainer width="100%" height="100%">
            <LineChart data={trend.slice(-40)}>
              <Line type="monotone" dataKey={trendKey} stroke={colorMap[status]} strokeWidth={1.5} dot={false} isAnimationActive={false} />
            </LineChart>
          </ResponsiveContainer>
        </div>
      ) : null}
    </div>
  );
}
