import type { IDataProvider } from "./IDataProvider";
import type { AdsChannelConfig, AdsProfile, BackendState, CommandResult } from "../types/telemetry";

export class ModbusProvider implements IDataProvider {
  async getState(): Promise<BackendState> {
    throw new Error("ModbusProvider pending integration");
  }

  async sendCommand(_command: string): Promise<CommandResult> {
    throw new Error("ModbusProvider pending integration");
  }

  async runAds(_profile: AdsProfile, _channels: AdsChannelConfig[] = []): Promise<CommandResult> {
    throw new Error("ModbusProvider pending integration");
  }

  async stopAds(): Promise<CommandResult> {
    throw new Error("ModbusProvider pending integration");
  }
}
