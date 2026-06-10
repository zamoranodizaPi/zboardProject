import type { Telemetry } from "../types/telemetry";

const permissives: [string, string][] = [
  ["Voltage", "p_vok"],
  ["Frequency", "p_fok"],
  ["Balance", "p_bal"],
  ["Phase Sequence", "p_seq"],
  ["Signal Present", "p_sig"],
  ["Power Factor", "p_pf"]
];

const trips: [string, string][] = [
  ["Undervoltage", "t_uv"],
  ["Overvoltage", "t_ov"],
  ["Underfrequency", "t_uf"],
  ["Overfrequency", "t_of"],
  ["Phase Loss", "t_ploss"],
  ["Voltage Unbalance", "t_vunb"],
  ["Low Power Factor", "t_lpf"],
  ["Phase Sequence", "t_pseq"],
  ["Loss of Signal", "t_los"]
];

export function ProtectionTable({ telemetry, mode }: { telemetry: Telemetry; mode: "permissives" | "trips" }) {
  const rows = mode === "permissives" ? permissives : trips;
  return (
    <table className="w-full border-collapse text-sm">
      <tbody>
        {rows.map(([label, key]) => {
          const active = (telemetry[key] ?? 0) === 1;
          const ok = mode === "permissives" ? active : !active;
          return (
            <tr key={key} className="border-b border-grid/70">
              <td className="py-1.5 text-slate-200">{label}</td>
              <td className={`py-1.5 text-right font-black ${ok ? "text-ok" : "text-bad"}`}>
                {mode === "permissives" ? (active ? "OK" : "BLOCK") : (active ? "TRIP" : "CLEAR")}
              </td>
            </tr>
          );
        })}
      </tbody>
    </table>
  );
}
