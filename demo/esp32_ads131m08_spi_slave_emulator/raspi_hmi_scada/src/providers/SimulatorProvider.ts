import type { IDataProvider } from "./IDataProvider";
import type { AdsChannelConfig, AdsProfile, BackendState, CommandResult } from "../types/telemetry";

async function postJson<T>(url: string, payload: unknown): Promise<T> {
  const response = await fetch(url, {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify(payload)
  });
  if (!response.ok) throw new Error(`${response.status} ${response.statusText}`);
  return response.json() as Promise<T>;
}

export class SimulatorProvider implements IDataProvider {
  async getState(signal?: AbortSignal): Promise<BackendState> {
    const response = await fetch("/api/state", { signal });
    if (!response.ok) throw new Error(`${response.status} ${response.statusText}`);
    return response.json() as Promise<BackendState>;
  }

  sendCommand(command: string): Promise<CommandResult> {
    return postJson<CommandResult>("/api/command", { command });
  }

  runAds(profile: AdsProfile, channels: AdsChannelConfig[] = []): Promise<CommandResult> {
    return postJson<CommandResult>("/api/ads-control", { action: "run", profile, channels });
  }

  stopAds(): Promise<CommandResult> {
    return postJson<CommandResult>("/api/ads-control", { action: "stop" });
  }
}
