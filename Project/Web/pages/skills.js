import { call } from '../api.js';
import { h, button, openModal, confirmBox, showError } from '../ui.js';
import { masterDetail, kv, pre } from '../master_detail.js';
import { openForm } from '../forms.js';
import { byName } from '../state.js';
import { tr } from '../i18n.js';

export default function create() {
  const editor = (skill) => openForm({
    title: skill?.id ? tr('{name} — düzenle', { name: skill.name }) : tr('Yeni yetenek'),
    wide: true,
    values: skill || {},
    fields: [
      { key: 'name', label: tr('Ad') },
      { key: 'description', label: tr('Kısa açıklama') },
      { key: 'tools', label: tr('Araçlar'), placeholder: 'WebFetch, WebSearch, Read, mcp__...', hint: tr('Virgülle ayır. Her araç her çalıştırmada yaklaşık 0,9 bin token ekler.') },
      { key: 'mcp_config', label: tr('MCP ayar dosyası (isteğe bağlı)'), hint: tr('Kasaya göre ya da tam yol.') },
      { key: 'body', label: tr('Nasıl kullanılır'), type: 'textarea', rows: 9 },
    ],
    onSave: (v) => call('skill.save', v),
  });

  async function fromTemplate() {
    const templates = (await call('templates.list')).sort(byName);
    const list = h('div', { class: 'list' }, templates.map((t) => h('div', {
      class: 'list-item',
      onClick: () => { modal.close(); editor({ ...t }); },  // lets you adjust the template before it is saved
    }, h('div', { class: 't' }, t.name), h('div', { class: 'd' }, t.description))));
    const modal = openModal({ title: tr('Hazır yetenek ekle'), body: list });
  }

  return masterDetail({
    items: (s) => s.skills,
    title: (s) => s.name,
    sub: (s) => s.description || s.tools || '—',
    newLabel: tr('Yeni yetenek'),
    headerButtons: [button(tr('Şablondan ekle'), { iconName: 'spark', onClick: () => fromTemplate().catch(showError) })],
    empty: tr('Henüz yetenek yok. Yetenek, ajana araç ve kullanım talimatı verir; ajan yalnızca yeteneklerindeki araçları kullanabilir.'),
    hint: tr('Yetenekler ajan düzenlenirken seçilir.'),
    detail: (s) => [
      kv([
        [tr('Açıklama'), s.description || '—'],
        [tr('Araçlar'), s.tools || tr('yok')],
        [tr('MCP ayarı'), s.mcp_config || '—'],
        [tr('Kullanan ajanlar'), s.used_by.length ? s.used_by.join(', ') : '—'],
      ]),
      pre(s.body),
    ],
    onNew: () => editor(null),
    onEdit: editor,
    onDelete: async (s) => {
      if (await confirmBox(tr('"{name}" yeteneği silinsin mi?', { name: s.name }), tr('Sil'), tr('Yeteneği sil'))) call('skill.delete', { id: s.id }).catch(showError);
    },
  });
}
