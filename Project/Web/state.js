// Latest program snapshot plus a few lookups every page needs.
import { tr, lang } from './i18n.js';
export const state = {
  snap: null,
  navigate: () => {},  // set by app.js: navigate('chats', { chat: id })
};

const subscribers = new Set();
export function subscribe(fn) { subscribers.add(fn); return () => subscribers.delete(fn); }
// Alphabetical in Turkish rules (Ç after C, İ and I apart), so lists stay easy to scan when they grow.
// Agents keep the order the program gives (bosses first, then their reports).
export const byName = (a, b) => a.name.localeCompare(b.name, lang, { sensitivity: 'base', numeric: true });

export function setSnapshot(snap) {
  for (const key of ['projects', 'skills', 'tasks', 'rules']) snap[key].sort(byName);
  state.snap = snap;
  subscribers.forEach((fn) => fn(snap));
}

export const agentById = (id) => state.snap?.agents.find((a) => a.id === id);
export const taskById = (id) => state.snap?.tasks.find((t) => t.id === id);
export const projectById = (id) => state.snap?.projects.find((p) => p.id === id);
export const agentName = (id) => (id === 'user' ? tr('Sen') : agentById(id)?.name || id);
