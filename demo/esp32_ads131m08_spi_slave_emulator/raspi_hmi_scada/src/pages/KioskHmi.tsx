import { useEffect, useState } from "react";
import { Menu, Settings } from "lucide-react";
import { ControlBar } from "../components/ControlBar";
import { MeasurementCard } from "../components/MeasurementCard";
import { OperationDrawer } from "../components/OperationDrawer";
import { OscillographyPanel } from "../components/OscillographyPanel";
import { Panel } from "../components/Panel";
import { PlantPanel } from "../components/PlantPanel";
import { ProtectionTable } from "../components/ProtectionTable";
import { Sidebar, type NavTarget } from "../components/Sidebar";
import { StateLeds } from "../components/StateLeds";
import { SyncPanel } from "../components/SyncPanel";
import { SystemSynoptic } from "../components/SystemSynoptic";
import { TrendPanel } from "../components/TrendPanel";
import { useTelemetry } from "../hooks/useTelemetry";
import { averageCurrent, averageVoltage, ctrlName, faultName, loadAngle, statusColor } from "../utils/domain";

export function KioskHmi() {
  const { state, status, history } = useTelemetry();
  const t = state?.telemetry ?? {};
  const now = new Date();
  const [activeView, setActiveView] = useHashView("resumen");

  return (
    <div className="vector-shell h-screen w-screen overflow-hidden text-slate-100">
      <div className="h-full w-full overflow-hidden border border-grid bg-[#050b0f]/95">
        <header className="grid h-[44px] grid-cols-[56px_1fr_190px_180px_56px] items-center gap-2 border-b border-grid px-2">
          <button onClick={() => window.location.assign("/scada#resumen")} className="grid h-9 place-items-center rounded-sm border border-grid bg-panel">
            <Menu className="h-7 w-7" />
          </button>
          <div className="brand-chip flex h-9 items-center gap-3 rounded-sm px-3">
            <img className="brand-logo h-7 w-7" src="/branding/logo_dark.png" alt="SIEZA" />
            <div className="min-w-0">
              <div className="text-xl font-black uppercase leading-none tracking-wide">Nexus Sync</div>
              <div className="truncate text-[11px] font-bold uppercase text-[#AFC4D8]">Motor sincrono / monitoreo local</div>
            </div>
          </div>
          <div className="text-center">
            <div className="text-[10px] font-black uppercase text-[#AFC4D8]">Estado global</div>
            <div className={`flex items-center justify-center gap-2 text-xl font-black ${statusColor(t)}`}>
              <span className="h-2.5 w-2.5 rounded-full bg-current" />
              {ctrlName(t)}
            </div>
          </div>
          <div className="grid grid-cols-2 gap-2 text-[11px]">
            <b>{now.toLocaleTimeString()}</b>
            <b className={status.connected ? "text-ok" : "text-bad"}>COM {status.connected ? "OK" : "ERR"}</b>
            <span>Modo Auto</span>
            <span>Alarm {(t.fault ?? 0) === 1 ? "1" : "0"}</span>
          </div>
          <button onClick={() => window.location.assign("/scada#configuracion")} className="grid h-9 place-items-center rounded-sm border border-grid bg-panel">
            <Settings className="h-6 w-6" />
          </button>
        </header>
        <div className="flex h-[calc(100vh-44px)]">
          <Sidebar compact active={activeView} onNavigate={setActiveView} />
          <main className="grid min-w-0 flex-1 grid-rows-[1fr_64px] gap-1.5 p-1.5">
            <section className="min-h-0 overflow-hidden">
              {activeView === "resumen" ? (
                <div className="grid h-full grid-cols-[1fr_250px] grid-rows-[280px_minmax(0,1fr)] gap-2">
                  <Panel title="Diagrama del sistema" className="p-2">
                    <div className="grid h-full grid-rows-[1fr_auto] gap-1.5">
                      <SystemSynoptic telemetry={t} compact />
                      <OperationDrawer telemetry={t} compact />
                    </div>
                  </Panel>
                  <Panel title="Estados" className="overflow-hidden"><StateLeds telemetry={t} compact /></Panel>
                  <Panel title="Planta" className="overflow-hidden"><PlantPanel state={state} compact /></Panel>
                  <Panel title="Sincronismo" className="overflow-hidden"><SyncPanel telemetry={t} compact /></Panel>
                </div>
              ) : null}
              {activeView === "mediciones" ? (
                <Panel title="Mediciones principales" className="h-full overflow-hidden p-2">
                  <div className="grid h-full grid-cols-4 grid-rows-2 gap-2">
                    <MeasurementCard compact title="Voltaje linea" value={averageVoltage(t)} unit="V" min={0} max={160} nominal={120} warn={130} danger={145} history={history} trendKey="va" />
                    <MeasurementCard compact title="Corriente estator" value={averageCurrent(t)} unit="A" min={0} max={10} nominal={5} warn={7} danger={8.5} history={history} trendKey="ia" />
                    <MeasurementCard compact title="Potencia activa" value={t.p ?? 0} unit="W" min={-500} max={2500} nominal={1600} warn={1800} danger={2200} history={history} trendKey="p" />
                    <MeasurementCard compact title="Potencia reactiva" value={t.q ?? 0} unit="var" min={0} max={1800} nominal={850} warn={1300} danger={1600} history={history} trendKey="q" />
                    <MeasurementCard compact title="Factor potencia" value={t.pf ?? 0} unit="cos" min={-1} max={1} nominal={0.9} history={history} trendKey="pf" />
                    <MeasurementCard compact title="Frecuencia" value={t.f ?? 0} unit="Hz" min={58} max={62} nominal={60} warn={61.5} danger={62} history={history} trendKey="f" />
                    <MeasurementCard compact title="Angulo carga" value={loadAngle(t)} unit="deg" min={0} max={120} nominal={30} warn={70} danger={95} history={history} trendKey="angle" />
                    <MeasurementCard compact title="Corriente campo DC" value={t.fielda ?? 0} unit="A" min={0} max={8} nominal={5} warn={6} danger={7.5} history={history} trendKey="fielda" />
                  </div>
                </Panel>
              ) : null}
              {activeView === "tendencias" ? <Panel title="Tendencias" className="h-full"><TrendPanel history={history} /></Panel> : null}
              {activeView === "oscilografia" ? <Panel title="Oscilografia COMTRADE" className="h-full overflow-hidden"><OscillographyPanel compact /></Panel> : null}
              {activeView === "sincronismo" ? <Panel title="Sincronismo" className="h-full"><SyncPanel telemetry={t} /></Panel> : null}
              {activeView === "alarmas" ? <Panel title="Protecciones" className="h-full overflow-hidden text-xs"><ProtectionTable telemetry={t} mode="trips" /></Panel> : null}
              {activeView === "eventos" ? <Panel title="Eventos" className="h-full"><ProtectionTable telemetry={t} mode="permissives" /></Panel> : null}
              {activeView === "diagnostico" ? <Panel title="Diagnostico de planta" className="h-full overflow-hidden"><PlantPanel state={state} /></Panel> : null}
              {activeView === "configuracion" ? <Panel title="Operacion y escenarios" className="h-full p-3"><ControlBar /></Panel> : null}
            </section>
            <footer className="grid grid-cols-4 items-center gap-2 rounded-sm border border-grid bg-panel px-3">
              <FooterItem label="Modo" value="Automatico" status="ok" />
              <FooterItem label="Comunicacion" value={status.connected ? "OK" : "ERROR"} status={status.connected ? "ok" : "bad"} />
              <FooterItem label="Control excitacion" value={(t.fs ?? 0) === 1 ? "Activo" : "Listo"} status={(t.fs ?? 0) === 1 ? "ok" : "warn"} />
              <FooterItem label="Falla activa" value={faultName(t)} status={(t.fault ?? 0) === 1 ? "bad" : "ok"} />
            </footer>
          </main>
        </div>
      </div>
    </div>
  );
}

function useHashView(defaultView: NavTarget) {
  const [view, setView] = useState<NavTarget>(() => {
    const hash = window.location.hash.replace("#", "") as NavTarget;
    return hash || defaultView;
  });

  useEffect(() => {
    function onHash() {
      const hash = window.location.hash.replace("#", "") as NavTarget;
      if (hash) setView(hash);
    }
    window.addEventListener("hashchange", onHash);
    return () => window.removeEventListener("hashchange", onHash);
  }, []);

  return [view, setView] as const;
}

function FooterItem({ label, value, status }: { label: string; value: string; status: "ok" | "warn" | "bad" }) {
  const color = status === "bad" ? "text-bad" : status === "warn" ? "text-warn" : "text-ok";
  return (
    <div className="min-w-0">
      <div className="truncate text-[11px] font-bold uppercase text-slate-200">{label}</div>
      <div className={`truncate font-mono text-lg font-black ${color}`}>{value}</div>
    </div>
  );
}
