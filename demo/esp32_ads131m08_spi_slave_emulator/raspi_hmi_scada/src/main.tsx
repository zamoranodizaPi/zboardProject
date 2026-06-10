import React from "react";
import ReactDOM from "react-dom/client";
import { KioskHmi } from "./pages/KioskHmi";
import { ScadaWeb } from "./pages/ScadaWeb";
import "./styles.css";

function App() {
  const path = window.location.pathname.toLowerCase();
  if (path.startsWith("/kiosk")) return <KioskHmi />;
  return <ScadaWeb />;
}

ReactDOM.createRoot(document.getElementById("root")!).render(
  <React.StrictMode>
    <App />
  </React.StrictMode>
);
