import { call } from '../api.js';
import { state, agentById } from '../state.js';
import { h, clear, fmtTokens, fmtDuration, relTime, STATE_LABEL } from '../ui.js';
import { mascotEl } from '../mascot.js';
import { tr } from '../i18n.js';

const statusChip = (s) => h('span', { class: `chip ${s === 'tamamlandı' ? 'good' : s === 'hata' ? 'bad' : 'warn'}` }, tr(s));

export default function create({ navigate }) {
  const stats = h('div', { class: 'stats' });
  const team = h('div', { class: 'card' });
  const recent = h('div', { class: 'card' });
  const flow = h('div', { class: 'card' });
  const alert = h('div', { class: 'card', style: { borderColor: 'var(--warn)', display: 'none' } });
  const el = h('div', null, alert, stats, h('div', { class: 'two-col' }, team, h('div', { style: { display: 'grid', gap: '18px', alignContent: 'start' } }, flow, recent)));

  const stat = (k, v, s) => h('div', { class: 'stat' }, h('div', { class: 'k' }, k), h('div', { class: 'v' }, v), h('div', { class: 's' }, s));

  async function drawFlow() {
    // The newest conversation between agents: this is what shows how the team talks to each other.
    const list = await call('chats.list');
    const conv = list.find((c) => c.kind === 'agents');
    const box = h('div');
    if (!conv) {
      box.append(h('div', { class: 'muted' }, tr('Henüz ajanlar arası konuşma yok. Bir ajan başka bir ajana <send> ile mesaj gönderdiğinde burada görünür.')));
    } else {
      const data = await call('chat.load', { id: conv.id });
      box.append(h('div', { class: 'muted', style: { marginBottom: '10px' } }, data.title));
      for (const m of data.messages.slice(-5)) {
        const from = agentById(m.from);
        const to = agentById(m.to);
        box.append(h('div', { style: { display: 'flex', gap: '10px', margin: '10px 0' } },
          from ? mascotEl(from.mascot, from.state, 34) : null,
          h('div', { style: { minWidth: 0 } },
            h('div', { class: 'muted', style: { fontSize: '12px' } }, h('b', { style: { color: 'var(--text)' } }, m.from === 'user' ? tr('Sen') : m.from_name), ' → ', to ? to.name : m.to_name || tr('Sen'), ' · ', relTime(m.time)),
            h('div', { style: { overflow: 'hidden', display: '-webkit-box', WebkitLineClamp: '2', WebkitBoxOrient: 'vertical' } }, m.text))));
      }
      box.append(h('button', { class: 'btn small', onClick: () => navigate('chats', { chat: conv.id }) }, tr('Konuşmayı aç')));
    }
    flow.replaceChildren(h('div', { class: 'card-head' }, h('h3', null, tr('Ajanlar arası konuşma'))), box);
  }

  function refresh(snap) {
    const active = snap.agents.filter((a) => a.enabled);
    const working = snap.agents.filter((a) => a.state === 'working').length;
    const tokensToday = snap.agents.reduce((n, a) => n + a.tokens_today, 0);
    const tokens7 = snap.agents.reduce((n, a) => n + a.tokens_7d, 0);
    // Vault-wide budget (Ayarlar): only scheduled wake-ups are held back once it is used up.
    const b = snap.budget, v = snap.vault;
    const over = [];
    if (b.daily_over) over.push(tr('Günlük kasa bütçesi doldu ({n} token); zamanlı uyanışlar yarına kadar durdu.', { n: fmtTokens(v.daily_budget) }));
    if (b.weekly_over) over.push(tr('Haftalık kasa bütçesi doldu ({n} token); zamanlı uyanışlar durdu.', { n: fmtTokens(v.weekly_budget) }));
    alert.style.display = over.length ? '' : 'none';
    clear(alert).append(over.map((t) => h('div', null, t)));
    const todayNote = v.daily_budget ? tr('bütçe {n}', { n: fmtTokens(v.daily_budget) }) : '';
    const weekNote = v.weekly_budget ? ` · ${tr('haftalık bütçe {n}', { n: fmtTokens(v.weekly_budget) })}` : '';
    clear(stats).append(
      stat(tr('Ekip'), `${active.length}/${snap.agents.length}`, working ? tr('{n} ajan çalışıyor', { n: working }) : tr('etkin / toplam ajan')),
      stat(tr('Bugün harcanan token'), fmtTokens(tokensToday), `${todayNote ? `${todayNote} · ` : ''}${tr('son 7 gün: {n}', { n: fmtTokens(tokens7) })}${weekNote}`),
      stat(tr('Kuyruk'), `${snap.queue.running}`, tr('{n} iş sırada bekliyor', { n: snap.queue.queued })),
      stat(tr('Projeler'), snap.projects.length, tr('{n} proje notu', { n: snap.note_count })));

    clear(team).append(
      h('div', { class: 'card-head' }, h('h3', null, tr('Ekip'))),
      snap.agents.length
        ? h('table', { class: 'tbl' },
          h('thead', null, h('tr', null, [tr('Ajan'), tr('Durum'), tr('Bugün'), tr('7 gün'), tr('Çalıştırma'), tr('Son')].map((t, i) => h('th', { class: i > 1 && i < 5 ? 'num' : '' }, t)))),
          h('tbody', null, snap.agents.map((a) => {
            const pct = a.daily_tokens ? Math.min(100, (a.tokens_today / a.daily_tokens) * 100) : 0;
            return h('tr', { class: 'click', onClick: () => navigate('agents', { agent: a.id }) },
              h('td', null, h('div', { class: 'who' }, mascotEl(a.mascot, a.state, 38), h('div', null, h('div', { class: 'nm' }, a.name), h('div', { class: 'sub' }, a.role || '—')))),
              h('td', null, h('span', { class: `chip ${a.state}` }, STATE_LABEL[a.state])),
              h('td', { class: 'num' }, fmtTokens(a.tokens_today), a.daily_tokens ? h('div', { class: `bar ${pct >= 100 ? 'bad' : pct > 75 ? 'warn' : 'accent'}`, style: { marginTop: '4px', width: '70px', marginLeft: 'auto' }, title: tr('Günlük sınır: {n}', { n: fmtTokens(a.daily_tokens) }) }, h('i', { style: { width: `${pct}%` } })) : null),
              h('td', { class: 'num' }, fmtTokens(a.tokens_7d)),
              h('td', { class: 'num' }, a.runs_7d, a.failures_7d ? h('span', { class: 'chip bad plain', style: { marginLeft: '6px' } }, tr('{n} hata', { n: a.failures_7d })) : null),
              h('td', { class: 'muted' }, relTime(a.last_run) || '—'));
          })))
        : h('div', { class: 'empty' }, tr('Henüz ajan yok. "Ajanlar" sayfasından ilk ajanını oluştur.')));

    clear(recent).append(
      h('div', { class: 'card-head' }, h('h3', null, tr('Son çalıştırmalar'))),
      snap.recent_runs.length
        ? h('div', null, snap.recent_runs.slice(0, 8).map((r) => h('div', { style: { display: 'flex', alignItems: 'center', gap: '10px', padding: '8px 0', borderBottom: '1px solid var(--border-soft)' } },
          h('div', { style: { flex: 1, minWidth: 0 } }, h('div', { style: { fontWeight: 600 } }, r.agent), h('div', { class: 'muted', style: { fontSize: '12px' } }, tr('{task} · {tokens} token · {duration} · {time}', { task: r.task || tr('sohbet'), tokens: fmtTokens(r.tokens), duration: fmtDuration(r.duration), time: relTime(r.time) }))),
          statusChip(r.status))))
        : h('div', { class: 'muted' }, tr('Henüz çalıştırma yok.')));
    drawFlow().catch(() => {});
  }

  return { el, refresh };
}
