import { Plug, PlugZap, Loader } from "lucide-react";

const STATUS = {
  device: (port) => ({ cls: "on", icon: <PlugZap size={14} />, text: `Connected · ${port}` }),
  preview: () => ({ cls: "warn", icon: <Plug size={14} />, text: "No device connected" }),
  down: () => ({ cls: "err", icon: <Plug size={14} />, text: "Disconnected" }),
  connecting: () => ({ cls: "", icon: <Loader size={14} className="spin" />, text: "Connecting…" }),
};

export default function Faceplate({ tel, accent, accents, onAccent }) {
  const s = (STATUS[tel.link] || STATUS.connecting)(tel.port);

  return (
    <header className="faceplate">
      <div className="brand">
        <span className="wordmark">SF4</span>
        <span className="tagline">SMART GUITAR AMP</span>
      </div>

      <div className="face-right">
        <div className="accents" role="group" aria-label="Accent colour">
          {accents.map((a) => (
            <button
              key={a}
              className={`accent-dot accent-${a}${a === accent ? " sel" : ""}`}
              onClick={() => onAccent(a)}
              title={a}
              aria-label={a}
            />
          ))}
        </div>
        <div className={`status-pill ${s.cls}`}>
          <span className="pill-dot" />
          {s.icon}
          <span className="pill-text">{s.text}</span>
        </div>
      </div>
    </header>
  );
}
