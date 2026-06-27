import { defineConfig } from "vite";
import react from "@vitejs/plugin-react";

// Built output is served by the native CEF host as static files (file:// or a custom
// scheme), NOT from a web server root, so asset URLs must be relative -> base: "./".
// Output goes to launcher_ui/dist (sibling of web/), which the C++ host stages next to
// the launcher exe.
export default defineConfig({
  plugins: [react()],
  base: "./",
  build: {
    outDir: "../dist",
    emptyOutDir: true,
    target: "chrome120", // CEF 145 ships modern Chromium; no legacy transpile needed.
  },
  server: {
    port: 5173,
    strictPort: true,
  },
});
