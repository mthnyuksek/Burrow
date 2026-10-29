// Small DOM helpers shared by all pages: element builder, icons, modal, toast, formatting.
import { tr, lang, locale } from './i18n.js';

export function h(tag, attrs, ...kids) {
  const el = document.createElement(tag);
  for (const [k, v] of Object.entries(attrs || {})) {
    if (v == null || v === false) continue;
    if (k === 'class') el.className = v;
    else if (k === 'style' && typeof v === 'object') Object.assign(el.style, v);
    else if (k.startsWith('on')) el.addEventListener(k.slice(2).toLowerCase(), v);
    else if (k === 'html') el.innerHTML = v;
    else if (k in el && k !== 'list') el[k] = v;
    else el.setAttribute(k, v === true ? '' : v);
  }
  append(el, kids);
  return el;
}

function append(el, kids) {
  for (const kid of kids.flat(Infinity)) {
    if (kid == null || kid === false) continue;
    el.append(kid instanceof Node ? kid : document.createTextNode(String(kid)));
  }
}

export function clear(el) {
  el.replaceChildren();
  return el;
}

const ICONS = {
  overview: 'M3 3h7v9H3z M14 3h7v5h-7z M14 12h7v9h-7z M3 16h7v5H3z',
  agents: 'M16 21v-2a4 4 0 0 0-4-4H6a4 4 0 0 0-4 4v2 M9 11a4 4 0 1 0 0-8 4 4 0 0 0 0 8z M22 21v-2a4 4 0 0 0-3-3.87 M16 3.13a4 4 0 0 1 0 7.75',
  tasks: 'M9 11l3 3L22 4 M21 12v7a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2V5a2 2 0 0 1 2-2h11',
  skills: 'M13 2L3 14h9l-1 8 10-12h-9l1-8z',
  rules: 'M12 22s8-4 8-10V5l-8-3-8 3v7c0 6 8 10 8 10z',
  projects: 'M22 19a2 2 0 0 1-2 2H4a2 2 0 0 1-2-2V5a2 2 0 0 1 2-2h5l2 3h9a2 2 0 0 1 2 2z',
  chats: 'M21 15a2 2 0 0 1-2 2H7l-4 4V5a2 2 0 0 1 2-2h14a2 2 0 0 1 2 2z',
  runs: 'M22 12h-4l-3 9L9 3l-3 9H2',
  plus: 'M12 5v14 M5 12h14',
  edit: 'M12 20h9 M16.5 3.5a2.12 2.12 0 0 1 3 3L7 19l-4 1 1-4z',
  trash: 'M3 6h18 M8 6V4a2 2 0 0 1 2-2h4a2 2 0 0 1 2 2v2 M19 6l-1 14a2 2 0 0 1-2 2H8a2 2 0 0 1-2-2L5 6',
  play: 'M5 3l14 9-14 9z',
  send: 'M22 2L11 13 M22 2l-7 20-4-9-9-4z',
  stop: 'M6 6h12v12H6z',
  refresh: 'M23 4v6h-6 M1 20v-6h6 M3.51 9a9 9 0 0 1 14.85-3.36L23 10 M1 14l4.64 4.36A9 9 0 0 0 20.49 15',
  download: 'M21 15v4a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2v-4 M7 10l5 5 5-5 M12 15V3',
  search: 'M11 19a8 8 0 1 0 0-16 8 8 0 0 0 0 16z M21 21l-4.35-4.35',
  x: 'M18 6L6 18 M6 6l12 12',
  swap: 'M17 1l4 4-4 4 M3 11V9a4 4 0 0 1 4-4h14 M7 23l-4-4 4-4 M21 13v2a4 4 0 0 1-4 4H3',
  clock: 'M12 22a10 10 0 1 0 0-20 10 10 0 0 0 0 20z M12 6v6l4 2',
  tree: 'M10 3h4v5h-4z M3 16h4v5H3z M17 16h4v5h-4z M12 8v4 M5 16v-4h14v4',
  grid: 'M3 3h7v7H3z M14 3h7v7h-7z M14 14h7v7h-7z M3 14h7v7H3z',
  settings: 'M12 15a3 3 0 1 0 0-6 3 3 0 0 0 0 6z M19.4 15a1.65 1.65 0 0 0 .33 1.82l.06.06a2 2 0 1 1-2.83 2.83l-.06-.06a1.65 1.65 0 0 0-1.82-.33 1.65 1.65 0 0 0-1 1.51V21a2 2 0 1 1-4 0v-.09A1.65 1.65 0 0 0 9 19.4a1.65 1.65 0 0 0-1.82.33l-.06.06a2 2 0 1 1-2.83-2.83l.06-.06a1.65 1.65 0 0 0 .33-1.82 1.65 1.65 0 0 0-1.51-1H3a2 2 0 1 1 0-4h.09A1.65 1.65 0 0 0 4.6 9a1.65 1.65 0 0 0-.33-1.82l-.06-.06a2 2 0 1 1 2.83-2.83l.06.06a1.65 1.65 0 0 0 1.82.33H9a1.65 1.65 0 0 0 1-1.51V3a2 2 0 1 1 4 0v.09a1.65 1.65 0 0 0 1 1.51 1.65 1.65 0 0 0 1.82-.33l.06-.06a2 2 0 1 1 2.83 2.83l-.06.06a1.65 1.65 0 0 0-.33 1.82V9a1.65 1.65 0 0 0 1.51 1H21a2 2 0 1 1 0 4h-.09a1.65 1.65 0 0 0-1.51 1z',
  spark: 'M12 2l2.4 6.6L21 11l-6.6 2.4L12 20l-2.4-6.6L3 11l6.6-2.4z',
};

