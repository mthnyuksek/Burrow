import { call } from '../api.js';
import { h, clear, button, fmtTokens, fmtDuration, relTime, showError } from '../ui.js';
import { renderMarkdown } from '../markdown.js';
import { tr } from '../i18n.js';

export default function create() {
  let runs = [];
  let open = null;
  const filterAgent = h('select');
  const filterStatus = h('select', null,
    h('option', { value: '' }, tr('Tüm durumlar')), ...['tamamlandı', 'hata', 'atlandı', 'durduruldu'].map((s) => h('option', { value: s }, tr(s))));
  const body = h('div', { class: 'card' });
  const el = h('div', null,
    h('div', { class: 'page-head' },
      button(tr('Yenile'), { iconName: 'refresh', onClick: load }),
      h('div', { style: { width: '200px' } }, filterAgent), h('div', { style: { width: '180px' } }, filterStatus),
      h('div', { class: 'grow' }),
      h('div', { class: 'hint' }, tr('Kasadaki Runs klasöründen okunur; hiç token harcamaz.'))),
    body);

  const chip = (s) => h('span', { class: `chip ${s === 'tamamlandı' ? 'good' : s === 'hata' ? 'bad' : 'warn'}` }, tr(s));

  async function toggle(run, row) {
    if (open === run.id) { open = null; draw(); return; }
    open = run.id;
    draw();
    try {
      const { text } = await call('run.get', { id: run.id });
      const slot = document.getElementById(`run-${run.id}`);
      if (slot) slot.append(h('div', { class: 'md pre', style: { maxHeight: '340px' }, html: renderMarkdown(text || tr('(boş)')) }));
    } catch (e) { showError(e); }
  }

  function draw() {
    const names = [...new Set(runs.map((r) => r.agent).filter(Boolean))];
    const keepAgent = filterAgent.value;
    filterAgent.replaceChildren(h('option', { value: '' }, tr('Tüm ajanlar')), ...names.map((n) => h('option', { value: n }, n)));
    filterAgent.value = names.includes(keepAgent) ? keepAgent : '';
    const rows = runs.filter((r) => (!filterAgent.value || r.agent === filterAgent.value) && (!filterStatus.value || r.status === filterStatus.value));
    clear(body);
    if (!rows.length) { body.append(h('div', { class: 'empty' }, tr('Kayıt yok.'))); return; }
    body.append(h('table', { class: 'tbl' },
      h('thead', null, h('tr', null, [tr('Zaman'), tr('Ajan'), tr('İş'), tr('Kaynak'), tr('Durum'), tr('Token'), tr('Süre')].map((t, i) => h('th', { class: i > 4 ? 'num' : '' }, t)))),
      h('tbody', null, rows.flatMap((r) => [
        h('tr', { class: 'click', onClick: () => toggle(r) },
          h('td', { class: 'muted' }, relTime(r.time), h('div', { class: 'faint', style: { fontSize: '11px' } }, r.time)),
          h('td', null, h('b', null, r.agent || '—')),
          h('td', null, r.task || tr('sohbet')),
          h('td', { class: 'muted' }, tr(r.source)),
          h('td', null, chip(r.status)),
          h('td', { class: 'num' }, fmtTokens(r.tokens)),
          h('td', { class: 'num' }, fmtDuration(r.duration))),
        open === r.id ? h('tr', null, h('td', { colspan: 7 }, h('div', { id: `run-${r.id}`, class: 'run-text' }))) : null,
      ]))));
  }

  async function load() {
    try { runs = await call('runs.list'); draw(); } catch (e) { showError(e); }
  }
  filterAgent.addEventListener('change', draw);
  filterStatus.addEventListener('change', draw);

  return { el, refresh: load };
}
