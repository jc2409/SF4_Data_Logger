import { useEffect, useRef, useState } from "react";
import { AnimatePresence, motion } from "motion/react";
import { Send } from "lucide-react";
import { SUGGESTIONS } from "../ampParams";

let uid = 0;

export default function Chat({ onSend }) {
  const [messages, setMessages] = useState([
    {
      id: uid++,
      role: "assistant",
      content:
        "Tell me a tone, a song, or an artist — e.g. “warm bluesy overdrive” or “Gilmour on Comfortably Numb”.",
    },
  ]);
  const [text, setText] = useState("");
  const [sending, setSending] = useState(false);
  const historyRef = useRef([]);
  const scrollRef = useRef(null);

  useEffect(() => {
    const el = scrollRef.current;
    if (el) el.scrollTop = el.scrollHeight;
  }, [messages, sending]);

  const submit = async (raw) => {
    const message = raw.trim();
    if (!message || sending) return;
    setText("");
    setMessages((m) => [...m, { id: uid++, role: "user", content: message }]);
    setSending(true);
    try {
      const data = await onSend(message, historyRef.current);
      historyRef.current = [
        ...historyRef.current,
        { role: "user", content: message },
        { role: "assistant", content: data.reply },
      ];
      setMessages((m) => [
        ...m,
        { id: uid++, role: "assistant", content: data.reply },
        ...(data.changed
          ? [{ id: uid++, role: "note", content: `updated · ${data.params.effect}` }]
          : []),
      ]);
    } catch (e) {
      setMessages((m) => [
        ...m,
        { id: uid++, role: "error", content: e.message || "Something went wrong." },
      ]);
    } finally {
      setSending(false);
    }
  };

  return (
    <section className="panel chat">
      <h2>Talk to the amp</h2>

      <div className="messages" ref={scrollRef}>
        <AnimatePresence initial={false}>
          {messages.map((m) => (
            <motion.div
              key={m.id}
              className={`msg ${m.role}`}
              initial={{ opacity: 0, y: 8, scale: 0.98 }}
              animate={{ opacity: 1, y: 0, scale: 1 }}
              transition={{ duration: 0.22 }}
            >
              {m.content}
            </motion.div>
          ))}
        </AnimatePresence>
        {sending && (
          <div className="msg assistant typing">
            <span></span><span></span><span></span>
          </div>
        )}
      </div>

      <div className="chips">
        {SUGGESTIONS.map((s) => (
          <button key={s} className="chip" disabled={sending} onClick={() => submit(s)}>
            {s}
          </button>
        ))}
      </div>

      <form
        className="chat-input"
        onSubmit={(e) => {
          e.preventDefault();
          submit(text);
        }}
      >
        <input
          type="text"
          value={text}
          placeholder="Describe a sound…"
          autoComplete="off"
          onChange={(e) => setText(e.target.value)}
        />
        <button type="submit" disabled={sending || !text.trim()}>
          <Send size={16} />
        </button>
      </form>
    </section>
  );
}
