import type { IDataProvider } from "./IDataProvider";
import { SimulatorProvider } from "./SimulatorProvider";

let provider: IDataProvider | undefined;

export function getDataProvider(): IDataProvider {
  if (!provider) provider = new SimulatorProvider();
  return provider;
}
