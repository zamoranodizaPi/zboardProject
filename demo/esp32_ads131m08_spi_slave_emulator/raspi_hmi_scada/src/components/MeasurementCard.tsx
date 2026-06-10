import { Line, LineChart, ResponsiveContainer } from "recharts";
import type { TrendPoint } from "../types/telemetry";

interface Props {
  title: string;
  value: number;
  unit: string;
  min: number;
  max: number;
  nominal?: number;
  warn?: number;
  danger?: number;
  history?: TrendPoint[];
  trendKey?: keyof TrendPoint;
  compact?: boolean;
}

export function MeasurementCard({ title, value, unit, min, max, nominal, warn, danger, history = [], trendKey, compact = false }: Props) {
  const state = danger !== undefined && value >= danger ? "bad" : warn !== undefined && value >= warn ? "warn" : "ok";
  const color = state === "bad" ? "#EF4444" : state === "warn" ? "#EAB308" : "#22C55E";
  const digits = Math.abs(value) < 10 ? 2 : 1;
  const trend = trendKey ? history.slice(-48) : [];

  return (
    <article className={`industrial-panel-strong grid min-w-0 grid-rows-[auto_1fr] rounded-sm transition-colors duration-150 hover:border-[#3a5e7a] ${compact ? "px-2 py-1" : "px-3 py-2"}`}>
      <div className="flex items-start justify-between gap-2">
        <h3 className={`${compact ? "text-[9px]" : "text-xs"} font-black uppercase leading-tight text-[#DCE7EF]`}>{title}</h3>
        <span className="mt-0.5 h-2 w-2 shrink-0 rounded-full" style={{ backgroundColor: color }} />
      </div>
      <div className={`grid min-h-0 grid-cols-[1fr_54px] gap-2 ${compact ? "items-center" : "items-end"}`}>
        <div className="min-w-0">
          <div className="flex items-baseline gap-1">
            <span className={`font-mono ${compact ? "text-lg" : "text-2xl"} font-black tracking-tight`} style={{ color }}>
              {Number.isFinite(value) ? value.toFixed(digits) : "--"}
            </span>
            <span className="text-[11px] font-bold text-[#AFC4D8]">{unit}</span>
          </div>
          {!compact ? (
            <div className="mt-1 flex gap-2 text-[10px] text-[#6E879B]">
              <span>MIN {min}</span>
              {nominal !== undefined ? <span>NOM {nominal}</span> : null}
              <span>MAX {max}</span>
            </div>
          ) : null}
        </div>
        <div className={`${compact ? "h-4" : "h-10"} min-w-0`}>
          {trendKey ? (
            <ResponsiveContainer width="100%" height="100%">
              <LineChart data={trend}>
                <Line type="monotone" dataKey={trendKey} stroke={color} strokeWidth={1.6} dot={false} isAnimationActive={false} />
              </LineChart>
            </ResponsiveContainer>
          ) : null}
        </div>
      </div>
    </article>
  );
}