export function icon(name) {
  const svg = document.createElementNS('http://www.w3.org/2000/svg', 'svg');
  svg.setAttribute('viewBox', '0 0 24 24');
  svg.setAttribute('fill', 'none');
  svg.setAttribute('stroke', 'currentColor');
  svg.setAttribute('stroke-width', '1.8');
  svg.setAttribute('stroke-linecap', 'round');
  svg.setAttribute('stroke-linejoin', 'round');
  const path = document.createElementNS('http://www.w3.org/2000/svg', 'path');
  path.setAttribute('d', ICONS[name] || '');
  svg.append(path);
  return svg;
}

export function button(label, opts = {}) {
  const { kind = '', iconName, onClick, title, small, disabled } = opts;
  const b = h('button', { class: `btn ${kind} ${small ? 'small' : ''} ${label ? '' : 'icon'}`, title, disabled, onClick });
  if (iconName) b.append(icon(iconName));
  if (label) b.append(label);
  return b;
}

// ---------- Modal, confirm, toast ----------

// locked: no close button, no click-outside or Escape (the person has to finish the dialog).
export function openModal({ title, body, actions = [], wide = false, onClose, locked = false }) {
  const overlay = h('div', { class: 'overlay' });
  const close = () => { overlay.remove(); onClose?.(); };
  const foot = h('div', { class: 'modal-foot' });
  const buttons = actions.map((a) => {
    const b = button(a.label, {
      kind: a.kind,
      onClick: async () => {
        b.disabled = true;
        try {
          const keepOpen = await a.onClick?.({ close });
          if (keepOpen !== false) close();
        } catch (e) {
          showError(e);
        } finally {
          b.disabled = false;
        }
      },
    });
    return b;
  });
  foot.append(...buttons);
  const modal = h('div', { class: `modal ${wide ? 'wide' : ''}` },
    h('div', { class: 'modal-head' }, h('h2', null, title), locked ? null : button('', { kind: 'ghost', iconName: 'x', onClick: close })),
    h('div', { class: 'modal-body' }, body),
    actions.length ? foot : null);
  overlay.append(modal);
  if (!locked) {
    overlay.addEventListener('mousedown', (e) => { if (e.target === overlay) close(); });
    overlay.addEventListener('keydown', (e) => { if (e.key === 'Escape') close(); });
  }
  document.body.append(overlay);
  const first = modal.querySelector('input[type=text], textarea');
  if (first) first.focus();
  return { close, modal };
}

