import { defineConfig } from "vite";
import react from "@vitejs/plugin-react";
import { viteSingleFile } from "vite-plugin-singlefile";

// The in-game creator view loads this either from the dev server (npm run dev /
// pixi run creator-ui-dev -> http://localhost:5173) or, shipped, as ONE self-contained
// HTML file hosted on Pages next to hud.html/chat.html (viteSingleFile inlines the JS+CSS,
// so publishing is a single-file copy -> docs/ui/creator.html, same as the other UIs).
export default defineConfig({
  plugins: [react(), viteSingleFile()],
  base: "./",
  build: {
    outDir: "dist",
    emptyOutDir: true,
    target: "chrome120", // framework CEF ships modern Chromium
  },
  server: {
    port: 5173,
    strictPort: true,
  },
});
