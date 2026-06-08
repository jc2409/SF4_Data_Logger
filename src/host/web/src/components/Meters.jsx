const FS = 512; // full-scale peak

export default function Meters({ tel, history }) {
  const preview = !tel.deviceConnected;
  const pct = Math.min(100, (tel.peak / FS) * 100);

  // Build the level-history sparkline.
  const W = 260, H = 46;
  const n = history.length;
  const pts = history
    .map((v, i) => {
      const x = (i / (n - 1)) * W;
      const y = H - Math.min(1, v / FS) * H;
      return `${x.toFixed(1)},${y.toFixed(1)}`;
    })
    .join(" ");
  const peakHold = Math.max(0, ...history);
  const holdY = H - Math.min(1, peakHold / FS) * H;

  return (
    <div className="meters">
      <div className="meter-head">
        <span className="meter-label">Output level</span>
        {preview && <span className="preview-tag">PREVIEW</span>}
      </div>

      <div className="vu">
        <div className="vu-fill" style={{ width: pct + "%" }} />
        <div className="vu-gloss" />
      </div>

      <div className="spark">
        <svg viewBox={`0 0 ${W} ${H}`} preserveAspectRatio="none">
          <defs>
            <linearGradient id="sparkfill" x1="0" y1="0" x2="0" y2="1">
              <stop offset="0%" stopColor="var(--accent)" stopOpacity="0.35" />
              <stop offset="100%" stopColor="var(--accent)" stopOpacity="0" />
            </linearGradient>
          </defs>
          <polygon points={`0,${H} ${pts} ${W},${H}`} fill="url(#sparkfill)" />
          <polyline
            points={pts}
            fill="none"
            stroke="var(--accent)"
            strokeWidth="1.5"
            strokeLinejoin="round"
          />
          <line
            x1="0" y1={holdY} x2={W} y2={holdY}
            stroke="var(--rust)" strokeWidth="1" strokeDasharray="3 4" opacity="0.7"
          />
        </svg>
      </div>

      <div className="leds">
        <span className={`led${tel.clip ? " on" : ""}`}>CLIP</span>
        <span className="vga-readout">VGA <b>{tel.vga ?? "–"}</b></span>
      </div>
    </div>
  );
}
