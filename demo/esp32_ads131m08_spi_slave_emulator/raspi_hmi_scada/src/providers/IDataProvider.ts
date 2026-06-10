import type { AdsChannelConfig, AdsProfile, BackendState, CommandResult } from "../types/telemetry";

export interface IDataProvider {
  getState(signal?: AbortSignal): Promise<BackendState>;
  sendCommand(command: string): Promise<CommandResult>;
  runAds(profile: AdsProfile, channels?: AdsChannelConfig[]): Promise<CommandResult>;
  stopAds(): Promise<CommandResult>;
}
