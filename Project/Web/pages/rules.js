import { call } from '../api.js';
import { h, confirmBox, showError, fmtTokens } from '../ui.js';
import { masterDetail, kv, pre } from '../master_detail.js';
import { openForm } from '../forms.js';
import { tr } from '../i18n.js';

export default function create() {
  const editor = (rule) => openForm({
    title: rule ? tr('{name} — düzenle', { name: rule.name }) : tr('Yeni kural'),
    wide: true,
    values: rule || { enabled: true },
    fields: [
      { key: 'name', label: tr('Ad') },
      { key: 'enabled', label: tr('Etkin'), type: 'checkbox', hint: tr('Etkin kurallar her ajanın her çalıştırmasına eklenir.') },
      { key: 'body', label: tr('Kural metni'), type: 'textarea', rows: 9, hint: tr('Kısa tut: her çalıştırmada token olarak ödenir.') },
    ],
    onSave: (v) => call('rule.save', v),
  });

  return masterDetail({
    items: (s) => s.rules,
    title: (r) => r.name,
    badge: (r) => (r.enabled ? null : h('span', { class: 'chip sleeping plain' }, tr('kapalı'))),
    sub: (r) => tr('~{n} token', { n: r.tokens }),
    newLabel: tr('Yeni kural'),
    empty: tr('Henüz kural yok. Şirket kuralları tüm ajanların ortak talimatıdır.'),
    hint: (s) => tr('Etkin kurallar her çalıştırmaya yaklaşık {n} token ekler.', { n: fmtTokens(s.rule_tokens) }),
    detail: (r) => [kv([[tr('Durum'), r.enabled ? tr('Etkin') : tr('Kapalı')], [tr('Token tahmini'), `~${r.tokens}`]]), pre(r.body)],
    onNew: () => editor(null),
    onEdit: editor,
    onDelete: async (r) => {
      if (await confirmBox(tr('"{name}" kuralı silinsin mi?', { name: r.name }), tr('Sil'), tr('Kuralı sil'))) call('rule.delete', { id: r.id }).catch(showError);
    },
  });
}
