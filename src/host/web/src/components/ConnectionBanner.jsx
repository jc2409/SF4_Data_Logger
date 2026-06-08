import { AnimatePresence, motion } from "motion/react";
import { TriangleAlert } from "lucide-react";

const MESSAGES = {
  preview: {
    title: "No device connected — preview mode",
    hint: "Plug in the SF4 amp (and close the Arduino Serial Monitor); it connects automatically. Knobs and chat still work, but changes aren't reaching hardware.",
  },
  down: {
    title: "Lost connection to the amp",
    hint: "The serial link dropped. Check the USB cable, then restart the host.",
  },
};

export default function ConnectionBanner({ tel }) {
  const msg = MESSAGES[tel.link];
  return (
    <AnimatePresence initial={false}>
      {msg && (
        <motion.div
          className="conn-banner"
          initial={{ opacity: 0, height: 0, marginBottom: 0 }}
          animate={{ opacity: 1, height: "auto", marginBottom: 18 }}
          exit={{ opacity: 0, height: 0, marginBottom: 0 }}
          transition={{ duration: 0.28 }}
        >
          <TriangleAlert size={18} className="banner-icon" />
          <div className="banner-text">
            <strong>{msg.title}</strong>
            <span>{msg.hint}</span>
          </div>
        </motion.div>
      )}
    </AnimatePresence>
  );
}
