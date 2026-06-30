import { StrictMode } from "react";
import { createRoot } from "react-dom/client";
import { App } from "./App";
import { bridge } from "./bridge";
import "./styles.css";

const root = document.getElementById("root");
if (!root) throw new Error("#root not found");

// The C++ Creator view toggles visibility via window.hmpCreator.open/close and expects a
// creator:ready event once mounted. App self-gates on that (renders nothing until opened).
createRoot(root).render(
  <StrictMode>
    <App />
  </StrictMode>,
);

bridge.ready();
