// Settings, all kept in the vault's vault.ini (so the "kasa_ayarlari" skill lets an agent change them too):
// language, run limits (concurrency, agent chain length, default model) and the vault-wide token budget.
import { call } from '../api.js';
import { tr, lang } from '../i18n.js';
import { h, field, selectInput, showError, toast, button, fmtTokens } from '../ui.js';
import { kv } from '../master_detail.js';

export default function create() {
  const language = selectInput([['tr', 'Türkçe'], ['en', 'English']], lang);
  language.addEventListener('change', () => {
    // The program answers with a "changed" snapshot; app.js then reloads the page in the new language.
    call('settings.set', { language: language.value }).catch((e) => { language.value = lang; showError(e); });
  });
  const info = h('div');

  const numberInput = (min, max) => h('input', { type: 'number', min, max });
  const concurrent = numberInput(1, 4);
  const hops = numberInput(1, 20);
  const model = h('input', { type: 'text', placeholder: tr('boş = Claude Code varsayılanı (örn. sonnet, haiku)') });
  const daily = numberInput(0, 2000000000);
  const weekly = numberInput(0, 2000000000);
  const usageNote = h('div', { class: 'hint' });
  let loaded = '';

  function save(args) {
    call('settings.set', args).then(() => toast(tr('Kaydedildi.'))).catch(showError);
  }

  const limitsCard = h('div', { class: 'card' },
    h('div', { class: 'card-head' }, h('h3', null, tr('Çalışma sınırları'))),
    field(tr('Aynı anda çalışan iş sayısı'), concurrent, tr('1-4. Abonelik sınırı ortak olduğu için düşük tut; 1 işler sırayla çalışır.')),
    field(tr('Ajan zinciri sınırı'), hops, tr('Bir sohbetten doğan ajanlar arası mesajlaşmanın en çok kaç adım süreceği (1-20). Sonsuz gidip gelmeyi ve token israfını önler.')),
    field(tr('Varsayılan model'), model, tr('Ne ajan ne görev model belirtmişse bu kullanılır.')),
    button(tr('Kaydet'), { kind: 'primary', onClick: () => save({ max_concurrent: Number(concurrent.value) || 1, max_hops: Number(hops.value) || 8, default_model: model.value }) }));

  const budgetCard = h('div', { class: 'card' },
    h('div', { class: 'card-head' }, h('h3', null, tr('Kasa token bütçesi'))),
    field(tr('Günlük bütçe (token)'), daily, tr('Bütün ajanların bugünkü toplamı. 0 = sınırsız.')),
    field(tr('Haftalık bütçe (token)'), weekly, tr('Son 7 günün toplamı. 0 = sınırsız.')),
    usageNote,
    h('div', { class: 'hint' }, tr('Bütçe dolunca yalnızca zamanlı uyanışlar durur; sohbetler ve elle başlatılan işler engellenmez. Genel Bakış sayfasında uyarı görünür.')),
    button(tr('Kaydet'), { kind: 'primary', onClick: () => save({ daily_budget: Number(daily.value) || 0, weekly_budget: Number(weekly.value) || 0 }) }));

  // Telegram: agents with the "Telegram bildirimi" skill can message this chat. The token stays on this
  // computer (%APPDATA%\burrow\telegram.ini) and is never sent back to the page.
  const tgToken = h('input', { type: 'password', autocomplete: 'off' });
  const tgChat = h('input', { type: 'text', placeholder: tr('örn. 123456789') });
  const tgFail = h('input', { type: 'checkbox' });
  const tgStatus = h('div', { class: 'hint' });
  let tgLoaded = '';
  const tgArgs = () => ({ token: tgToken.value, chat_id: tgChat.value, notify_failures: tgFail.checked });

  async function tgFind() {
    try { const { id } = await call('telegram.detect', { token: tgToken.value }); tgChat.value = id; toast(tr('Sohbet kimliği bulundu.')); } catch (e) { showError(e); }
  }
  async function tgSave() {
    try { await call('telegram.set', tgArgs()); tgToken.value = ''; toast(tr('Kaydedildi.')); } catch (e) { showError(e); }
  }
  async function tgTest() {
    try { await call('telegram.test', tgArgs()); toast(tr('Test mesajı gönderildi.')); } catch (e) { showError(e); }
  }
  const telegramCard = h('div', { class: 'card' },
    h('div', { class: 'card-head' }, h('h3', null, 'Telegram')),
    h('div', { class: 'hint', style: { marginBottom: '10px' } }, tr('1) Telegram\'da @BotFather ile bir bot oluştur ve jetonu aşağı yapıştır. 2) Botuna bir mesaj yaz. 3) "Sohbet kimliğini bul"a bas. Sonra bir ajana "Telegram bildirimi" yeteneğini ver.')),
    field(tr('Bot jetonu'), tgToken),
    field(tr('Sohbet kimliği'), h('div', { style: { display: 'flex', gap: '8px' } }, tgChat, button(tr('Sohbet kimliğini bul'), { onClick: tgFind }))),
    h('label', { class: 'check-row' }, tgFail, ' ', tr('Zamanlı bir iş başarısız olursa bana haber ver')),
    tgStatus,
    h('div', { style: { display: 'flex', gap: '8px', marginTop: '10px' } },
      button(tr('Kaydet'), { kind: 'primary', onClick: tgSave }), button(tr('Test mesajı gönder'), { onClick: tgTest })));

  const el = h('div', { style: { display: 'grid', gap: '18px', maxWidth: '640px', alignContent: 'start' } },
    h('div', { class: 'card' },
      h('div', { class: 'card-head' }, h('h3', null, tr('Dil'))),
      field(tr('Arayüz dili'), language,
        tr('Bu ayar kasanın vault.ini dosyasında saklanır. "Kasa ayarlarını düzenleme" yeteneği olan bir ajan da değiştirebilir. Değişince sayfa yenilenir.'))),
    limitsCard, budgetCard, telegramCard,
    h('div', { class: 'card' }, info));

  return {
    el,
    refresh(snap) {
      const v = snap.vault;
      const sig = JSON.stringify([v.max_concurrent, v.max_hops, v.default_model, v.daily_budget, v.weekly_budget]);
      if (sig !== loaded && document.activeElement?.tagName !== 'INPUT') {  // never overwrite what is being typed
        loaded = sig;
        concurrent.value = v.max_concurrent; hops.value = v.max_hops; model.value = v.default_model;
        daily.value = v.daily_budget; weekly.value = v.weekly_budget;
      }
      const tg = snap.telegram;
      const tgSig = JSON.stringify([tg.chat_id, tg.notify_failures]);
      if (tgSig !== tgLoaded && document.activeElement?.tagName !== 'INPUT') { tgLoaded = tgSig; tgChat.value = tg.chat_id; tgFail.checked = tg.notify_failures; }
      tgToken.placeholder = tg.token_set ? tr('kayıtlı (değiştirmek için yeni jeton yaz)') : tr('123456:ABC...');
      tgStatus.textContent = tg.last_error ? tr('Son gönderim başarısız: {e}', { e: tg.last_error }) : tg.ready ? tr('Telegram hazır.') : tr('Henüz ayarlanmadı.');
      usageNote.textContent = tr('Şu ana kadar: bugün {a}, son 7 gün {b} token.', { a: fmtTokens(snap.budget.tokens_today), b: fmtTokens(snap.budget.tokens_7d) });
      info.replaceChildren(kv([[tr('Kasa adı'), snap.vault.name], [tr('Kasa klasörü'), snap.vault.root]]));
    },
  };
}
