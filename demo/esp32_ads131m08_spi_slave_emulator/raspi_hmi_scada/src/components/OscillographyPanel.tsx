import { useEffect, useState } from "react";
import type { ComtradeRecordMeta } from "../types/telemetry";

function fmtDate(value?: string) {
  if (!value) return "--";
  const date = new Date(value);
  return Number.isNaN(date.getTime()) ? value : date.toLocaleString();
}

function fmtRate(value?: number) {
  if (!value) return "--";
  if (value >= 1000) return `${(value / 1000).toFixed(1)} kS/s`;
  return `${value.toFixed(1)} S/s`;
}

function fileUrl(record: ComtradeRecordMeta, ext: "cfg" | "dat" | "json" | "zip") {
  if (ext === "cfg" && record.cfg) return record.cfg;
  if (ext === "dat" && record.dat) return record.dat;
  return `/comtrade/${record.id}.${ext}`;
}

export function OscillographyPanel({ compact = false }: { compact?: boolean }) {
  const [records, setRecords] = useState<ComtradeRecordMeta[]>([]);
  const [message, setMessage] = useState("Listo");
  const [loading, setLoading] = useState(false);

  async function loadList() {
    setLoading(true);
    try {
      const response = await fetch("/api/comtrade/list");
      const payload = await response.json();
      setRecords(payload.records ?? []);
      setMessage("Lista actualizada");
    } catch (error) {
      setMessage(error instanceof Error ? error.message : "Error leyendo registros");
    } finally {
      setLoading(false);
    }
  }

  async function trigger(action: "start" | "finish") {
    const response = await fetch("/api/comtrade/trigger", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ action, event: "MANUAL_OSCILLOGRAPHY" })
    });
    const payload = await response.json();
    setMessage(`${action.toUpperCase()} ${payload.response ?? ""}`);
    loadList();
  }

  useEffect(() => {
    loadList();
    const timer = window.setInterval(loadList, 15000);
    return () => window.clearInterval(timer);
  }, []);

  return (
    <div className="grid h-full grid-rows-[auto_1fr] gap-3 overflow-hidden">
      <div className="rounded-sm border border-grid bg-[#08131b] p-3">
        <div className="flex flex-wrap items-center justify-between gap-3">
          <div>
            <div className="text-[11px] font-black uppercase tracking-[0.08em] text-[#AFC4D8]">Registros COMTRADE</div>
            <div className="mt-1 text-[11px] text-[#6E879B]">
              Visualizacion desactivada. Descarga el paquete para abrirlo en tu software de PC.
            </div>
          </div>
          <div className="flex flex-wrap gap-2">
            <button className="rounded-sm border border-grid bg-panel2 px-3 py-2 text-xs font-black text-[#AFC4D8]" onClick={loadList}>
              {loading ? "..." : "REFRESH"}
            </button>
            {!compact ? (
              <>
                <button className="rounded-sm border border-[#166534] bg-[#052e16] px-3 py-2 text-xs font-black text-ok" onClick={() => trigger("start")}>REC</button>
                <button className="rounded-sm border border-[#854D0E] bg-[#3B2504] px-3 py-2 text-xs font-black text-warn" onClick={() => trigger("finish")}>STOP REC</button>
              </>
            ) : null}
          </div>
        </div>
        <div className="mt-2 truncate font-mono text-[10px] text-[#6E879B]">{message}</div>
      </div>

      <div className="min-h-0 overflow-auto rounded-sm border border-grid bg-[#050b0f]">
        <table className="w-full min-w-[760px] border-collapse text-left text-xs">
          <thead className="sticky top-0 bg-[#0D1823] text-[10px] uppercase tracking-[0.08em] text-[#6E879B]">
            <tr>
              <th className="border-b border-grid px-3 py-2">Evento</th>
              <th className="border-b border-grid px-3 py-2">Fecha</th>
              <th className="border-b border-grid px-3 py-2">Muestras</th>
              <th className="border-b border-grid px-3 py-2">Rate</th>
              <th className="border-b border-grid px-3 py-2">Estado</th>
              <th className="border-b border-grid px-3 py-2 text-right">Descarga</th>
            </tr>
          </thead>
          <tbody>
            {records.map((record) => (
              <tr key={record.id} className="border-b border-grid/70 text-[#DCE7EF]">
                <td className="px-3 py-2">
                  <div className="font-mono font-black">{record.event}</div>
                  <div className="truncate text-[10px] text-[#6E879B]">{record.id}</div>
                </td>
                <td className="px-3 py-2 text-[#AFC4D8]">{fmtDate(record.trigger ?? record.start)}</td>
                <td className="px-3 py-2 font-mono">{record.samples ?? "--"}</td>
                <td className="px-3 py-2 font-mono">{fmtRate(record.sample_rate)}</td>
                <td className="px-3 py-2">
                  <span className={`rounded-sm border px-2 py-1 text-[10px] font-black uppercase ${record.active ? "border-[#854D0E] bg-[#3B2504] text-warn" : "border-[#166534] bg-[#052e16] text-ok"}`}>
                    {record.active ? "REC" : record.reason ?? "LISTO"}
                  </span>
                </td>
                <td className="px-3 py-2">
                  {!record.active ? (
                    <div className="flex justify-end gap-2">
                      <a className="rounded-sm border border-cyan/60 bg-cyan/10 px-3 py-1 font-black text-cyan" href={fileUrl(record, "zip")} download>
                        ZIP
                      </a>
                      <a className="rounded-sm border border-grid bg-panel2 px-3 py-1 font-black text-[#AFC4D8]" href={fileUrl(record, "cfg")} download>
                        CFG
                      </a>
                      <a className="rounded-sm border border-grid bg-panel2 px-3 py-1 font-black text-[#AFC4D8]" href={fileUrl(record, "dat")} download>
                        DAT
                      </a>
                    </div>
                  ) : (
                    <span className="block text-right text-[#6E879B]">grabando</span>
                  )}
                </td>
              </tr>
            ))}
            {records.length === 0 ? (
              <tr>
                <td className="px-3 py-8 text-center text-[#6E879B]" colSpan={6}>Sin oscilografias registradas</td>
              </tr>
            ) : null}
          </tbody>
        </table>
      </div>
    </div>
  );
}
