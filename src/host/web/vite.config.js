import { defineConfig } from "vite";
import react from "@vitejs/plugin-react";

// Built assets are served by FastAPI from app/static under the /static prefix.
// Dev server proxies the API (incl. the telemetry SSE stream) to uvicorn :8000.
export default defineConfig({
  plugins: [react()],
  base: "/static/",
  build: {
    outDir: "../app/static",
    emptyOutDir: true,
  },
  server: {
    port: 5173,
    proxy: {
      "/api": {
        target: "http://127.0.0.1:8000",
        changeOrigin: true,
      },
    },
  },
});
