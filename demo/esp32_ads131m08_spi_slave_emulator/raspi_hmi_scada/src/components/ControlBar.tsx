import { useState, type ChangeEvent } from "react";
import { getDataProvider } from "../providers/providerFactory";
import type { AdsProfile } from "../types/telemetry";

const profiles: AdsProfile[] = ["GRID_NORMAL", "START_PROFILE", "PULLOUT", "LOW_PF", "VOLTAGE_SAG", "UNBALANCE", "PHASE_LOSS_A", "NO_SIGNAL"];
const plantScenarios = [
  ["NORMAL", "Normal"],
  ["NO_DISCHARGE", "Sin descarga"],
  ["NO_FIELD", "Sin campo"],
  ["THERMAL_TRIP", "Termico"],
  ["PULLOUT", "Pullout"],
  ["GATE_FAIL", "Gate fail"],
  ["SCR_OPEN", "SCR abierto"],
  ["SCR_SHORT", "SCR corto"]
] as const;

export function ControlBar({ compact = false }: { compact?: boolean }) {
  const provider = getDataProvider();
  const [pending, setPending] = useState<{ command: string; label: string; tone: "start" | "stop" | "reset" | "test" } | null>(null);

  async function send(command: string) {
    if (["STARTSEQ", "STOPSEQ", "RESET", "HAPPY_PATH"].includes(command)) {
      const label = command === "STARTSEQ" ? "START" : command === "STOPSEQ" ? "STOP" : command === "HAPPY_PATH" ? "HAPPY PATH" : "RESET";
      const tone = command === "STARTSEQ" ? "start" : command === "STOPSEQ" ? "stop" : command === "HAPPY_PATH" ? "test" : "reset";
      setPending({ command, label, tone });
      return;
    }
    await provider.sendCommand(command);
  }

  async function confirmPending() {
    if (!pending) return;
    const command = pending.command;
    setPending(null);
    await provider.sendCommand(command);
  }

  async function runProfile(event: ChangeEvent<HTMLSelectElement>) {
    await provider.runAds(event.target.value as AdsProfile);
  }

  async function runPlantScenario(event: ChangeEvent<HTMLSelectElement>) {
    await provider.sendCommand(`PLANT SCENARIO ${event.target.value}`);
  }

  async function setPlantScenario(value: string) {
    await provider.sendCommand(`PLANT SCENARIO ${value}`);
  }

  return (
    <>
      <div className={`grid gap-2 ${compact ? "grid-cols-5 text-xs" : "grid-cols-2"}`}>
        <button className="industrial-button h-11 rounded-sm border border-[#166534] bg-[#052e16] px-4 font-black text-[#22C55E]" onClick={() => send("STARTSEQ")}>START</button>
        <button className="industrial-button h-11 rounded-sm border border-[#7f1d1d] bg-[#3f1111] px-4 font-black text-[#EF4444]" onClick={() => send("STOPSEQ")}>STOP</button>
        <button className="industrial-button h-11 rounded-sm border border-grid bg-panel2 px-4 font-black text-[#AFC4D8]" onClick={() => send("ACK")}>ACK</button>
        <button className="industrial-button h-11 rounded-sm border border-[#1d4ed8] bg-[#0b1f45] px-4 font-black text-[#3B82F6]" onClick={() => send("RESET")}>RESET</button>
        <button className={`industrial-button h-11 rounded-sm border border-cyan/60 bg-cyan/10 px-4 font-black text-cyan ${compact ? "col-span-5" : "col-span-2"}`} onClick={() => send("HAPPY_PATH")}>
          HAPPY PATH START / STOP
        </button>
        <select className={`h-11 rounded-sm border border-grid bg-panel2 px-3 font-bold text-[#DCE7EF] ${compact ? "col-span-1" : "col-span-2"}`} onChange={runProfile} defaultValue="GRID_NORMAL">
          {profiles.map((p) => <option key={p} value={p}>{p}</option>)}
        </select>
        <select className={`h-11 rounded-sm border border-grid bg-panel2 px-3 font-bold text-[#DCE7EF] ${compact ? "col-span-5" : "col-span-2"}`} onChange={runPlantScenario} defaultValue="NORMAL">
          {plantScenarios.map(([value, label]) => <option key={value} value={value}>PLANT {label}</option>)}
        </select>
        {!compact ? (
          <div className="col-span-2 grid grid-cols-5 gap-2">
            {plantScenarios.map(([value, label]) => (
              <button key={value} className="industrial-button h-10 rounded-sm border border-grid bg-panel2 px-2 text-xs font-black uppercase text-[#AFC4D8]" onClick={() => setPlantScenario(value)}>
                {label}
              </button>
            ))}
          </div>
        ) : null}
      </div>
      {pending ? (
        <div className="fixed inset-0 z-50 grid place-items-center bg-black/70">
          <div className="w-[360px] rounded-sm border border-grid bg-[#0D1823] p-5 shadow-2xl">
            <div className="text-xs font-black uppercase tracking-[0.12em] text-[#6E879B]">Confirmacion operacional</div>
            <div className={`mt-2 text-3xl font-black ${pending.tone === "stop" ? "text-bad" : pending.tone === "start" ? "text-ok" : pending.tone === "test" ? "text-cyan" : "text-[#3B82F6]"}`}>{pending.label}</div>
            <p className="mt-3 text-sm text-[#AFC4D8]">
              {pending.tone === "test"
                ? "Ejecutara arranque, aplicacion de campo, sincronismo, marcha estable y paro limpio, generando oscilografia COMTRADE."
                : "Esta accion se enviara al controlador FakeFPGA y cambiara el estado de la secuencia."}
            </p>
            <div className="mt-5 grid grid-cols-2 gap-3">
              <button className="h-11 rounded-sm border border-grid bg-panel2 font-black text-[#AFC4D8]" onClick={() => setPending(null)}>CANCELAR</button>
              <button className="h-11 rounded-sm border border-[#1d4ed8] bg-[#0b1f45] font-black text-[#DCE7EF]" onClick={confirmPending}>CONFIRMAR</button>
            </div>
          </div>
        </div>
      ) : null}
    </>
  );
}
