// Agent mascots drawn as SVG. 100 x 100 box.
import { tr } from './i18n.js';
// Lists only grow at the end: agents store the index in their file.
export const SPECIES = [
  tr('Bulut'), tr('Kedi'), tr('Robot'), tr('Ayı'), tr('Hayalet'),
  tr('Tavşan'), tr('Baykuş'), tr('Kurbağa'), tr('Penguen'), tr('Ahtapot'),
];
export const COLORS = [
  tr('Turuncu'), tr('Kırmızı'), tr('Yeşil'), tr('Mavi'), tr('Mor'), tr('Pembe'), tr('Limon'), tr('Gri'),
  tr('Turkuaz'), tr('Sarı'), tr('Lacivert'),
  tr('Gün batımı'), tr('Okyanus'), tr('Orman'), tr('Aurora'), tr('Şeker'), tr('Alev'), tr('Gece'),
];
export const ACCESSORIES = [
  tr('Yok'), tr('Gözlük'), tr('Silindir şapka'), tr('Papyon'),
  tr('Taç'), tr('Kulaklık'), tr('Kovboy şapkası'), tr('Çiçek'),
];
// One rgb per solid color, two or three for a gradient (top left -> bottom right).
export const PALETTE = [
  [[244, 162, 97]], [[231, 111, 81]], [[42, 157, 143]], [[77, 150, 255]],
  [[155, 93, 229]], [[241, 91, 181]], [[128, 185, 24]], [[141, 153, 174]],
  [[64, 201, 198]], [[255, 200, 50]], [[58, 80, 170]],
  [[255, 138, 76], [236, 64, 122]],
  [[56, 189, 248], [99, 102, 241]],
  [[132, 204, 22], [13, 148, 136]],
  [[52, 211, 153], [168, 85, 247]],
  [[244, 114, 182], [129, 140, 248]],
  [[250, 204, 21], [239, 68, 68]],
  [[99, 102, 241], [168, 85, 247], [236, 72, 153]],
];

const clamp = (v, n) => Math.max(0, Math.min(n - 1, v | 0));
// Lightness rule: above 100 mixes with white, below 100 darkens.
function shade([r, g, b], pct) {
  const f = (c) => Math.round(pct > 100 ? c + ((255 - c) * (pct - 100)) / 100 : (c * pct) / 100);
  return `rgb(${f(r)},${f(g)},${f(b)})`;
}
const stopsCss = (stops, pct) => stops.map((c) => shade(c, pct));
const mid = (stops) => stops[stops.length >> 1];
let gradSeq = 0;
// Solid color -> plain rgb; gradient -> url(#id) and its <linearGradient> is added to defs.
function paint(stops, pct, defs) {
  const cols = stopsCss(stops, pct);
  if (cols.length === 1) return cols[0];
  const id = `mg${gradSeq++}`;
  const st = cols.map((c, i) => `<stop offset="${i / (cols.length - 1)}" stop-color="${c}"/>`).join('');
  defs.push(`<linearGradient id="${id}" gradientUnits="userSpaceOnUse" x1="12" y1="4" x2="88" y2="96">${st}</linearGradient>`);
  return `url(#${id})`;
}
const el = (x, y, w, h, fill, extra = '') => `<ellipse cx="${x + w / 2}" cy="${y + h / 2}" rx="${w / 2}" ry="${h / 2}" fill="${fill}" ${extra}/>`;
const rr = (x, y, w, h, r, fill) => `<rect x="${x}" y="${y}" width="${w}" height="${h}" rx="${r}" fill="${fill}"/>`;
const tri = (pts, fill) => `<polygon points="${pts.join(' ')}" fill="${fill}"/>`;

// CSS background for a swatch.
export function hexColor(i) {
  const cols = stopsCss(PALETTE[clamp(i, PALETTE.length)], 100);
  return cols.length === 1 ? cols[0] : `linear-gradient(135deg, ${cols.join(', ')})`;
}

