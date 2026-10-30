// Things you can do with an agent, used by several pages: chat, run a task, create / edit / delete.
import { call } from './api.js';
import { state } from './state.js';
import {
  h, openModal, confirmBox, field, selectInput, textInput, button, toast, showError,
} from './ui.js';
import { SPECIES, COLORS, ACCESSORIES, PALETTE, mascotEl, hexColor } from './mascot.js';
import { tr } from './i18n.js';

function projectOptions(emptyLabel) {
  return [['', emptyLabel], ...state.snap.projects.map((p) => [p.id, p.name])];
}

export function startChat(agent, fixedProject) {
  const go = async (project) => {
    const { id } = await call('chat.new', { agent: agent.id, project });
    state.navigate('chats', { chat: id });
  };
  if (fixedProject !== undefined || !state.snap.projects.length) {
    go(fixedProject || '').catch(showError);
    return;
  }
  const project = selectInput(projectOptions(tr('(Proje yok)')), '');
  openModal({
    title: tr('{name} ile sohbet', { name: agent.name }),
    body: field(tr('Hangi projede? Ajan o projenin notlarını okur.'), project),
    actions: [{ label: tr('Vazgeç') }, { label: tr('Sohbeti başlat'), kind: 'primary', onClick: () => go(project.value) }],
  });
}

export function runNow(agent) {
  const task = selectInput([['', tr('(Ajanın kendi talimatı)')], ...state.snap.tasks.map((t) => [t.id, t.name])], '');
  const project = selectInput(projectOptions(tr('(Proje yok)')), '');
  openModal({
    title: tr('{name} hangi görevi yapsın?', { name: agent.name }),
    body: [field(tr('Görev'), task), state.snap.projects.length ? field(tr('Proje'), project) : null],
    actions: [
      { label: tr('Vazgeç') },
      {
        label: tr('Çalıştır'), kind: 'primary', iconName: 'play',
        onClick: async () => {
          await call('run.now', { agent: agent.id, task: task.value, project: project.value });
          toast(tr('{name} sıraya alındı', { name: agent.name }));
        },
      },
    ],
  });
}

export async function deleteAgent(agent) {
  const bosses = state.snap.agents.some((a) => a.reports_to === agent.id);
  const ok = await confirmBox(
    tr('"{name}" ajanı silinsin mi?', { name: agent.name }) + (bosses ? `\n\n${tr('Bu ajana bağlı ajanlar yöneticisiz kalacak.')}` : ''), tr('Sil'), tr('Ajanı sil'));
  if (ok) await call('agent.delete', { id: agent.id }).catch(showError);
  return ok;
}

// ---------- Editor ----------

