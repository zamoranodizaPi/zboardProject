import { ChevronDown, ChevronUp } from "lucide-react";
import { useState } from "react";
import { ControlBar } from "./ControlBar";
import type { Telemetry } from "../types/telemetry";
import { ctrlName, faultName, statusColor } from "../utils/domain";

export function OperationDrawer({ telemetry = {}, compact = false }: { telemetry?: Telemetry; compact?: boolean }) {
  const [open, setOpen] = useState(false);
  const Icon = open ? ChevronDown : ChevronUp;

  return (
    <div className={`rounded-sm border border-grid bg-[#0D1823] ${compact ? "p-2" : "p-3"}`}>
      <button
        className="flex w-full items-center justify-between text-left"
        onClick={() => setOpen((value) => !value)}
        aria-expanded={open}
      >
        <div className="flex min-w-0 items-center gap-3">
          <span className="h-2 w-2 bg-[#38BDF8]" />
          <div>
            <div className="text-[11px] font-black uppercase tracking-[0.12em] text-[#AFC4D8]">Operacion</div>
            <div className={`font-mono ${compact ? "text-sm" : "text-base"} font-black ${statusColor(telemetry)}`}>
              {ctrlName(telemetry)}
            </div>
          </div>
        </div>
        <div className="flex items-center gap-3">
          <span className={`hidden font-mono text-xs font-black uppercase sm:inline ${(telemetry.fault ?? 0) === 1 ? "text-bad" : "text-ok"}`}>
            {faultName(telemetry)}
          </span>
          <Icon className="h-5 w-5 text-[#AFC4D8]" />
        </div>
      </button>
      {open ? (
        <div className={`mt-3 border-t border-grid pt-3 ${compact ? "max-h-[210px] overflow-hidden" : ""}`}>
          <ControlBar compact={compact} />
        </div>
      ) : null}
    </div>
  );
}