export function confirmBox(message, okLabel = tr('Evet'), title = tr('Emin misin?')) {
  return new Promise((resolve) => {  // the first resolve wins: closing the window after an answer changes nothing
    openModal({
      title,
      body: h('p', { style: { whiteSpace: 'pre-wrap' } }, message),
      onClose: () => resolve(false),
      actions: [
        { label: tr('Vazgeç') },
        { label: okLabel, kind: 'primary', onClick: () => resolve(true) },
      ],
    });
  });
}

let toastBox;
export function toast(message, bad = false) {
  if (!toastBox) { toastBox = h('div', { class: 'toasts' }); document.body.append(toastBox); }
  const t = h('div', { class: `toast ${bad ? 'bad' : ''}` }, message);
  toastBox.append(t);
  setTimeout(() => t.remove(), bad ? 6000 : 3200);
}

export function showError(e) {
  toast(tr(e && e.message ? e.message : String(e)), true);
}

// ---------- Form helpers ----------

export function field(label, input, hint) {
  return h('div', { class: 'field' }, h('label', null, label), input, hint ? h('div', { class: 'hint' }, hint) : null);
}

export function textInput(value = '', attrs = {}) {
  return h('input', { type: 'text', value, ...attrs });
}

export function selectInput(options, value) {
  const s = h('select');
  for (const [v, label] of options) s.append(h('option', { value: v, selected: v === value }, label));
  s.value = value ?? options[0]?.[0] ?? '';
  return s;
}

// ---------- Formatting ----------

export function fmtTokens(n) {
  n = Number(n) || 0;
  const dec = (x) => x.toFixed(1).replace('.', lang === 'en' ? '.' : ',');
  if (n >= 1e6) return `${dec(n / 1e6)} ${tr('Mn')}`;
  if (n >= 1e4) return `${Math.round(n / 1e3)} ${tr('bin')}`;
  if (n >= 1e3) return `${dec(n / 1e3)} ${tr('bin')}`;
  return String(n);
}

export function fmtDuration(s) {
  s = Number(s) || 0;
  if (s < 60) return `${s.toFixed(0)} ${tr('sn')}`;
  return `${Math.floor(s / 60)} ${tr('dk')} ${Math.round(s % 60)} ${tr('sn')}`;
}

// "2026-10-28 14:05:09" -> Date (local time)
export function parseTime(text) {
  if (!text) return null;
  const d = new Date(text.replace(' ', 'T'));
  return Number.isNaN(d.getTime()) ? null : d;
}

export function relTime(text) {
  const d = parseTime(text);
  if (!d) return '';
  const sec = (Date.now() - d.getTime()) / 1000;
  if (sec < 60) return tr('şimdi');
  if (sec < 3600) return tr('{n} dk önce', { n: Math.floor(sec / 60) });
  if (sec < 86400) return tr('{n} sa önce', { n: Math.floor(sec / 3600) });
  if (sec < 86400 * 7) return tr('{n} gün önce', { n: Math.floor(sec / 86400) });
  return d.toLocaleDateString(locale, { day: 'numeric', month: 'short' });
}

export function clockTime(epochSeconds) {
  if (!epochSeconds) return '';
  return new Date(epochSeconds * 1000).toLocaleTimeString(locale, { hour: '2-digit', minute: '2-digit' });
}

export const STATE_LABEL = { working: tr('Çalışıyor'), idle: tr('Boşta'), sleeping: tr('Uykuda'), limit: tr('Limit doldu') };
