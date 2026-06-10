import { useEffect } from "react";
import { create } from "zustand";
import { getDataProvider } from "../providers/providerFactory";
import type { BackendState, ProviderStatus, Telemetry, TrendPoint } from "../types/telemetry";

interface TelemetryStore {
  state?: BackendState;
  status: ProviderStatus;
  history: TrendPoint[];
  setState: (state: BackendState) => void;
  setError: (error: string) => void;
}

function pointFromTelemetry(t: Telemetry): TrendPoint {
  return {
    ts: Date.now(),
    va: t.va ?? 0,
    ia: t.ia ?? 0,
    f: t.f ?? 0,
    p: t.p ?? 0,
    q: t.q ?? 0,
    pf: t.pf ?? 0,
    angle: t.aia ?? 0,
    fielda: t.fielda ?? 0
  };
}

export const useTelemetryStore = create<TelemetryStore>((set) => ({
  status: { loading: true, connected: false, lastUpdateMs: 0 },
  history: [],
  setState: (state) =>
    set((current) => ({
      state,
      status: {
        loading: false,
        connected: state.age_ms < 1200,
        lastUpdateMs: Date.now()
      },
      history: [...current.history.slice(-1439), pointFromTelemetry(state.telemetry ?? {})]
    })),
  setError: (error) =>
    set({
      status: {
        loading: false,
        connected: false,
        error,
        lastUpdateMs: Date.now()
      }
    })
}));

export function useTelemetry() {
  const store = useTelemetryStore();

  useEffect(() => {
    const provider = getDataProvider();
    let stopped = false;
    let inFlight = false;

    async function tick() {
      if (inFlight) return;
      inFlight = true;
      const controller = new AbortController();
      try {
        const next = await provider.getState(controller.signal);
        if (!stopped) useTelemetryStore.getState().setState(next);
      } catch (error) {
        if (!stopped) useTelemetryStore.getState().setError(error instanceof Error ? error.message : "data error");
      } finally {
        inFlight = false;
      }
    }

    tick();
    const id = window.setInterval(tick, 750);
    return () => {
      stopped = true;
      window.clearInterval(id);
    };
  }, []);

  return store;
}
