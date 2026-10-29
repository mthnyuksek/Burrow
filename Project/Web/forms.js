// Generic editor dialog for simple records (task, skill, rule, project, note, chat title ...).
import { h, openModal, field, textInput, selectInput, showError } from './ui.js';
import { tr } from './i18n.js';

// fields: [{ key, label, type: 'text'|'textarea'|'number'|'select'|'checkbox', options, hint, rows, placeholder }]
// onSave(values) may throw; the dialog then stays open and shows the message.
export function openForm({ title, fields, values = {}, saveLabel = tr('Kaydet'), wide = false, onSave }) {
  const inputs = {};
  const body = [];
  for (const f of fields) {
    const v = values[f.key];
    let input;
    if (f.type === 'textarea') input = h('textarea', { rows: f.rows || 7, placeholder: f.placeholder }, v ?? '');
    else if (f.type === 'number') input = h('input', { type: 'number', min: f.min ?? 0, max: f.max, value: v ?? 0 });
    else if (f.type === 'select') input = selectInput(f.options, v ?? f.options[0][0]);
    else if (f.type === 'checkbox') input = h('input', { type: 'checkbox', checked: v ?? true });
    else input = textInput(v ?? '', { placeholder: f.placeholder });
    inputs[f.key] = input;
    body.push(f.type === 'checkbox'
      ? h('div', { class: 'field' }, h('label', { class: 'check' }, input, f.label), f.hint ? h('div', { class: 'hint' }, f.hint) : null)
      : field(f.label, input, f.hint));
  }
  const error = h('div', { class: 'err' });
  return openModal({
    title,
    wide,
    body: [...body, error],
    actions: [
      { label: tr('Vazgeç') },
      {
        label: saveLabel,
        kind: 'primary',
        onClick: async () => {
          const out = { ...values };
          for (const f of fields) {
            const i = inputs[f.key];
            out[f.key] = f.type === 'checkbox' ? i.checked : f.type === 'number' ? Number(i.value) || 0 : i.value;
          }
          try {
            await onSave(out);
          } catch (e) {
            error.textContent = tr(e.message);
            return false;  // keep the dialog open
          }
        },
      },
    ],
  });
}

export { showError };
