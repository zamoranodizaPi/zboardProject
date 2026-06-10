# Nexus Sync HMI / SCADA

Dos interfaces React + TypeScript + Vite que consumen el backend actual del simulador:

- `/kiosk`: HMI local para pantalla tactil 1024x600 sin scroll.
- `/scada`: SCADA web responsive para supervision avanzada.

Ambas usan los mismos datos actuales de `/api/state`, `/api/command` y `/api/ads-control`.

## Ejecutar

```bash
npm install
npm run dev
```

En Raspberry con Chromium:

```bash
chromium --kiosk http://127.0.0.1:5173/kiosk
```

## Build

```bash
npm run build
```

## Arquitectura

- `providers/IDataProvider.ts`: contrato comun.
- `providers/SimulatorProvider.ts`: adaptador HTTP del simulador actual.
- `providers/ModbusProvider.ts`: placeholder para integracion futura.
- `hooks/useTelemetry.ts`: polling, loading, error y reconexion.
- `pages/KioskHmi.tsx`: HMI local.
- `pages/ScadaWeb.tsx`: SCADA web.
