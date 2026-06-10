import { Activity, Bell, Gauge, Home, ListChecks, Settings, Shield, Target, TrendingUp } from "lucide-react";

const items = [
  [Home, "Resumen", "resumen"],
  [Gauge, "Mediciones", "mediciones"],
  [TrendingUp, "Tendencias", "tendencias"],
  [Activity, "Oscilografia", "oscilografia"],
  [Target, "Sincronismo", "sincronismo"],
  [Bell, "Alarmas", "alarmas"],
  [ListChecks, "Eventos", "eventos"],
  [Shield, "Diagnostico", "diagnostico"],
  [Settings, "Configuracion", "configuracion"]
] as const;

export type NavTarget = (typeof items)[number][2];

export function Sidebar({
  compact = false,
  active = "resumen",
  onNavigate
}: {
  compact?: boolean;
  active?: string;
  onNavigate?: (target: NavTarget) => void;
}) {
  function navigate(target: string) {
    if (onNavigate) {
      onNavigate(target as NavTarget);
      window.history.replaceState(null, "", `#${target}`);
      return;
    }
    const element = document.getElementById(target);
    if (element) {
      element.scrollIntoView({ behavior: "smooth", block: "start" });
      window.history.replaceState(null, "", `#${target}`);
      return;
    }
    window.location.hash = target;
  }

  return (
    <nav className={`${compact ? "w-[70px]" : "w-[88px]"} shrink-0 border-r border-grid bg-[#061016]/95`}>
      <div className="flex h-full flex-col">
        <div className={`brand-chip m-2 grid ${compact ? "h-10" : "h-14"} place-items-center rounded`}>
          <img className={`brand-logo ${compact ? "h-7 w-7" : "h-10 w-10"}`} src="/branding/logo_dark.png" alt="SIEZA" />
        </div>
        {items.map(([Icon, label, target], index) => (
          <button key={label} onClick={() => navigate(target)} className={`mx-1 my-0.5 flex ${compact ? "h-[46px]" : "h-[70px]"} flex-col items-center justify-center gap-1 rounded border border-grid text-white ${active === target || (!active && index === 0) ? "bg-cyan/20 text-cyan" : "bg-panel"}`}>
            <Icon className={compact ? "h-5 w-5" : "h-7 w-7"} />
            <span className="text-[10px] font-bold uppercase">{compact ? label.slice(0, 4) : label}</span>
          </button>
        ))}
      </div>
    </nav>
  );
}
