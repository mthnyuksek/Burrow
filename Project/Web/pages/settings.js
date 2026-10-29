// Settings. For now only the interface language, which lives in the vault's vault.ini
// (so the "kasa_ayarlari" skill lets an agent change it too). New settings go here as more cards.
import { call } from '../api.js';
import { tr, lang } from '../i18n.js';
import { h, field, selectInput, showError } from '../ui.js';
import { kv } from '../master_detail.js';

export default function create() {
  const language = selectInput([['tr', 'Türkçe'], ['en', 'English']], lang);
  language.addEventListener('change', () => {
    // The program answers with a "changed" snapshot; app.js then reloads the page in the new language.
    call('settings.set', { language: language.value }).catch((e) => { language.value = lang; showError(e); });
  });
  const info = h('div');

  const el = h('div', { style: { display: 'grid', gap: '18px', maxWidth: '640px', alignContent: 'start' } },
    h('div', { class: 'card' },
      h('div', { class: 'card-head' }, h('h3', null, tr('Dil'))),
      field(tr('Arayüz dili'), language,
        tr('Bu ayar kasanın vault.ini dosyasında saklanır. "Kasa ayarlarını düzenleme" yeteneği olan bir ajan da değiştirebilir. Değişince sayfa yenilenir.'))),
    h('div', { class: 'card' }, info),
    h('div', { class: 'muted' }, tr('Daha fazla ayar yakında.')));

  return {
    el,
    refresh(snap) {
      info.replaceChildren(kv([[tr('Kasa adı'), snap.vault.name], [tr('Kasa klasörü'), snap.vault.root]]));
    },
  };
}
