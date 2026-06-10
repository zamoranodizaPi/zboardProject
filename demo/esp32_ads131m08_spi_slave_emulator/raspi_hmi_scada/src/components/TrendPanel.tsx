import { CartesianGrid, Legend, Line, LineChart, ResponsiveContainer, Tooltip, XAxis, YAxis } from "recharts";
import type { TrendPoint } from "../types/telemetry";

export function TrendPanel({ history, compact = false }: { history: TrendPoint[]; compact?: boolean }) {
  const data = history.slice(compact ? -80 : -240).map((p) => ({
    ...p,
    time: new Date(p.ts).toLocaleTimeString([], { minute: "2-digit", second: "2-digit" })
  }));
  return (
    <ResponsiveContainer width="100%" height="100%">
      <LineChart data={data} margin={{ left: -20, right: 12, top: compact ? 6 : 12, bottom: compact ? 0 : 4 }}>
        <CartesianGrid stroke="#203649" strokeDasharray="2 4" />
        <XAxis dataKey="time" stroke="#6E879B" tick={{ fontSize: 11, fill: "#AFC4D8" }} minTickGap={24} />
        <YAxis stroke="#6E879B" tick={{ fontSize: 11, fill: "#AFC4D8" }} />
        {!compact ? <Tooltip contentStyle={{ background: "#13202B", border: "1px solid #203649", color: "#DCE7EF" }} labelStyle={{ color: "#AFC4D8" }} /> : null}
        {!compact ? <Legend wrapperStyle={{ color: "#AFC4D8", fontSize: 12 }} /> : null}
        <Line dataKey="va" name="Voltaje" stroke="#EAB308" strokeWidth={2} dot={false} isAnimationActive={false} />
        <Line dataKey="ia" name="Corriente" stroke="#38BDF8" strokeWidth={2} dot={false} isAnimationActive={false} />
        <Line dataKey="p" name="Potencia" stroke="#22C55E" strokeWidth={2} dot={false} isAnimationActive={false} />
        <Line dataKey="f" name="Frecuencia" stroke="#A78BFA" strokeWidth={2} dot={false} isAnimationActive={false} />
      </LineChart>
    </ResponsiveContainer>
  );
}
