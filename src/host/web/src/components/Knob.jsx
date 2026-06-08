import { useRef, useState } from "react";

const SWEEP = 270; // degrees of travel

export default function Knob({ label, value, min, max, onChange, onReset, dim, highlight }) {
  const [grabbing, setGrabbing] = useState(false);
  const [showTip, setShowTip] = useState(false);
  const drag = useRef(null);

  const span = max - min;
  const frac = Math.min(1, Math.max(0, (value - min) / (span || 1)));
  const angle = -SWEEP / 2 + frac * SWEEP;

  const clamp = (v) => Math.max(min, Math.min(max, Math.round(v)));

  const onPointerDown = (e) => {
    e.preventDefault();
    drag.current = { y: e.clientY, v: value };
    setGrabbing(true);
    setShowTip(true);
    e.currentTarget.setPointerCapture?.(e.pointerId);
  };
  const onPointerMove = (e) => {
    if (!drag.current) return;
    const delta = (drag.current.y - e.clientY) * (span / 160); // full sweep ≈ 160px
    onChange(clamp(drag.current.v + delta));
  };
  const onPointerUp = () => {
    drag.current = null;
    setGrabbing(false);
    setShowTip(false);
  };
  const onWheel = (e) => {
    e.preventDefault();
    const step = Math.max(1, Math.round(span / 100));
    onChange(clamp(value + (e.deltaY < 0 ? step : -step)));
  };

  return (
    <div className={`knob-cell${dim ? " dim" : ""}${highlight ? " pulse" : ""}`}>
      <div
        className={`knob-wrap${grabbing ? " grabbing" : ""}`}
        style={{ "--frac": frac, "--angle": `${angle}deg` }}
        onPointerDown={onPointerDown}
        onPointerMove={onPointerMove}
        onPointerUp={onPointerUp}
        onPointerCancel={onPointerUp}
        onWheel={onWheel}
        onDoubleClick={onReset}
        onMouseEnter={() => setShowTip(true)}
        onMouseLeave={() => !drag.current && setShowTip(false)}
        role="slider"
        aria-label={label}
        aria-valuenow={value}
        aria-valuemin={min}
        aria-valuemax={max}
        tabIndex={0}
      >
        <div className={`knob-tip${showTip ? " show" : ""}`}>{value}</div>
        <div className="knob-arc" />
        <div className="knob">
          <div className="knob-needle" />
        </div>
      </div>
      <label>{label}</label>
      <output>{value}</output>
    </div>
  );
}
