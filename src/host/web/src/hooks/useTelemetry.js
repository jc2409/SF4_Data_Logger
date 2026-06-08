import { useEffect, useRef, useState } from "react";

const EMPTY = {
  effect: 0,
  peak: 0,
  vga: 0,
  clip: 0,
  rx_err: 0,
  connected: false,
  port: null,
  // derived
  deviceConnected: false,
  link: "connecting", // "connecting" | "device" | "preview" | "down"
};

// Subscribes to /api/telemetry (SSE). Returns the latest telemetry plus a
// rolling history of peak values (for the level sparkline).
export function useTelemetry(historyLen = 120) {
  const [tel, setTel] = useState(EMPTY);
  const historyRef = useRef(new Array(historyLen).fill(0));
  const [history, setHistory] = useState(historyRef.current);

  useEffect(() => {
    const es = new EventSource("/api/telemetry");
    es.onmessage = (ev) => {
      let t;
      try {
        t = JSON.parse(ev.data);
      } catch {
        return;
      }
      const deviceConnected = !!t.connected && t.port && t.port !== "mock";
      const link = deviceConnected ? "device" : t.port === "mock" ? "preview" : "down";
      setTel({ ...t, deviceConnected, link });

      const h = historyRef.current.slice(1);
      h.push(t.peak || 0);
      historyRef.current = h;
      setHistory(h);
    };
    es.onerror = () => {
      setTel((prev) => ({ ...prev, link: "connecting" }));
    };
    return () => es.close();
  }, [historyLen]);

  return { tel, history };
}
