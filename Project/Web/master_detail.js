// A list on the left, the selected item on the right, with new / edit / delete: shared by
// tasks, skills, rules and projects.
import { h, clear, button } from './ui.js';
import { tr } from './i18n.js';

// spec: { items(snap), title(item), sub(item), badge?(item), detail(item, snap) -> Node, empty,
//         headerButtons?: [Node], onNew(), onEdit(item), onDelete(item), newLabel, extraButtons?(item) }
export function masterDetail(spec) {
  let selected = null;
  let snapshot = null;
  const list = h('div', { class: 'list' });
  const detail = h('div', { class: 'card', style: { overflow: 'auto' } });
  const header = h('div', { class: 'page-head' });
  const el = h('div', { style: { display: 'flex', flexDirection: 'column', height: '100%' } },
    header, h('div', { class: 'master', style: { flex: 1, minHeight: 0 } }, list, detail));

  function draw() {
    const items = spec.items(snapshot);
    if (!items.some((i) => i.id === selected)) selected = items[0]?.id ?? null;
    const item = items.find((i) => i.id === selected);

    clear(header).append(
      button(spec.newLabel, { kind: 'primary', iconName: 'plus', onClick: spec.onNew }),
      ...(spec.headerButtons || []),
      h('div', { class: 'grow' }),
      spec.hint ? h('div', { class: 'hint' }, typeof spec.hint === 'function' ? spec.hint(snapshot) : spec.hint) : null);

    clear(list);
    if (!items.length) list.append(h('div', { class: 'card empty' }, spec.empty));
    for (const i of items) {
      list.append(h('div', { class: `list-item ${i.id === selected ? 'sel' : ''}`, onClick: () => { selected = i.id; draw(); }, onDblclick: () => spec.onEdit(i) },
        h('div', { class: 't' }, spec.title(i), spec.badge?.(i)),
        h('div', { class: 'd' }, spec.sub(i))));
    }

    clear(detail);
    if (!item) { detail.append(h('div', { class: 'empty' }, tr('Ayrıntı için soldan seç.'))); return; }
    detail.append(
      h('div', { class: 'card-head' },
        h('div', { style: { fontSize: '17px', fontWeight: 650 } }, spec.title(item)),
        h('div', { class: 'btn-row' },
          ...(spec.extraButtons?.(item) || []),
          button(tr('Düzenle'), { iconName: 'edit', onClick: () => spec.onEdit(item) }),
          button('', { kind: 'danger', iconName: 'trash', title: tr('Sil'), onClick: () => spec.onDelete(item) }))),
      ...[spec.detail(item, snapshot)].flat(Infinity));
  }

  return {
    el,
    refresh(snap) { snapshot = snap; draw(); },
    select(id) { selected = id; if (snapshot) draw(); },
  };
}

export const kv = (pairs) => h('dl', { class: 'kv' }, pairs.flatMap(([k, v]) => [h('dt', null, k), h('dd', null, v)]));
export const pre = (text) => h('div', { class: 'pre', style: { maxHeight: 'none' } }, text || '—');
