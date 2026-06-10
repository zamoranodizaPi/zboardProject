interface Props {
  title: string;
  value: number;
  unit: string;
  min: number;
  max: number;
  warn?: number;
  danger?: number;
  compact?: boolean;
}

export function GaugeCard({ title, value, unit, min, max, warn, danger, compact = false }: Props) {
  const pct = Math.max(0, Math.min(1, (value - min) / (max - min || 1)));
  const angle = -120 + pct * 240;
  const color = danger !== undefined && value >= danger ? "#ff7a7a" : warn !== undefined && value >= warn ? "#f6c85f" : "#5fd08d";
  const ticks = Array.from({ length: 7 }, (_, i) => -120 + i * 40);

  return (
    <div className="rounded-md border border-grid bg-panel2 p-1.5 text-center">
      <div className={`${compact ? "h-7 text-[10px]" : "h-9 text-[11px]"} font-bold uppercase leading-tight text-slate-100`}>{title}</div>
      <svg viewBox="0 0 120 82" className={`mx-auto ${compact ? "h-[54px]" : "h-[72px]"} w-full`}>
        <path d="M 18 66 A 42 42 0 0 1 102 66" fill="none" stroke="#3a4b55" strokeWidth="7" />
        <path d="M 18 66 A 42 42 0 0 1 102 66" fill="none" stroke={color} strokeWidth="7" strokeDasharray={`${pct * 132} 132`} />
        {ticks.map((a) => {
          const r = (a * Math.PI) / 180;
          const x1 = 60 + Math.cos(r) * 34;
          const y1 = 66 + Math.sin(r) * 34;
          const x2 = 60 + Math.cos(r) * 42;
          const y2 = 66 + Math.sin(r) * 42;
          return <line key={a} x1={x1} y1={y1} x2={x2} y2={y2} stroke="#d9e6ef" strokeWidth="1" />;
        })}
        <line x1="60" y1="66" x2={60 + Math.cos((angle * Math.PI) / 180) * 34} y2={66 + Math.sin((angle * Math.PI) / 180) * 34} stroke="#f8fafc" strokeWidth="4" strokeLinecap="round" />
        <circle cx="60" cy="66" r="4" fill="#f8fafc" />
      </svg>
      <div className={`font-mono ${compact ? "text-lg" : "text-2xl"} font-bold`} style={{ color }}>{Number.isFinite(value) ? value.toFixed(value < 10 ? 2 : 1) : "--"}</div>
      <div className={`${compact ? "text-[10px]" : "text-xs"} text-slate-300`}>{unit}</div>
    </div>
  );
}
