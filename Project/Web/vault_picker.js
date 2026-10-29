// Vault picker: open one of the known vaults, add an existing vault folder or create a new vault.
// With required = true (first start, no vault yet) it cannot be closed until a vault is open.
import { call } from './api.js';
import { state } from './state.js';
import { h, openModal, button, field, textInput, showError, confirmBox, clear } from './ui.js';
import { tr } from './i18n.js';

let active = false;  // one picker at a time

export function openVaultPicker({ required = false } = {}) {
  if (active) return;
  active = true;

  const list = h('div', { class: 'list' });
  const name = textInput('', { placeholder: tr('Örn. Şirketim') });
  const folder = textInput('', { placeholder: tr('Kasa için bir klasör seç') });
  let dialog;

  async function openVault(path) {
    const snap = state.snap;
    const busy = snap && !snap.needs_vault && snap.queue.running + snap.queue.queued > 0;
    if (busy && !(await confirmBox(tr('Çalışan ya da sıradaki işler var. Kasa değişirse durdurulacaklar. Devam edilsin mi?'),
      tr('Devam'), tr('Kasa değiştir')))) return;
    try {
      await call('vault.open', { path });
      dialog.close();
    } catch (e) {
      showError(e);
    }
  }

  async function browse(title) {
    const { path } = await call('vault.browse', { title });
    return path;
  }

  async function refresh() {
    const { entries, current } = await call('vault.list');
    clear(list);
    if (!entries.length) list.append(h('div', { class: 'hint' }, tr('Henüz kayıtlı kasa yok.')));
    for (const e of entries) {
      const isCurrent = current && e.path.toLowerCase() === current.toLowerCase();
      list.append(h('div', { class: `list-item vault-row ${isCurrent ? 'sel' : ''}`, onClick: () => openVault(e.path) },
        h('div', { class: 'meta' }, h('div', { class: 't' }, e.name), h('div', { class: 'd' }, e.path)),
        button('', {
          kind: 'ghost', iconName: 'trash', title: tr('Listeden kaldır'),
          onClick: async (ev) => {
            ev.stopPropagation();  // the row itself opens the vault
            try { await call('vault.remove', { path: e.path }); await refresh(); } catch (err) { showError(err); }
          },
        })));
    }
  }

  const addExisting = button(tr('Mevcut kasayı ekle...'), {
    iconName: 'plus',
    onClick: async () => {
      try {
        const path = await browse(tr('Mevcut kasa klasörünü seç'));
        if (!path) return;
        await call('vault.add', { path });
        await refresh();
      } catch (e) {
        showError(e);
      }
    },
  });

  const pickFolder = button(tr('Gözat...'), {
    onClick: async () => {
      try {
        const path = await browse(tr('Kasa klasörünü seç'));
        if (path) folder.value = path;
      } catch (e) {
        showError(e);
      }
    },
  });

  const createButton = button(tr('Oluştur ve aç'), {
    kind: 'primary',
    onClick: async () => {
      try {
        await call('vault.create', { name: name.value, path: folder.value });
        dialog.close();
      } catch (e) {
        showError(e);
      }
    },
  });

  dialog = openModal({
    title: tr('Kasa seç'),
    locked: required,
    onClose: () => { active = false; },
    body: [
      h('div', { class: 'field' }, h('label', null, tr('Kayıtlı kasalar')), list),
      h('div', null, addExisting),
      h('div', { class: 'vault-create' },
        h('h3', null, tr('Yeni kasa oluştur')),
        field(tr('Ad'), name),
        field(tr('Klasör'), h('div', { class: 'vault-folder' }, folder, pickFolder)),
        createButton),
    ],
  });
  refresh().catch(showError);
}
