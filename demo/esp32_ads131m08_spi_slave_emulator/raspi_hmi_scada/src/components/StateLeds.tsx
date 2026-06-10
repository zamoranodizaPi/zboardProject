import type { Telemetry } from "../types/telemetry";
import { ctrlNames } from "../utils/domain";
import { operationalColor, stateColor } from "../utils/design";

export function StateLeds({ telemetry, compact = false }: { telemetry: Telemetry; compact?: boolean }) {
  const active = telemetry.ctrl ?? 0;
  return (
    <div className={`${compact ? "space-y-0" : "space-y-1"} relative`}>
      {ctrlNames.map((name, index) => {
        const isActive = active === index;
        const isDone = index < active && active < 7;
        const isFault = index === 7 || index === 8;
        const blocked = (active === 7 || active === 8) && index > active;
        const color = isActive ? operationalColor(active, telemetry.fault) : isDone ? stateColor.running : blocked ? stateColor.disabled : "#6E879B";
        return (
          <div key={name} className={`relative grid grid-cols-[22px_1fr_auto] items-center gap-2 border-l ${compact ? "py-1 pl-1 pr-2" : "py-1.5 pl-1 pr-3"} ${isActive ? "bg-[#13202B]" : "bg-transparent"}`} style={{ borderColor: isActive ? color : "#203649" }}>
            <div className="relative grid place-items-center">
              {index < ctrlNames.length - 1 ? <span className="absolute top-4 h-6 w-px bg-grid" /> : null}
              <span className={`${compact ? "h-3 w-3" : "h-4 w-4"} z-10 rounded-sm border`} style={{ backgroundColor: isActive || isDone ? color : "#0D1823", borderColor: color }} />
            </div>
            <span className={`${compact ? "text-[11px]" : "text-sm"} font-black uppercase ${isActive ? "text-[#DCE7EF]" : "text-[#AFC4D8]"}`}>{name}</span>
            <span className={`${compact ? "text-[9px]" : "text-[10px]"} font-black uppercase`} style={{ color }}>
              {isActive ? (isFault ? "TRIP" : "ACTIVE") : isDone ? "DONE" : blocked ? "BLOCK" : "WAIT"}
            </span>
          </div>
        );
      })}
    </div>
  );
}
