// Bridge to the program: commands go out as JSON messages, the answer comes back with the same id.
// Program-initiated events (e.g. "changed") are delivered to on() listeners.
const webview = window.chrome && window.chrome.webview;
const pending = new Map();
const listeners = new Map();
let nextId = 1;

export const connected = Boolean(webview);

export function call(cmd, args = {}) {
  if (!webview) return Promise.reject(new Error('Program bağlantısı yok (WebView2 dışında açıldı).'));
  return new Promise((resolve, reject) => {
    const id = nextId++;
    pending.set(id, { resolve, reject });
    webview.postMessage({ id, cmd, args });
  });
}

export function on(event, fn) {
  if (!listeners.has(event)) listeners.set(event, new Set());
  listeners.get(event).add(fn);
  return () => listeners.get(event).delete(fn);
}

if (webview) {
  webview.addEventListener('message', (e) => {
    const m = e.data;
    if (m && m.event) {
      (listeners.get(m.event) || []).forEach((fn) => fn(m.data));
    } else if (m && pending.has(m.id)) {
      const { resolve, reject } = pending.get(m.id);
      pending.delete(m.id);
      m.ok ? resolve(m.data) : reject(new Error(m.error || 'Bilinmeyen hata'));
    }
  });
}
