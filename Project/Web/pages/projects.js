import { call } from '../api.js';
import { state } from '../state.js';
import { h, button, confirmBox, field, selectInput, openModal, toast, showError } from '../ui.js';
import { masterDetail, kv, pre } from '../master_detail.js';
import { openForm } from '../forms.js';
import { startChat } from '../agent_actions.js';
import { tr } from '../i18n.js';

export default function create() {
  const projectEditor = (p) => openForm({
    title: p ? tr('{name} — düzenle', { name: p.name }) : tr('Yeni proje'),
    wide: true,
    values: p || {},
    fields: [
      { key: 'name', label: tr('Ad') },
      { key: 'summary', label: tr('Özet'), type: 'textarea', rows: 6, hint: tr('Proje ne hakkında? Projede çalışan her ajanın her çalıştırmasına eklenir.') },
    ],
    onSave: (v) => call('project.save', v),
  });

  const noteEditor = (project, note) => openForm({
    title: note ? tr('{name} — düzenle', { name: note.name }) : tr('Yeni proje notu'),
    wide: true,
    values: { ...(note || { enabled: true }), project: project.id },
    fields: [
      { key: 'name', label: tr('Ad') },
      { key: 'enabled', label: tr('Etkin'), type: 'checkbox', hint: tr('Etkin notlar projede çalışan her ajanın kalıcı hafızasıdır.') },
      { key: 'body', label: tr('Not'), type: 'textarea', rows: 8 },
    ],
    onSave: (v) => call('note.save', v),
  });

  function chatInProject(project) {
    const agents = state.snap.agents;
    if (!agents.length) { toast(tr('Önce bir ajan oluştur.'), true); return; }
    const pick = selectInput(agents.map((a) => [a.id, a.name]), agents[0].id);
    openModal({
      title: tr('"{name}" projesinde sohbet', { name: project.name }),
      body: field(tr('Kiminle?'), pick),
      actions: [{ label: tr('Vazgeç') }, { label: tr('Başlat'), kind: 'primary', onClick: () => startChat(agents.find((a) => a.id === pick.value), project.id) }],
    });
  }

  return masterDetail({
    items: (s) => s.projects,
    title: (p) => p.name,
    sub: (p) => tr('{n} not · ~{tokens} token', { n: p.notes.length, tokens: p.tokens }),
    newLabel: tr('Yeni proje'),
    empty: tr('Henüz proje yok. Proje, ajanların kalıcı notlarla çalıştığı bir bağlamdır.'),
    hint: tr('Projedeki sohbet, uyanış ve çalıştırmalarda ajan özeti ve etkin notları okur.'),
    extraButtons: (p) => [button(tr('Projede sohbet'), { iconName: 'chats', onClick: () => chatInProject(p) })],
    detail: (p) => [
      kv([[tr('Token tahmini'), tr('~{n} (özet + etkin notlar, her çalıştırmada)', { n: p.tokens })]]),
      pre(p.summary),
      h('div', { class: 'card-head', style: { marginTop: '20px' } }, h('h3', null, tr('Notlar ({n})', { n: p.notes.length })),
        button(tr('Not ekle'), { small: true, iconName: 'plus', onClick: () => noteEditor(p, null) })),
      p.notes.length ? p.notes.map((n) => h('div', { class: 'note' },
        h('div', { class: 'nh' }, h('span', { class: 't' }, n.name),
          n.enabled ? null : h('span', { class: 'chip sleeping plain' }, tr('kapalı')),
          h('span', { class: 'muted', style: { fontSize: '12px' } }, tr('~{n} token', { n: n.tokens })),
          button('', { kind: 'ghost', small: true, iconName: 'edit', onClick: () => noteEditor(p, n) }),
          button('', { kind: 'ghost', small: true, iconName: 'trash', onClick: async () => {
            if (await confirmBox(tr('"{name}" notu silinsin mi?', { name: n.name }), tr('Sil'), tr('Notu sil'))) call('note.delete', { project: p.id, id: n.id }).catch(showError);
          } })),
        h('div', { class: 'muted', style: { whiteSpace: 'pre-wrap' } }, n.body))) : h('div', { class: 'muted' }, tr('Bu projede henüz not yok.')),
    ],
    onNew: () => projectEditor(null),
    onEdit: projectEditor,
    onDelete: async (p) => {
      if (await confirmBox(tr('"{name}" projesi ve içindeki {n} not silinsin mi? Sohbetler silinmez.', { name: p.name, n: p.notes.length }), tr('Sil'), tr('Projeyi sil'))) call('project.delete', { id: p.id }).catch(showError);
    },
  });
}
