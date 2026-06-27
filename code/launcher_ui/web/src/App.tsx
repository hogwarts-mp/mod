import { useState, type CSSProperties } from "react";
import { C, hair } from "./theme";
import { isHost } from "./bridge";
import type { PageKey } from "./data";
import { TitleBar } from "./components/TitleBar";
import { Sidebar } from "./components/Sidebar";
import { ServersPage } from "./pages/Servers";
import { DirectConnectPage } from "./pages/DirectConnect";
import { NewsPage } from "./pages/News";
import { SettingsPage } from "./pages/Settings";

export function App() {
  const [page, setPage] = useState<PageKey>("servers");

  // In the launcher the OS window IS the frame, so fill it edge-to-edge. In a plain
  // browser, show the centered 1280x840 card with shadow as a design preview.
  const outerStyle: CSSProperties = isHost
    ? { width: "100vw", height: "100vh", display: "flex", overflow: "hidden" }
    : { minHeight: "100vh", boxSizing: "border-box", padding: 48, display: "flex", alignItems: "center", justifyContent: "center" };

  const frameStyle: CSSProperties = isHost
    ? { width: "100%", height: "100%", flex: "none", background: C.window, overflow: "hidden", display: "flex", flexDirection: "column" }
    : { width: 1280, height: 840, flex: "none", background: C.window, borderRadius: 5, boxShadow: "0 30px 80px rgba(0,0,0,.6)", overflow: "hidden", border: hair(0.14), display: "flex", flexDirection: "column" };

  return (
    <div style={outerStyle}>
      {/* Window frame (OS window when hosted, preview card in a browser). */}
      <div style={frameStyle}>
        <TitleBar />

        <div style={{ flex: 1, minHeight: 0, display: "flex" }}>
          <Sidebar page={page} onNavigate={setPage} />

          {page === "servers" && <ServersPage />}
          {page === "direct" && <DirectConnectPage />}
          {page === "news" && <NewsPage />}
          {page === "settings" && <SettingsPage />}
        </div>
      </div>
    </div>
  );
}
