import { call } from '../api.js';
import { h, confirmBox, showError } from '../ui.js';
import { masterDetail, kv, pre } from '../master_detail.js';
import { openForm } from '../forms.js';
import { tr } from '../i18n.js';

const PRIORITY = [tr('düşük'), tr('normal'), tr('yüksek')];

export default function create() {
  const editor = (task) => openForm({
    title: task ? tr('{name} — düzenle', { name: task.name }) : tr('Yeni görev'),
    wide: true,
    values: task || { priority: 1, min_remaining: 0 },
    fields: [
      { key: 'name', label: tr('Ad') },
      { key: 'model', label: tr('Model'), placeholder: tr('boş = Claude Code varsayılanı') },
      { key: 'priority', label: tr('Öncelik'), type: 'select', options: PRIORITY.map((p, i) => [String(i), p]) },
      { key: 'min_remaining', label: tr('En az kalan kullanım (%)'), type: 'number', max: 100, hint: tr('Kalan Claude kullanımı bundan azsa görev atlanır. 0 = hep çalış.') },
      { key: 'body', label: tr('Talimat'), type: 'textarea', rows: 9 },
    ],
    onSave: (v) => call('task.save', { ...v, priority: Number(v.priority) }),
  });

  return masterDetail({
    items: (s) => s.tasks,
    title: (t) => t.name,
    sub: (t) => tr('{model} · {priority} öncelik', { model: t.model || tr('varsayılan model'), priority: PRIORITY[t.priority] }),
    newLabel: tr('Yeni görev'),
    empty: tr('Henüz görev yok. Görev, ajanların uyanış saatinde yapacağı hazır bir iş tanımıdır.'),
    hint: tr('Uyanışlar bir görevi gösterir; bir ajan zamana göre farklı işler yapabilir.'),
    detail: (t) => [
      kv([
        [tr('Model'), t.model || tr('varsayılan')],
        [tr('Öncelik'), PRIORITY[t.priority]],
        [tr('Min. kalan kullanım'), t.min_remaining ? `%${t.min_remaining}` : '—'],
        [tr('Kullanan ajanlar'), t.used_by.length ? t.used_by.join(', ') : '—'],
      ]),
      pre(t.body),
    ],
    onNew: () => editor(null),
    onEdit: editor,
    onDelete: async (t) => {
      const uses = t.used_by.length;
      if (await confirmBox(tr('"{name}" görevi silinsin mi?', { name: t.name }) + (uses ? `\n\n${tr('{n} ajanın uyanışı bu görevi kullanıyor; "(silinmiş görev)" olarak görünecekler.', { n: uses })}` : ''), tr('Sil'), tr('Görevi sil')))
        call('task.delete', { id: t.id }).catch(showError);
    },
  });
}
