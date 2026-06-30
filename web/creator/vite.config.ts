import { defineConfig } from "vite";
import react from "@vitejs/plugin-react";

// The in-game creator view loads this either from the dev server (npm run dev /
// pixi run creator-ui-dev -> http://localhost:5173) or, shipped, as a built bundle
// hosted on Pages. Relative asset URLs (base: "./") so it works under any path.
export default defineConfig({
  plugins: [react()],
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
