// Interface language. The Turkish text in the code is the key: tr('Kaydet') gives 'Save' in English
// and the same text in Turkish. {name} placeholders are filled from the second argument.
// The language is kept in the vault (vault.ini, "Language=tr|en"), so it is read from the program
// before any other module runs (top-level await); app.js reloads the page when it changes.
import { call, connected } from './api.js';
import EN from './i18n_en.js';

let code = 'tr';
if (connected) {
  try { code = (await call('bootstrap')).vault.language === 'en' ? 'en' : 'tr'; } catch { /* stays Turkish */ }
}
document.documentElement.lang = code;

export const lang = code;
export const locale = code === 'en' ? 'en-US' : 'tr-TR';

export function tr(text, vars) {
  let s = code === 'en' ? EN[text] ?? text : text;
  if (vars) s = s.replace(/\{(\w+)\}/g, (_, k) => vars[k] ?? '');
  return s;
}
