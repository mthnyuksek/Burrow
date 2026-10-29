// Shell: sidebar, top bar with the usage gauges, and the pages. Pages stay alive when hidden,
// so a half-written chat message or a selection survives switching tabs.
import { call, on, connected } from './api.js';
import { state, setSnapshot, subscribe } from './state.js';
import { h, icon, clear, button, showError, clockTime } from './ui.js';
import { mascotEl } from './mascot.js';
import { openVaultPicker } from './vault_picker.js';

import overview from './pages/overview.js';
import agents from './pages/agents.js';
import chats from './pages/chats.js';
import projects from './pages/projects.js';
import tasks from './pages/tasks.js';
import skills from './pages/skills.js';
import rules from './pages/rules.js';
import runs from './pages/runs.js';
import settings from './pages/settings.js';
import { tr, lang } from './i18n.js';

const PAGES = [
  { id: 'overview', label: tr('Genel Bakış'), icon: 'overview', create: overview },
  { id: 'agents', label: tr('Ajanlar'), icon: 'agents', create: agents },
  { id: 'chats', label: tr('Sohbetler'), icon: 'chats', create: chats, flush: true },
  { id: 'projects', label: tr('Projeler'), icon: 'projects', create: projects },
  { id: 'tasks', label: tr('Görevler'), icon: 'tasks', create: tasks },
  { id: 'skills', label: tr('Yetenekler'), icon: 'skills', create: skills },
  { id: 'rules', label: tr('Şirket Kuralları'), icon: 'rules', create: rules },
  { id: 'runs', label: tr('Çalıştırmalar'), icon: 'runs', create: runs },
  { id: 'settings', label: tr('Ayarlar'), icon: 'settings', create: settings },
];

const root = document.getElementById('root');
let current = null;
const instances = new Map();
const navButtons = new Map();
let title, gauges, queueChip, schedulerBox, vaultName, content;

function buildShell() {
  const nav = h('nav', { class: 'sidebar' },
    h('div', { class: 'brand' },
      h('div', { class: 'brand-mark' }, icon('spark')),
      h('div', null, h('div', { class: 'brand-name' }, 'Burrow'), h('div', { class: 'brand-sub' }, tr('Ajan merkezi')))),
    ...PAGES.map((p) => {
      const b = h('button', { class: 'nav-item', onClick: () => show(p.id) }, icon(p.icon), p.label);
      navButtons.set(p.id, b);
      return b;
    }),
    h('div', { class: 'nav-spacer' }),
    h('div', { class: 'vault-box' },
      h('div', { class: 'meta' }, h('div', { class: 'label' }, tr('Kasa')), (vaultName = h('div', { class: 'name' }))),
      button('', { kind: 'ghost', iconName: 'swap', title: tr('Kasa değiştir'), onClick: () => openVaultPicker() })));

  title = h('div', { class: 'page-title' });
  gauges = h('div', { style: { display: 'flex', gap: '18px' } });
  queueChip = h('span', { class: 'chip plain' });
  const schedulerInput = h('input', {
    type: 'checkbox',
    onChange: (e) => call('scheduler.set', { on: e.target.checked }).catch(showError),
  });
  schedulerBox = schedulerInput;
  const topbar = h('div', { class: 'topbar' },
    title, queueChip, gauges,
    h('label', { class: 'switch', title: tr('Açıkken uyanış saati gelen görevler kendiliğinden çalışır') },
      schedulerInput, h('span', { class: 'track' }), tr('Zamanlayıcı')),
    button('', { kind: 'ghost', iconName: 'refresh', title: tr('Kullanımı yenile'), onClick: () => call('usage.refresh').catch(showError) }),
    button(tr('Hepsini durdur'), { kind: 'danger', small: true, iconName: 'stop', onClick: () => call('runs.stopAll').catch(showError) }));

  content = h('div', { class: 'content' });
  root.append(h('div', { class: 'app' }, nav, h('main', { class: 'main' }, topbar, content)));
}

function gauge(label, w) {
  if (!w.known) return h('div', { class: 'gauge' }, h('div', { class: 'gauge-head' }, h('span', null, label), h('b', null, '—')), h('div', { class: 'bar' }, h('i', { style: { width: '0%' } })));
  const left = Math.max(0, Math.min(100, (1 - w.used) * 100));
  const level = left < 15 ? 'bad' : left < 40 ? 'warn' : '';
  return h('div', { class: 'gauge', title: w.resets_at ? tr('Sıfırlanma: {time}', { time: clockTime(w.resets_at) }) : '' },
    h('div', { class: 'gauge-head' }, h('span', null, label), h('b', null, tr('%{n} kaldı', { n: left.toFixed(0) }))),
    h('div', { class: `bar ${level}` }, h('i', { style: { width: `${left}%` } })));
}

function renderChrome(snap) {
  vaultName.textContent = snap.vault.name;
  clear(gauges).append(gauge(tr('5 saat'), snap.usage.five_hour), gauge(tr('7 gün'), snap.usage.seven_day));
  const { running, queued } = snap.queue;
  queueChip.className = `chip ${running + queued ? 'working' : 'plain'}`;
  queueChip.textContent = running + queued ? tr('{running} çalışıyor · {queued} sırada', { running, queued }) : tr('Sıra boş');
  schedulerBox.checked = snap.scheduler;
  const sel = PAGES.find((p) => p.id === current);
  const workingCount = snap.agents.filter((a) => a.state === 'working').length;
  navButtons.get('agents').querySelector('.badge')?.remove();
  if (workingCount) navButtons.get('agents').append(h('span', { class: 'badge' }, workingCount));
  if (sel) title.textContent = sel.label;
}

function show(id, params) {
  const def = PAGES.find((p) => p.id === id) || PAGES[0];
  current = def.id;
  navButtons.forEach((b, key) => b.classList.toggle('active', key === current));
  let inst = instances.get(current);
  if (!inst) {
    inst = def.create({ navigate: show });
    instances.set(current, inst);
  }
  clear(content).append(inst.el);
  content.classList.toggle('flush', Boolean(def.flush));
  if (state.snap) {
    renderChrome(state.snap);
    inst.refresh?.(state.snap);
  }
  if (params) inst.open?.(params);
}

state.navigate = show;
subscribe((snap) => {
  renderChrome(snap);
  instances.get(current)?.refresh?.(snap);
});

async function start() {
  buildShell();
  if (!connected) {
    clear(content).append(h('div', { class: 'empty' }, tr('Bu sayfa yalnızca Burrow uygulamasının içinde çalışır.')));
    return;
  }
  on('changed', (snap) => {
    if ((snap.vault.language || 'tr') !== lang) {  // language changed (settings page or an agent): rebuild everything
      sessionStorage.setItem('burrow.page', current);
      location.reload();
      return;
    }
    setSnapshot(snap);
  });
  on('vault', () => { instances.clear(); show(current); });  // another vault: forget selections
  try {
    setSnapshot(await call('bootstrap'));
  } catch (e) {
    showError(e);
    return;
  }
  const wanted = sessionStorage.getItem('burrow.page') || state.snap.start_page;
  sessionStorage.removeItem('burrow.page');
  show(PAGES.some((p) => p.id === wanted) ? wanted : 'overview');
  if (state.snap.needs_vault) openVaultPicker({ required: true });  // first start: no vault yet
}

start();
