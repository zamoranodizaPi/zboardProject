import type { Telemetry } from "../types/telemetry";
import { ctrlName, faultName, isFault } from "../utils/domain";
import { getDataProvider } from "../providers/providerFactory";

export function EventsTable({ telemetry }: { telemetry: Telemetry }) {
  const provider = getDataProvider();
  const now = new Date();
  const rows = [
    ["INFO", `Sistema en estado ${ctrlName(telemetry)}`],
    [(telemetry.fs ?? 0) === 1 ? "EVENTO" : "INFO", "Campo DC aplicado"],
    [(telemetry.fax ?? 0) === 1 ? "EVENTO" : "INFO", "Sincronismo completado"],
    [isFault(telemetry) ? "ALARMA" : "INFO", isFault(telemetry) ? faultName(telemetry) : "Protecciones OK"]
  ];
  return (
    <table className="w-full text-sm">
      <thead className="text-[11px] uppercase tracking-wide text-[#AFC4D8]">
        <tr><th className="py-1 text-left">Tiempo</th><th className="text-left">Severidad</th><th className="text-left">Mensaje</th><th className="text-right">Estado</th></tr>
      </thead>
      <tbody>
        {rows.map(([sev, msg], i) => (
          <tr key={`${sev}-${msg}`} className="border-b border-grid/70 hover:bg-panel2">
            <td className="py-1.5 font-mono text-[#AFC4D8]">{new Date(now.getTime() - i * 45000).toLocaleTimeString()}</td>
            <td className={sev === "ALARMA" ? "font-black text-bad" : sev === "EVENTO" ? "font-black text-warn" : "font-black text-ready"}>{sev}</td>
            <td className="text-[#DCE7EF]">{msg}</td>
            <td className="text-right"><button onClick={() => provider.sendCommand("ACK")} className="rounded-sm border border-grid px-2 py-0.5 text-[11px] font-black text-[#AFC4D8]">ACK</button></td>
          </tr>
        ))}
      </tbody>
    </table>
  );
}
