import { motion } from "motion/react";
import { EFFECTS } from "../ampParams";

export default function EffectSelector({ value, onChange, highlight }) {
  return (
    <div className={`effects${highlight ? " pulse" : ""}`}>
      {EFFECTS.map((fx) => {
        const active = fx === value;
        return (
          <button
            key={fx}
            className={`fx${active ? " active" : ""}`}
            onClick={() => onChange(fx)}
          >
            {active && (
              <motion.span
                className="fx-glow"
                layoutId="fx-glow"
                transition={{ type: "spring", stiffness: 380, damping: 32 }}
              />
            )}
            <span className="led-tip" />
            <span className="fx-label">{fx}</span>
          </button>
        );
      })}
    </div>
  );
}
