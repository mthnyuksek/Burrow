import { state, agentById, taskById, projectById } from '../state.js';
import { h, clear, button, fmtTokens, relTime, STATE_LABEL } from '../ui.js';
import { mascotEl, SPECIES } from '../mascot.js';
import { startChat, runNow, openAgentEditor, deleteAgent } from '../agent_actions.js';
import { tr } from '../i18n.js';

export default function create() {
  let view = 'cards';
  let selected = null;
  const grid = h('div');
  const detail = h('div', { class: 'detail' });
  const seg = h('div', { class: 'seg' });
  const el = h('div', null,
    h('div', { class: 'page-head' },
      seg, h('div', { class: 'grow' }),
      button(tr('Yeni ajan'), { kind: 'primary', iconName: 'plus', onClick: () => openAgentEditor(null) })),
    h('div', { class: 'layout-split' }, grid, detail));

  const select = (id) => { selected = id; draw(state.snap); };

  function card(a) {
    const mini = (label, value) => h('div', null, `${label}: `, h('b', null, value));
    return h('div', { class: `agent-card ${selected === a.id ? 'sel' : ''} ${a.state === 'working' ? 'is-working' : ''}`, onClick: () => select(a.id), onDblclick: () => openAgentEditor(a) },
      h('div', { class: 'top' }, mascotEl(a.mascot, a.state, 72),
        h('div', { style: { minWidth: 0, flex: 1 } }, h('div', { class: 'nm' }, a.name), h('div', { class: 'role' }, a.role || tr('Ünvan yok')),
          h('div', { style: { marginTop: '6px' } }, h('span', { class: `chip ${a.state}` }, STATE_LABEL[a.state])))),
      h('div', { class: 'facts' },
        mini(tr('Yönetici'), agentById(a.reports_to)?.name || '—'),
        mini(tr('Bugün'), a.daily_tokens ? tr('{n} token / {limit}', { n: fmtTokens(a.tokens_today), limit: fmtTokens(a.daily_tokens) }) : tr('{n} token', { n: fmtTokens(a.tokens_today) })),
        mini(tr('Uyanış'), a.wakeups.length ? a.wakeups.map((w) => w.text).join(', ') : tr('yok'))),
      h('div', { class: 'foot' },
        button(tr('Sohbet'), { small: true, iconName: 'chats', onClick: (e) => { e.stopPropagation(); startChat(a); } }),
        button(tr('Çalıştır'), { small: true, iconName: 'play', onClick: (e) => { e.stopPropagation(); runNow(a); } })));
  }

  function orgNode(a, byBoss) {
    const kids = byBoss.get(a.id) || [];
    return h('li', null,
      h('div', { class: `org-node ${a.state === 'working' ? 'is-working' : ''}`, onClick: () => select(a.id) },
        mascotEl(a.mascot, a.state, 52),
        h('div', null, h('div', { class: 'nm', style: { fontWeight: 650 } }, a.name), h('div', { class: 'muted', style: { fontSize: '12.5px' } }, a.role || '—'),
          h('span', { class: `chip ${a.state}`, style: { marginTop: '4px' } }, STATE_LABEL[a.state]))),
      kids.length ? h('ul', null, kids.map((k) => orgNode(k, byBoss))) : null);
  }

  function drawDetail(snap) {
    const a = agentById(selected);
    if (!a) {
      clear(detail).append(h('div', { class: 'card empty' }, tr('Ayrıntı için bir ajan seç.')));
      return;
    }
    const skillNames = a.skills.map((id) => snap.skills.find((s) => s.id === id)?.name || tr('{id} (silinmiş)', { id }));
    const wake = a.wakeups.map((w) => `${w.text} → ${w.task ? taskById(w.task)?.name || tr('(silinmiş görev)') : tr('kendi talimatı')}${w.project ? ` · ${projectById(w.project)?.name || tr('(silinmiş proje)')}` : ''}`);
    clear(detail).append(h('div', { class: 'card' },
      h('div', { class: 'hero' }, mascotEl(a.mascot, a.state, 88),
        h('div', null, h('div', { style: { fontSize: '18px', fontWeight: 650 } }, a.name), h('div', { class: 'muted' }, a.role || '—'),
          h('span', { class: `chip ${a.state}`, style: { marginTop: '6px' } }, STATE_LABEL[a.state]))),
      h('div', { class: 'btn-row' },
        button(tr('Sohbet et'), { kind: 'primary', iconName: 'chats', onClick: () => startChat(a) }),
        button(tr('Şimdi çalıştır'), { iconName: 'play', onClick: () => runNow(a) }),
        button('', { iconName: 'edit', title: tr('Düzenle'), onClick: () => openAgentEditor(a) }),
        button('', { kind: 'danger', iconName: 'trash', title: tr('Sil'), onClick: async () => { if (await deleteAgent(a)) selected = null; } })),
      h('dl', { class: 'kv' },
        h('dt', null, tr('Tür')), h('dd', null, SPECIES[a.mascot.species] || '—'),
        h('dt', null, tr('Yönetici')), h('dd', null, agentById(a.reports_to)?.name || '—'),
        h('dt', null, tr('Model')), h('dd', null, a.model || tr('varsayılan')),
        h('dt', null, tr('Bugün')), h('dd', null, a.daily_tokens ? tr('{n} token (sınır {limit})', { n: fmtTokens(a.tokens_today), limit: fmtTokens(a.daily_tokens) }) : tr('{n} token', { n: fmtTokens(a.tokens_today) })),
        h('dt', null, tr('7 gün')), h('dd', null, tr('{tokens} token · {runs} çalıştırma', { tokens: fmtTokens(a.tokens_7d), runs: a.runs_7d }) + (a.failures_7d ? tr(' · {n} hata', { n: a.failures_7d }) : '')),
        h('dt', null, tr('Son çalışma')), h('dd', null, relTime(a.last_run) || '—'),
        h('dt', null, tr('Yetenekler')), h('dd', null, skillNames.length ? skillNames.join(', ') : tr('yok (araç kullanamaz)')),
        h('dt', null, tr('Uyanışlar')), h('dd', null, wake.length ? wake.map((t) => h('div', null, t)) : tr('yok'))),
      h('div', { class: 'muted', style: { fontSize: '12.5px', margin: '4px 0 6px' } }, tr('Kişilik / talimat')),
      h('div', { class: 'pre' }, a.persona || '—')));
  }

  function draw(snap) {
    if (selected && !agentById(selected)) selected = null;
    clear(seg).append(
      h('button', { class: view === 'cards' ? 'on' : '', onClick: () => { view = 'cards'; draw(state.snap); } }, tr('Kartlar')),
      h('button', { class: view === 'tree' ? 'on' : '', onClick: () => { view = 'tree'; draw(state.snap); } }, tr('Hiyerarşi')));
    if (!snap.agents.length) {
      clear(grid).append(h('div', { class: 'card empty' }, tr('Henüz ajan yok. "Yeni ajan" ile ilk ajanını oluştur.')));
    } else if (view === 'cards') {
      clear(grid).append(h('div', { class: 'agent-grid' }, snap.agents.map(card)));
    } else {
      const byBoss = new Map();
      for (const a of snap.agents) {
        const key = a.reports_to && agentById(a.reports_to) ? a.reports_to : '';
        byBoss.set(key, [...(byBoss.get(key) || []), a]);
      }
      clear(grid).append(h('ul', { class: 'org' }, (byBoss.get('') || []).map((a) => orgNode(a, byBoss))));
    }
    drawDetail(snap);
  }

  return { el, refresh: draw, open: (p) => { if (p.agent) select(p.agent); } };
}
