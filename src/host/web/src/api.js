// Thin wrappers over the FastAPI JSON endpoints. Same origin in production
// (served by FastAPI); proxied to :8000 in dev (see vite.config.js).

async function jsonFetch(url, options) {
  const r = await fetch(url, options);
  const body = await r.json().catch(() => ({}));
  if (!r.ok) {
    const err = new Error(body.detail || `HTTP ${r.status}`);
    err.status = r.status;
    throw err;
  }
  return body;
}

export function getState() {
  return jsonFetch("/api/state");
}

export function setParams(params) {
  return jsonFetch("/api/params", {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify(params),
  });
}

export function chat(message, history) {
  return jsonFetch("/api/chat", {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify({ message, history }),
  });
}
