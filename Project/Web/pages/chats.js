import { call } from '../api.js';
import { state, agentById } from '../state.js';
import { h, clear, button, field, selectInput, openModal, confirmBox, toast, showError, relTime } from '../ui.js';
import { mascotEl } from '../mascot.js';
import { renderMarkdown } from '../markdown.js';
import { openForm } from '../forms.js';
import { startChat } from '../agent_actions.js';
import { tr } from '../i18n.js';

function avatar(id, size = 40) {
  const a = agentById(id);
  if (a) return mascotEl(a.mascot, a.state, size);
  return h('div', { class: 'avatar', style: { width: `${size}px`, height: `${size}px`, fontWeight: 700, color: 'var(--accent)' } }, id === 'user' ? 'S' : '?');
}

export default function create() {
  let list = [];
  let selected = null;
  let conv = null;
  let lastSig = '';
  const filters = { kind: 'all', agent: '', project: '', q: '' };

  // ----- Side panel -----
  const kindSel = selectInput([['all', tr('Tüm konuşmalar')], ['chat', tr('Benim sohbetlerim')], ['agents', tr('Yalnızca ajanlar arası')]], 'all');
  const agentSel = h('select');
  const projectSel = h('select');
  const search = h('input', { type: 'search', placeholder: tr('Ara…') });
  const listBox = h('div', { class: 'chat-list' });
  const side = h('div', { class: 'chat-side' },
    h('div', { class: 'chat-filters' },
      button(tr('Yeni sohbet'), { kind: 'primary', iconName: 'plus', onClick: newChat }),
      kindSel, h('div', { class: 'row' }, agentSel, projectSel), search),
    listBox);

  // ----- Main panel -----
  const head = h('div', { class: 'chat-head' });
  const transcript = h('div', { class: 'transcript' });
  const composerBox = h('div');
  const mainBox = h('div', { class: 'chat-main' }, head, transcript, composerBox);
  const el = h('div', { class: 'chats' }, side, mainBox);

  const input = h('textarea', { rows: 1, placeholder: tr('Mesaj yaz… (Enter gönderir, Shift+Enter yeni satır)') });
  const sendBtn = button(tr('Gönder'), { kind: 'primary', iconName: 'send', onClick: send });
  input.addEventListener('keydown', (e) => { if (e.key === 'Enter' && !e.shiftKey) { e.preventDefault(); send(); } });
  input.addEventListener('input', () => { input.style.height = 'auto'; input.style.height = `${Math.min(input.scrollHeight, 180)}px`; });

  function newChat() {
    const agents = state.snap.agents;
    if (!agents.length) { toast(tr('Önce bir ajan oluştur.'), true); return; }
    const pick = selectInput(agents.map((a) => [a.id, a.name]), agents[0].id);
    openModal({
      title: tr('Kiminle sohbet edilsin?'),
      body: field(tr('Ajan'), pick),
      actions: [{ label: tr('Vazgeç') }, { label: tr('Devam'), kind: 'primary', onClick: () => startChat(agentById(pick.value)) }],
    });
  }

  async function send() {
    const text = input.value.trim();
    if (!text || !selected || !conv?.writable) return;
    input.value = '';
    input.style.height = 'auto';
    try { await call('chat.send', { id: selected, text }); } catch (e) { input.value = text; showError(e); }
    await loadConversation(selected, true);
  }

  // ----- List -----
  function visible() {
    const q = filters.q.trim().toLowerCase();
    return list.filter((c) => {
      if (filters.kind !== 'all' && c.kind !== filters.kind) return false;
      if (filters.agent && !c.participants.some((p) => p.id === filters.agent)) return false;
      if (filters.project && c.project !== filters.project) return false;
      if (q && !`${c.title} ${c.project_name} ${c.participants.map((p) => p.name).join(' ')}`.toLowerCase().includes(q)) return false;
      return true;
    });
  }

  function drawFilters(snap) {
    const fill = (sel, first, items, value) => {
      sel.replaceChildren(h('option', { value: '' }, first), ...items.map(([v, n]) => h('option', { value: v }, n)));
      sel.value = items.some(([v]) => v === value) ? value : '';
    };
    fill(agentSel, tr('Tüm ajanlar'), snap.agents.map((a) => [a.id, a.name]), filters.agent);
    fill(projectSel, tr('Tüm projeler'), snap.projects.map((p) => [p.id, p.name]), filters.project);
    filters.agent = agentSel.value;
    filters.project = projectSel.value;
  }

  function drawList() {
    const shown = visible();
    if (!shown.some((c) => c.id === selected)) {
      selected = shown[0]?.id || null;
      conv = null;
    }
    clear(listBox);
    if (!shown.length) listBox.append(h('div', { class: 'empty' }, list.length ? tr('Filtreye uyan konuşma yok.') : tr('Henüz konuşma yok.')));
    for (const c of shown) {
      const pic = c.kind === 'chat'
        ? avatar(c.participants[0]?.id, 40)
        : h('div', { class: 'pair' }, avatar(c.participants[0]?.id, 28), avatar(c.participants[1]?.id || c.participants[0]?.id, 28));
      listBox.append(h('div', { class: `chat-item ${c.id === selected ? 'sel' : ''}`, onClick: () => open(c.id) },
        pic,
        h('div', { class: 'meta' },
          h('div', { class: 't' }, c.title),
          h('div', { class: 's' }, c.kind === 'chat' ? c.participants[0]?.name || '?' : c.participants.map((p) => p.name).join(' ↔ '), c.project_name ? ` · ${c.project_name}` : '')),
        c.active ? h('span', { class: 'chip working plain' }, '●') : h('span', { class: 'when' }, relTime(c.updated))));
    }
  }

  // ----- Conversation -----
  async function loadConversation(id, forceScroll) {
    if (!id) { conv = null; drawConversation(true); return; }
    try { conv = await call('chat.load', { id }); } catch { conv = null; }
    drawConversation(forceScroll);
  }

  // Between agents every bubble also says who it is addressed to.
  function bubble(m, between) {
    const to = between && m.to && m.to !== 'user' ? agentById(m.to)?.name || m.to : '';
    return h('div', { class: `msg ${m.from === 'user' ? 'me' : ''}` },
      avatar(m.from, 40),
      h('div', { class: 'bubble' },
        h('div', { class: 'who-line' }, h('b', null, m.from === 'user' ? tr('Sen') : m.from_name), to ? [h('span', { class: 'arrow' }, '→'), to] : null, h('span', null, relTime(m.time))),
        h('div', { class: 'md', html: renderMarkdown(m.text) })));
  }

  function drawConversation(forceScroll) {
    const sig = conv ? `${conv.id}|${conv.messages.length}|${conv.busy}|${conv.title}` : 'none';
    if (sig === lastSig && !forceScroll) return;
    const nearBottom = transcript.scrollHeight - transcript.scrollTop - transcript.clientHeight < 80;
    const switched = !lastSig.startsWith(conv ? `${conv.id}|` : 'none');
    lastSig = sig;

    if (!conv) {
      clear(head);
      clear(transcript).append(h('div', { class: 'empty', style: { margin: 'auto' } }, h('div', { class: 'mascot-wrap' }), tr('Soldan bir konuşma seç ya da yeni sohbet başlat.')));
      clear(composerBox);
      return;
    }
    const between = conv.kind !== 'chat';
    const who = between ? conv.participants.map((p) => p.name).join(' ↔ ') : conv.participants[0]?.name || '?';
    clear(head).append(
      between ? h('div', { class: 'pair' }, avatar(conv.participants[0]?.id, 28), avatar(conv.participants[1]?.id || conv.participants[0]?.id, 28)) : avatar(conv.participants[0]?.id, 44),
      h('div', { class: 'grow' }, h('div', { class: 'tt' }, conv.title),
        h('div', { class: 'ss' }, conv.project_name ? tr('{who} · Proje: {project} · {n} mesaj', { who, project: conv.project_name, n: conv.messages.length }) : tr('{who} · {n} mesaj', { who, n: conv.messages.length }))),
      button('', { kind: 'ghost', iconName: 'edit', title: tr('Yeniden adlandır'), onClick: rename }),
      button('', { kind: 'ghost', iconName: 'download', title: tr('Markdown olarak dışa aktar'), onClick: exportChat }),
      button('', { kind: 'ghost', iconName: 'trash', title: tr('Sil'), onClick: remove }));

    clear(transcript);
    if (!conv.messages.length) transcript.append(h('div', { class: 'empty', style: { margin: 'auto' } }, tr('Henüz mesaj yok. İlk mesajı sen yaz.')));
    for (const m of conv.messages) transcript.append(bubble(m, between));
    if (conv.busy) {
      transcript.append(h('div', { class: 'typing' }, avatar(conv.participants[0]?.id, 34),
        h('span', null, between ? tr('Ajanlar konuşuyor') : tr('{name} yazıyor', { name: conv.participants[0]?.name })), h('span', { class: 'dots' }, h('span'), h('span'), h('span'))));
    }
    if (forceScroll || switched || nearBottom) transcript.scrollTop = transcript.scrollHeight;

    clear(composerBox);
    if (conv.writable) {
      input.disabled = false;
      composerBox.append(h('div', { class: 'composer' }, input, sendBtn));
      if (switched) input.focus();
    } else {
      composerBox.append(h('div', { class: 'hint-bar' }, between ? tr('Ajanlar arası konuşma salt okunur: ajanlar kendi aralarında yazışır.') : tr('{name} yazıyor…', { name: conv.participants[0]?.name })));
    }
  }

  function rename() {
    openForm({
      title: tr('Sohbeti adlandır'),
      fields: [{ key: 'title', label: tr('Başlık') }],
      values: { title: conv.title },
      onSave: async (v) => { await call('chat.rename', { id: conv.id, title: v.title }); await refreshAll(); },
    });
  }

  async function exportChat() {
    const { filename, markdown } = await call('chat.export', { id: conv.id });
    const url = URL.createObjectURL(new Blob([markdown], { type: 'text/markdown' }));
    h('a', { href: url, download: filename }).click();
    setTimeout(() => URL.revokeObjectURL(url), 5000);
  }

  async function remove() {
    if (!(await confirmBox(tr('Bu konuşma silinsin mi?'), tr('Sil'), tr('Konuşmayı sil')))) return;
    await call('chat.delete', { id: conv.id }).catch(showError);
    selected = null;
    conv = null;
    await refreshAll();
  }

  async function open(id) {
    selected = id;
    drawList();
    await loadConversation(id, true);
  }

  async function refreshAll() {
    list = await call('chats.list');
    drawList();
    if (selected && (!conv || conv.id !== selected)) await loadConversation(selected, true);
    else if (selected) await loadConversation(selected, false);
    else await loadConversation(null, true);
  }

  kindSel.addEventListener('change', () => { filters.kind = kindSel.value; drawList(); loadConversation(selected, true); });
  agentSel.addEventListener('change', () => { filters.agent = agentSel.value; drawList(); loadConversation(selected, true); });
  projectSel.addEventListener('change', () => { filters.project = projectSel.value; drawList(); loadConversation(selected, true); });
  search.addEventListener('input', () => { filters.q = search.value; drawList(); loadConversation(selected, true); });

  return {
    el,
    refresh(snap) { drawFilters(snap); refreshAll().catch(showError); },
    async open(params) {
      if (!params.chat) return;
      Object.assign(filters, { kind: 'all', agent: '', project: '', q: '' });
      kindSel.value = 'all'; search.value = '';
      list = await call('chats.list');
      drawFilters(state.snap);
      await open(params.chat);
    },
  };
}
