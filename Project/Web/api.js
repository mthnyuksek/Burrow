// Bridge to the program: commands go out as JSON messages, the answer comes back with the same id.
// Program-initiated events (e.g. "changed") are delivered to on() listeners.
// Two transports: inside the Burrow window it is WebView2's postMessage; opened in a browser through the
// owner's invite link (a friend joining remotely) it is HTTP: POST /api with the invite token, and a
// long poll on /api/poll that triggers a fresh "bootstrap" snapshot whenever the program changed.
const webview = window.chrome && window.chrome.webview;
const remote = !webview && /^https?:$/.test(location.protocol);
const pending = new Map();
const listeners = new Map();
let nextId = 1;
let token = '';

export const connected = Boolean(webview) || remote;
export const isRemote = remote;

if (remote) {
  try {
    const m = location.hash.match(/t=([0-9a-f]+)/);
    if (m) {
      localStorage.setItem('burrow.token', m[1]);
      history.replaceState(null, '', location.pathname);  // the secret does not stay in the address bar
    }
    token = localStorage.getItem('burrow.token') || '';
  } catch { /* private window: the token is simply not remembered */ }
}

function emit(event, data) {
  (listeners.get(event) || []).forEach((fn) => fn(data));
}

async function httpPost(cmd, args) {
  if (!token) throw new Error('Davet bağlantısı gerekli: arkadaşından aldığın bağlantıyı aç.');
  let res;
  try {
    res = await fetch('/api', {
      method: 'POST',
      headers: { Authorization: `Bearer ${token}`, 'Content-Type': 'application/json' },
      body: JSON.stringify({ id: 0, cmd, args }),
    });
  } catch {
    throw new Error('Burrow\'a ulaşılamıyor. Sunucu kapatılmış olabilir.');
  }
  const m = await res.json().catch(() => null);
  if (!m || !m.ok) {
    if (res.status === 401) emit('auth_lost');
    throw new Error((m && m.error) || 'Bilinmeyen hata');
  }
  return m.data;
}

export function call(cmd, args = {}) {
  if (remote) return httpPost(cmd, args);
  if (!webview) return Promise.reject(new Error('Program bağlantısı yok (WebView2 dışında açıldı).'));
  return new Promise((resolve, reject) => {
    const id = nextId++;
    pending.set(id, { resolve, reject });
    webview.postMessage({ id, cmd, args });
  });
}

let polling = false;
async function pollLoop() {
  let version = 0;
  for (;;) {
    try {
      const res = await fetch(`/api/poll?v=${version}`, { headers: { Authorization: `Bearer ${token}` } });
      if (res.status === 401) { emit('auth_lost'); return; }
      const { v } = await res.json();
      if (version !== 0 && v !== version) emit('changed', await httpPost('bootstrap', {}));
      version = v;
    } catch {
      await new Promise((r) => setTimeout(r, 3000));  // server closed or offline: try again soon
    }
  }
}

export function on(event, fn) {
  if (!listeners.has(event)) listeners.set(event, new Set());
  listeners.get(event).add(fn);
  if (remote && event === 'changed' && !polling && token) { polling = true; pollLoop(); }
  return () => listeners.get(event).delete(fn);
}

if (webview) {
  webview.addEventListener('message', (e) => {
    const m = e.data;
    if (m && m.event) {
      emit(m.event, m.data);
    } else if (m && pending.has(m.id)) {
      const { resolve, reject } = pending.get(m.id);
      pending.delete(m.id);
      m.ok ? resolve(m.data) : reject(new Error(m.error || 'Bilinmeyen hata'));
    }
  });
}