export function openAgentEditor(existing) {
  const snap = state.snap;
  const a = existing ? structuredClone(existing) : {
    id: '', name: '', role: '', mascot: { species: 0, color: 0, accessory: 0 }, reports_to: '', model: '',
    enabled: true, daily_tokens: 0, skills: [], wakeups: [], persona: '',
  };

  const name = textInput(a.name, { placeholder: tr('Örn. Araştırmacı Ayşe') });
  const role = textInput(a.role, { placeholder: tr('Örn. Araştırmacı') });
  const boss = selectInput([['', tr('(Yönetici yok)')], ...snap.agents.filter((x) => x.id !== a.id).map((x) => [x.id, x.name])], a.reports_to);
  const model = textInput(a.model, { placeholder: tr('boş = Claude Code varsayılanı (haiku, sonnet, opus)') });
  const enabled = h('input', { type: 'checkbox', checked: a.enabled });
  const daily = h('input', { type: 'number', min: 0, value: a.daily_tokens });
  const persona = h('textarea', { rows: 6, placeholder: tr('Bu ajan kim, nasıl çalışır? Kısa tut: her çalıştırmada token olarak ödenir.') }, a.persona);
  const error = h('div', { class: 'err' });

  // Mascot picker with a live preview.
  const preview = h('div', { class: 'preview' });
  const redraw = () => { preview.replaceChildren(mascotEl(a.mascot, enabled.checked ? 'idle' : 'sleeping', 92)); };
  const speciesSel = selectInput(SPECIES.map((n, i) => [String(i), n]), String(a.mascot.species));
  const accSel = selectInput(ACCESSORIES.map((n, i) => [String(i), n]), String(a.mascot.accessory));
  speciesSel.addEventListener('change', () => { a.mascot.species = Number(speciesSel.value); redraw(); });
  accSel.addEventListener('change', () => { a.mascot.accessory = Number(accSel.value); redraw(); });
  enabled.addEventListener('change', redraw);
  const swatches = h('div', { class: 'swatches' });
  const drawSwatches = () => {
    swatches.replaceChildren(...PALETTE.map((_, i) => h('button', {
      class: `swatch ${a.mascot.color === i ? 'on' : ''}`, title: COLORS[i], style: { background: hexColor(i) },
      onClick: () => { a.mascot.color = i; drawSwatches(); redraw(); },
    })));
  };
  drawSwatches();
  redraw();

  // Skills.
  const skillChecks = snap.skills.map((s) => {
    const box = h('input', { type: 'checkbox', checked: a.skills.includes(s.id) });
    return { id: s.id, box, el: h('label', { class: 'check', title: s.description }, box, s.name) };
  });

  // Wake-ups.
  const wakeBox = h('div');
  const drawWakeups = () => {
    wakeBox.replaceChildren(...a.wakeups.map((w, i) => {
      const kind = selectInput([['time', tr('Her gün')], ['interval', tr('Her N dakikada')]], w.interval > 0 ? 'interval' : 'time');
      const when = h('input', w.interval > 0 ? { type: 'number', min: 1, value: w.interval, style: { width: '90px' } } : { type: 'time', value: w.time || '09:00', style: { width: '120px' } });
      const task = selectInput([['', tr('(ajanın kendi talimatı)')], ...snap.tasks.map((t) => [t.id, t.name])], w.task);
      const proj = selectInput(projectOptions(tr('(proje yok)')), w.project);
      kind.style.width = '160px';
      kind.addEventListener('change', () => { if (kind.value === 'interval') { w.interval = 30; w.time = ''; w.days = 0; } else { w.interval = 0; w.time = '09:00'; } drawWakeups(); });
      when.addEventListener('input', () => { if (w.interval > 0) w.interval = Number(when.value) || 1; else w.time = when.value; });
      task.addEventListener('change', () => { w.task = task.value; });
      proj.addEventListener('change', () => { w.project = proj.value; });
      const dayNames = [tr('Pzt'), tr('Sal'), tr('Çar'), tr('Per'), tr('Cum'), tr('Cmt'), tr('Paz')];
      const days = w.interval > 0 ? null : h('div', { class: 'wake-days', style: { display: 'flex', gap: '4px', flexBasis: '100%' } }, dayNames.map((name, d) => {
        const on = ((w.days || 0) >> d & 1) === 1;
        return button(name, { small: true, kind: on ? 'primary' : 'ghost', title: tr('Seçili gün yoksa her gün çalışır.'), onClick: () => { w.days = (w.days || 0) ^ (1 << d); drawWakeups(); } });
      }));
      return h('div', { class: 'wake', style: { flexWrap: 'wrap' } }, kind, when, h('div', { class: 'grow' }, task), snap.projects.length ? h('div', { class: 'grow' }, proj) : null,
        button('', { kind: 'ghost', iconName: 'trash', title: tr('Uyanışı sil'), onClick: () => { a.wakeups.splice(i, 1); drawWakeups(); } }), days);
    }));
    if (!a.wakeups.length) wakeBox.append(h('div', { class: 'muted' }, tr('Uyanış yok: ajan yalnızca sen çağırınca çalışır.')));
  };
  drawWakeups();

  openModal({
    title: existing ? tr('{name} — düzenle', { name: existing.name }) : tr('Yeni ajan'),
    wide: true,
    body: [
      h('div', { class: 'picker' }, preview, h('div', { class: 'opts' },
        h('div', { class: 'field-row' }, field(tr('Tür'), speciesSel), field(tr('Aksesuar'), accSel)),
        field(tr('Renk'), swatches))),
      h('div', { class: 'field-row' }, field(tr('Ad'), name), field(tr('Görev ünvanı'), role)),
      h('div', { class: 'field-row' }, field(tr('Yönetici'), boss), field(tr('Model'), model)),
      h('div', { class: 'field-row' },
        field(tr('Günlük token sınırı'), daily, tr('0 = sınırsız. Dolunca zamanlı uyanışlar atlanır; sohbetler engellenmez.')),
        h('div', { class: 'field' }, h('label', null, tr('Durum')), h('label', { class: 'check', style: { marginTop: '6px' } }, enabled, tr('Etkin (kapalıysa uykuda)')))),
      field(tr('Yetenekler'), h('div', { class: 'checks' }, skillChecks.length ? skillChecks.map((s) => s.el) : h('span', { class: 'muted' }, tr('Henüz yetenek yok.'))),
        tr('Ajan yalnızca verdiğin yeteneklerin araçlarını kullanabilir.')),
      field(tr('Uyanışlar'), h('div', null, wakeBox, button(tr('Uyanış ekle'), { small: true, iconName: 'plus', onClick: () => { a.wakeups.push({ time: '09:00', interval: 0, days: 0, task: '', project: '' }); drawWakeups(); } }))),
      field(tr('Kişilik / kalıcı talimat'), persona),
      error,
    ],
    actions: [
      { label: tr('Vazgeç') },
      {
        label: tr('Kaydet'), kind: 'primary',
        onClick: async () => {
          a.name = name.value; a.role = role.value; a.reports_to = boss.value; a.model = model.value;
          a.enabled = enabled.checked; a.daily_tokens = Number(daily.value) || 0; a.persona = persona.value;
          a.skills = skillChecks.filter((s) => s.box.checked).map((s) => s.id);
          try { await call('agent.save', a); } catch (e) { error.textContent = tr(e.message); return false; }
        },
      },
    ],
  });
}

