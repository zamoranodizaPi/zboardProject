import { useEffect, useState } from "react";
import { Menu } from "lucide-react";
import { ControlBar } from "../components/ControlBar";
import { EventsTable } from "../components/EventsTable";
import { KpiCard } from "../components/KpiCard";
import { MeasurementCard } from "../components/MeasurementCard";
import { Panel } from "../components/Panel";
import { OperationDrawer } from "../components/OperationDrawer";
import { OscillographyPanel } from "../components/OscillographyPanel";
import { PlantPanel } from "../components/PlantPanel";
import { ProtectionTable } from "../components/ProtectionTable";
import { Sidebar, type NavTarget } from "../components/Sidebar";
import { StateLeds } from "../components/StateLeds";
import { SyncPanel } from "../components/SyncPanel";
import { SystemSynoptic } from "../components/SystemSynoptic";
import { TrendPanel } from "../components/TrendPanel";
import { useTelemetry } from "../hooks/useTelemetry";
import { averageCurrent, averageVoltage, ctrlName, faultName, loadAngle, n, statusColor } from "../utils/domain";

export function ScadaWeb() {
  const { state, status, history } = useTelemetry();
  const t = state?.telemetry ?? {};
  const now = new Date();
  const [activeView, setActiveView] = useHashView("resumen");

  return (
    <div className="vector-shell h-screen overflow-hidden text-slate-100">
      <div className="flex h-screen">
        <Sidebar active={activeView} onNavigate={setActiveView} />
        <div className="min-w-0 flex-1">
          <header className="grid min-h-[56px] grid-cols-[1fr_auto_auto] items-center gap-4 border-b border-grid bg-base px-4">
            <div className="flex w-fit items-center gap-3">
              <img className="brand-logo h-9 w-9" src="/branding/logo_dark.png" alt="SIEZA" />
              <div>
                <h1 className="text-2xl font-black uppercase leading-none tracking-wide">Nexus Sync</h1>
                <p className="text-xs uppercase text-[#AFC4D8]">Sistema de monitoreo y control de motor sincrono</p>
              </div>
            </div>
            <div className="grid grid-cols-5 gap-5 text-xs">
              <HeaderDatum label="Estado" value={ctrlName(t)} className={statusColor(t)} />
              <HeaderDatum label="Hora" value={now.toLocaleTimeString()} />
              <HeaderDatum label="Modo" value="Automatico" className="text-ok" />
              <HeaderDatum label="Usuario" value="Operador" />
              <HeaderDatum label="Comunicacion" value={status.connected ? "OK" : "ERROR"} className={status.connected ? "text-ok" : "text-bad"} />
            </div>
            <button onClick={() => window.location.assign("/kiosk")} className="rounded-sm border border-grid bg-panel p-2" title="Abrir kiosko"><Menu /></button>
          </header>

          <main className="h-[calc(100vh-104px)] overflow-hidden p-3">
            {activeView === "resumen" ? (
              <div className="grid h-full grid-rows-[88px_1fr] gap-3">
                <div className="grid grid-cols-6 gap-2">
                  <KpiCard label="Estado" value={ctrlName(t)} status={(t.fault ?? 0) === 1 ? "bad" : (t.ctrl ?? 0) === 6 ? "ok" : "warn"} trend={history} />
                  <KpiCard label="Potencia" value={n(t.p, 1)} unit="W" status="ok" trend={history} trendKey="p" />
                  <KpiCard label="FP" value={n(t.pf, 3)} status={(t.pf ?? 0) > 0.75 ? "ok" : "warn"} trend={history} trendKey="pf" />
                  <KpiCard label="Frecuencia" value={n(t.f, 3)} unit="Hz" status="ok" trend={history} trendKey="f" />
                  <KpiCard label="Carga" value={n(t.load, 1)} unit="%" status="ok" />
                  <KpiCard label="Alarmas" value={faultName(t)} status={(t.fault ?? 0) === 1 ? "bad" : "ok"} />
                </div>
                <div className="grid min-h-0 grid-cols-[1fr_330px] gap-3">
                  <Panel title="Diagrama del sistema" className="p-2">
                    <div className="grid h-full grid-rows-[1fr_auto] gap-2">
                      <SystemSynoptic telemetry={t} />
                      <OperationDrawer telemetry={t} />
                    </div>
                  </Panel>
                  <div className="grid min-h-0 grid-rows-[1fr_1fr] gap-3">
                    <Panel title="Estado del sistema"><StateLeds telemetry={t} /></Panel>
                    <Panel title="Planta simulada"><PlantPanel state={state} compact /></Panel>
                  </div>
                </div>
              </div>
            ) : null}
            {activeView === "mediciones" ? (
              <Panel title="Mediciones principales" className="h-full">
                <div className="grid grid-cols-4 gap-2">
                  <MeasurementCard title="Voltaje linea L-L" value={averageVoltage(t)} unit="V" min={0} max={160} nominal={120} warn={130} danger={145} history={history} trendKey="va" />
                  <MeasurementCard title="Corriente estator" value={averageCurrent(t)} unit="A" min={0} max={10} nominal={5} warn={7} danger={8.5} history={history} trendKey="ia" />
                  <MeasurementCard title="Potencia activa" value={t.p ?? 0} unit="W" min={-500} max={2500} nominal={1600} warn={1800} danger={2200} history={history} trendKey="p" />
                  <MeasurementCard title="Potencia reactiva" value={t.q ?? 0} unit="var" min={0} max={1800} nominal={850} warn={1300} danger={1600} history={history} trendKey="q" />
                  <MeasurementCard title="Factor de potencia" value={t.pf ?? 0} unit="cos phi" min={-1} max={1} nominal={0.9} history={history} trendKey="pf" />
                  <MeasurementCard title="Frecuencia" value={t.f ?? 0} unit="Hz" min={58} max={62} nominal={60} warn={61.5} danger={62} history={history} trendKey="f" />
                  <MeasurementCard title="Angulo de carga" value={loadAngle(t)} unit="deg" min={0} max={120} nominal={30} warn={70} danger={95} history={history} trendKey="angle" />
                  <MeasurementCard title="Corriente campo DC" value={t.fielda ?? 0} unit="A" min={0} max={8} nominal={5} warn={6} danger={7.5} history={history} trendKey="fielda" />
                </div>
              </Panel>
            ) : null}
            {activeView === "tendencias" ? <Panel title="Tendencias en tiempo real" className="h-full"><TrendPanel history={history} /></Panel> : null}
            {activeView === "oscilografia" ? <Panel title="Oscilografia COMTRADE" className="h-full overflow-hidden"><OscillographyPanel /></Panel> : null}
            {activeView === "sincronismo" ? <Panel title="Sincronismo" className="h-full"><SyncPanel telemetry={t} /></Panel> : null}
            {activeView === "alarmas" ? <div className="grid h-full grid-cols-2 gap-3"><Panel title="Permissives"><ProtectionTable telemetry={t} mode="permissives" /></Panel><Panel title="Electrical trips"><ProtectionTable telemetry={t} mode="trips" /></Panel></div> : null}
            {activeView === "eventos" ? <Panel title="Alarmas y eventos" className="h-full"><EventsTable telemetry={t} /></Panel> : null}
            {activeView === "diagnostico" ? <Panel title="Diagnostico de planta" className="h-full"><PlantPanel state={state} /></Panel> : null}
            {activeView === "configuracion" ? (
              <div className="grid h-full grid-cols-[1fr_430px] gap-3">
                <Panel title="Operacion"><ControlBar /></Panel>
                <Panel title="Escenarios de prueba">
                  <div className="grid gap-2 text-sm text-[#AFC4D8]">
                    <span>Los escenarios PLANT modifican feedbacks fisicos hacia FakeFPGA.</span>
                    <span>Use ACK/RESET despues de una falla para volver a READY.</span>
                  </div>
                </Panel>
              </div>
            ) : null}
          </main>

          <footer className="grid grid-cols-5 border-t border-grid bg-panel px-5 py-3 text-sm">
            <span><b>Sistema</b> NEXUS SYNC DIGITAL</span>
            <span><b>Modo</b> <span className="text-ok">AUTOMATICO</span></span>
            <span><b>Estado</b> <span className={statusColor(t)}>{ctrlName(t)}</span></span>
            <span><b>Comunicacion</b> <span className={status.connected ? "text-ok" : "text-bad"}>{state?.serial_port ?? "--"}</span></span>
            <span><b>PLC / MCU</b> ESP32</span>
          </footer>
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

function HeaderDatum({ label, value, className = "text-[#DCE7EF]" }: { label: string; value: string; className?: string }) {
  return (
    <div className="min-w-0">
      <span className="block text-[10px] font-black uppercase tracking-[0.08em] text-[#6E879B]">{label}</span>
      <b className={`block truncate font-mono text-sm ${className}`}>{value}</b>
    </div>
  );
}
