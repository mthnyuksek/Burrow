// Agent mascots drawn as SVG. 100 x 100 box.
import { tr } from './i18n.js';
export const SPECIES = [tr('Bulut'), tr('Kedi'), tr('Robot'), tr('Ayı'), tr('Hayalet')];
export const COLORS = [tr('Turuncu'), tr('Kırmızı'), tr('Yeşil'), tr('Mavi'), tr('Mor'), tr('Pembe'), tr('Limon'), tr('Gri')];
export const ACCESSORIES = [tr('Yok'), tr('Gözlük'), tr('Silindir şapka'), tr('Papyon')];
export const PALETTE = [
  [244, 162, 97], [231, 111, 81], [42, 157, 143], [77, 150, 255],
  [155, 93, 229], [241, 91, 181], [128, 185, 24], [141, 153, 174],
];

const clamp = (v, n) => Math.max(0, Math.min(n - 1, v | 0));
// Lightness rule: above 100 mixes with white, below 100 darkens.
function lighten([r, g, b], pct) {
  const f = (c) => Math.round(pct > 100 ? c + ((255 - c) * (pct - 100)) / 100 : (c * pct) / 100);
  return `rgb(${f(r)},${f(g)},${f(b)})`;
}
const el = (x, y, w, h, fill, extra = '') => `<ellipse cx="${x + w / 2}" cy="${y + h / 2}" rx="${w / 2}" ry="${h / 2}" fill="${fill}" ${extra}/>`;
const rr = (x, y, w, h, r, fill) => `<rect x="${x}" y="${y}" width="${w}" height="${h}" rx="${r}" fill="${fill}"/>`;
const tri = (pts, fill) => `<polygon points="${pts.join(' ')}" fill="${fill}"/>`;

export function hexColor(i) {
  const [r, g, b] = PALETTE[clamp(i, PALETTE.length)];
  return `rgb(${r},${g},${b})`;
}

// state: 'working' | 'idle' | 'sleeping' | 'limit' (limit looks awake)
export function mascotSvg(spec, state = 'idle', size = 64) {
  const awake = state !== 'sleeping';
  const species = clamp(spec.species, SPECIES.length);
  const raw = PALETTE[clamp(spec.color, PALETTE.length)];
  const base = lighten(raw, awake ? 100 : 135);
  const dark = lighten(raw, awake ? 70 : 95);
  const light = lighten(raw, 125);
  const ink = 'rgb(40,40,52)';
  let eyeY = 52;
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
    default:
      s += el(12, 20, 76, 72, base);
  }

  if (awake) {
    for (const ex of [36, 64]) s += el(ex - 8, eyeY - 8, 16, 16, '#fff') + el(ex - 4, eyeY - 3, 8, 8, ink);
    s += `<path d="M41 ${eyeY + 15} Q50 ${eyeY + 24} 59 ${eyeY + 15}" stroke="${ink}" stroke-width="2.5" fill="none" stroke-linecap="round"/>`;
  } else {
    for (const ex of [36, 64]) s += `<path d="M${ex - 8} ${eyeY} Q${ex} ${eyeY + 8} ${ex + 8} ${eyeY}" stroke="${ink}" stroke-width="2.5" fill="none" stroke-linecap="round"/>`;
    s += el(46, eyeY + 14, 8, 6, ink);
  }
  s += el(21, eyeY + 8, 11, 7, 'rgba(255,120,120,0.4)') + el(68, eyeY + 8, 11, 7, 'rgba(255,120,120,0.4)');

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
  }

  if (!awake) {
    s += `<g class="zz" fill="rgb(130,140,160)" font-weight="700" font-family="Segoe UI, sans-serif"><text x="78" y="15" font-size="13">z</text><text x="86" y="3" font-size="13">Z</text></g>`;
  } else if (state === 'working') {
    s += rr(60, -6, 40, 20, 10, '#fafafa') + el(56, 12, 6, 6, '#fafafa');
    s += [70, 80, 90].map((dx) => `<circle class="dot" cx="${dx}" cy="4" r="3" fill="${ink}"/>`).join('');
  }
  const cls = state === 'limit' ? 'idle' : state;
  return `<svg class="mascot ${cls}" width="${size}" height="${size}" viewBox="-4 -10 108 112" xmlns="http://www.w3.org/2000/svg">${s}</svg>`;
}

export function mascotEl(spec, state = 'idle', size = 64) {
  const wrap = document.createElement('span');
  wrap.className = 'mascot-wrap';
  wrap.style.display = 'inline-block';
  wrap.style.lineHeight = '0';
  wrap.innerHTML = mascotSvg(spec, state, size);
  return wrap;
}