// state: 'working' | 'idle' | 'sleeping' | 'limit' (limit looks awake)
export function mascotSvg(spec, state = 'idle', size = 64) {
  const awake = state !== 'sleeping';
  const species = clamp(spec.species, SPECIES.length);
  const stops = PALETTE[clamp(spec.color, PALETTE.length)];
  const defs = [];
  const base = paint(stops, awake ? 100 : 135, defs);
  const dark = paint(stops, awake ? 70 : 95, defs);
  const light = shade(mid(stops), 125);
  const ink = 'rgb(40,40,52)';
  const beak = 'rgb(244,162,40)';
  let eyeY = 52;
  let mouth = true;
  let s = el(22, 90, 56, 8, 'rgba(0,0,0,0.16)');

  switch (species) {
    case 1:
      s += tri([[18, 42], [22, 6], [46, 26]], base) + tri([[82, 42], [78, 6], [54, 26]], base);
      s += tri([[24, 34], [26, 16], [38, 27]], dark) + tri([[76, 34], [74, 16], [62, 27]], dark);
      s += rr(13, 22, 74, 68, 32, base);
      break;
    case 2:
      s += `<line x1="50" y1="22" x2="50" y2="9" stroke="${dark}" stroke-width="3"/>` + el(44, 2, 12, 12, dark);
      s += rr(8, 46, 9, 20, 3, dark) + rr(83, 46, 9, 20, 3, dark) + rr(15, 22, 70, 68, 14, base);
      break;
    case 3:
      s += el(12, 12, 24, 24, base) + el(64, 12, 24, 24, base) + el(18, 18, 12, 12, dark) + el(70, 18, 12, 12, dark);
      s += el(13, 20, 74, 72, base) + el(34, 56, 32, 24, light);
      break;
    case 4:
      eyeY = 46;
      s += el(16, 12, 68, 66, base) + `<rect x="16" y="44" width="68" height="42" fill="${base}"/>`;
      for (const cx of [27, 50, 73]) s += el(cx - 11, 76, 22, 20, base);
      break;
    case 5:
      s += el(24, -6, 18, 42, base) + el(58, -6, 18, 42, base) + el(29, 0, 8, 30, dark) + el(63, 0, 8, 30, dark);
      s += rr(13, 24, 74, 66, 31, base) + el(38, 62, 24, 18, light);
      break;
    case 6:
      s += tri([[16, 30], [18, 4], [40, 20]], base) + tri([[84, 30], [82, 4], [60, 20]], base);
      s += el(2, 40, 18, 40, dark) + el(80, 40, 18, 40, dark);
      s += el(11, 14, 78, 78, base) + el(28, 58, 44, 32, light);
      break;
    case 7:
      eyeY = 44;
      s += el(8, 30, 84, 62, base) + el(14, 10, 30, 30, base) + el(56, 10, 30, 30, base);
      s += el(22, 70, 56, 16, light);
      break;
    case 8:
      eyeY = 42;
      mouth = false;
      s += el(22, 86, 24, 10, beak) + el(54, 86, 24, 10, beak);
      s += el(12, 8, 76, 84, base) + el(28, 40, 44, 50, '#f6f6f6');
      s += tri([[42, 54], [58, 54], [50, 66]], beak);
      break;
    case 9:
      eyeY = 40;
      s += rr(14, 40, 72, 40, 6, base);
      for (const cx of [22, 41, 59, 78]) s += el(cx - 9, 66, 18, 28, base);
      s += el(12, 6, 76, 66, base);
      break;
    default:
      s += el(12, 20, 76, 72, base);
  }

  if (awake) {
    for (const ex of [36, 64]) s += el(ex - 8, eyeY - 8, 16, 16, '#fff') + el(ex - 4, eyeY - 3, 8, 8, ink);
    if (mouth) s += `<path d="M41 ${eyeY + 15} Q50 ${eyeY + 24} 59 ${eyeY + 15}" stroke="${ink}" stroke-width="2.5" fill="none" stroke-linecap="round"/>`;
  } else {
    for (const ex of [36, 64]) s += `<path d="M${ex - 8} ${eyeY} Q${ex} ${eyeY + 8} ${ex + 8} ${eyeY}" stroke="${ink}" stroke-width="2.5" fill="none" stroke-linecap="round"/>`;
    if (mouth) s += el(46, eyeY + 14, 8, 6, ink);
  }
  s += el(21, eyeY + 8, 11, 7, 'rgba(255,120,120,0.4)') + el(68, eyeY + 8, 11, 7, 'rgba(255,120,120,0.4)');

  const gold = 'rgb(250,204,21)';
  switch (spec.accessory | 0) {
    case 1:
      s += el(25, eyeY - 11, 22, 22, 'rgba(255,255,255,0.24)', `stroke="${ink}" stroke-width="2.5"`);
      s += el(53, eyeY - 11, 22, 22, 'rgba(255,255,255,0.24)', `stroke="${ink}" stroke-width="2.5"`);
      s += `<line x1="47" y1="${eyeY}" x2="53" y2="${eyeY}" stroke="${ink}" stroke-width="2.5"/>`;
      break;
    case 2:
      s += rr(31, -2, 38, 22, 3, 'rgb(52,52,72)') + rr(21, 16, 58, 6, 3, 'rgb(52,52,72)');
      s += `<rect x="31" y="11" width="38" height="5" fill="rgb(214,69,65)"/>`;
      break;
    case 3:
      s += tri([[50, 85], [35, 76], [35, 94]], 'rgb(214,69,65)') + tri([[50, 85], [65, 76], [65, 94]], 'rgb(214,69,65)') + el(46, 81, 8, 8, 'rgb(214,69,65)');
      break;
    case 4:
      s += `<polygon points="32,20 32,2 41,11 50,-3 59,11 68,2 68,20" fill="${gold}" stroke="rgb(200,150,10)" stroke-width="1.5" stroke-linejoin="round"/>`;
      s += el(47, 11, 6, 6, 'rgb(231,76,60)');
      break;
    case 5:
      s += `<path d="M16 50 Q16 6 50 6 Q84 6 84 50" stroke="rgb(52,52,72)" stroke-width="5" fill="none" stroke-linecap="round"/>`;
      s += rr(8, 40, 14, 26, 6, 'rgb(52,52,72)') + rr(78, 40, 14, 26, 6, 'rgb(52,52,72)');
      s += rr(10, 46, 5, 14, 2.5, 'rgb(77,150,255)') + rr(85, 46, 5, 14, 2.5, 'rgb(77,150,255)');
      break;
    case 6:
      s += rr(31, -6, 38, 20, 9, 'rgb(150,100,60)') + el(10, 8, 80, 16, 'rgb(120,78,44)');
      s += `<rect x="31" y="6" width="38" height="4" fill="rgb(214,69,65)"/>`;
      break;
    case 7:
      for (let k = 0; k < 5; k++) {
        const a = (k * 2 * Math.PI) / 5;
        s += `<circle cx="${(74 + Math.cos(a) * 6).toFixed(1)}" cy="${(22 + Math.sin(a) * 6).toFixed(1)}" r="5" fill="rgb(255,150,200)"/>`;
      }
      s += `<circle cx="74" cy="22" r="3.5" fill="${gold}"/>`;
      break;
  }

  if (!awake) {
    s += `<g class="zz" fill="rgb(130,140,160)" font-weight="700" font-family="Segoe UI, sans-serif"><text x="78" y="15" font-size="13">z</text><text x="86" y="3" font-size="13">Z</text></g>`;
  } else if (state === 'working') {
    s += rr(60, -6, 40, 20, 10, '#fafafa') + el(56, 12, 6, 6, '#fafafa');
    s += [70, 80, 90].map((dx) => `<circle class="dot" cx="${dx}" cy="4" r="3" fill="${ink}"/>`).join('');
  }
  const cls = state === 'limit' ? 'idle' : state;
  const defsXml = defs.length ? `<defs>${defs.join('')}</defs>` : '';
  return `<svg class="mascot ${cls}" width="${size}" height="${size}" viewBox="-4 -10 108 112" xmlns="http://www.w3.org/2000/svg">${defsXml}${s}</svg>`;
}

export function mascotEl(spec, state = 'idle', size = 64) {
  const wrap = document.createElement('span');
  wrap.className = 'mascot-wrap';
  wrap.style.display = 'inline-block';
  wrap.style.lineHeight = '0';
  wrap.innerHTML = mascotSvg(spec, state, size);
  return wrap;
}
