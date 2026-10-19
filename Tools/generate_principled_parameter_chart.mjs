import { mkdir, readFile, writeFile } from 'node:fs/promises';
import { dirname, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

const root = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const contractPath = resolve(root, 'Documentation/Schemas/NPRParameterContract.v1.json');
const outputPath = resolve(root, 'docs/public/images/principled-parameter-chart.svg');
const contract = JSON.parse(await readFile(contractPath, 'utf8'));

const groups = [
  { id: 'Diffuse', title: '明暗分界', tint: '#82989b' },
  { id: 'Specular', title: '高光', tint: '#c76762' },
  { id: 'Normal', title: '法线与形体', tint: '#d3a64f' },
  { id: 'Appearance.Rim', title: '边缘光', tint: '#58956b' },
  { id: 'Face', title: '面部专用', tint: '#e0a650' },
  { id: 'Hair', title: '头发专用', tint: '#4f9cb5' },
  { id: 'Geometry.Outline', title: '描边', tint: '#48a9b6' },
  { id: 'Appearance.MatCap', title: '材质外观 · MatCap', tint: '#c45e8d' },
  { id: 'Appearance.Color', title: '材质外观 · 颜色', tint: '#bd7845' },
];

const presentation = {
  TerminatorDotCenter: { name: '明暗交界位置', kind: 'terminator', color: '#899da0' },
  TerminatorDotHalfWidth: { name: '明暗过渡半宽', kind: 'soft-terminator', color: '#788f95' },
  SpecHalfAngleRad: { name: '高光覆盖角', kind: 'spec-size', color: '#c65e61' },
  SpecEdgeSoftness: { name: '高光边缘柔度', kind: 'spec-softness', color: '#d07956' },
  SpecPeakScale: { name: '高光亮度倍率', kind: 'spec-peak', color: '#bb4d50' },
  NormalDetailStrength: { name: '细节法线强度', kind: 'normal-detail', color: '#b99243' },
  MacroNormalBlend: { name: '形体法线融合', kind: 'macro-normal', color: '#d0a947', note: '需要资产提供引导法线' },
  RimGrazingAngleRad: { name: '边缘光延伸角', kind: 'rim-width', color: '#548b59' },
  RimEdgeHalfWidthRad: { name: '边缘光过渡半宽', kind: 'rim-softness', color: '#66a16c' },
  RimPeakScale: { name: '边缘光强度倍率', kind: 'rim-peak', color: '#497d4f' },
  FaceAngularBiasRad: { name: '面部明暗位置偏移', kind: 'terminator', color: '#d8a347' },
  FaceAngularHalfWidthRad: { name: '面部明暗过渡半宽', kind: 'soft-terminator', color: '#edb954' },
  HairShiftAngleRad: { name: '发丝高光偏转', kind: 'hair-shift', color: '#5298ac' },
  StrandAnisotropy: { name: '发束高光拉伸', kind: 'hair-stretch', color: '#54a8c0' },
  OutlineWidthPx: { name: '描边宽度', kind: 'outline', color: '#4aaabd', note: '全局映射尚未接通' },
  MatCapWeight: { name: 'MatCap 混合权重', kind: 'matcap', color: '#c65b86', note: '需要贴图与混合规则' },
  ChromaScale: { name: '颜色饱和度倍率', kind: 'chroma', color: '#bd7845', note: '颜色处理规则尚未定' },
};

const width = 1900;
const height = 2350;
const panel = { x: 54, y: 220, width: 1792 };
const labelX = 112;
const gridStartX = 740;
const cellWidth = 218;
const rowHeight = 84;
const groupHeight = 32;
const sphereRadius = 27;
const topHeaderHeight = 62;
const entries = contract.parameterDefinitions;
const byGroup = groups.map((group) => ({
  ...group,
  rows: entries.filter((entry) => entry.group === group.id),
})).filter((group) => group.rows.length > 0);
const groupedIds = new Set(byGroup.flatMap((group) => group.rows.map((entry) => entry.id)));

if (entries.length !== Object.keys(presentation).length) {
  throw new Error(`Chart labels cover ${Object.keys(presentation).length} parameters, contract contains ${entries.length}.`);
}
if (groupedIds.size !== entries.length) throw new Error('Chart groups do not cover every contract parameter.');
for (const entry of entries) {
  if (!presentation[entry.id]) throw new Error(`Missing chart label for ${entry.id}.`);
}

const esc = (value) => String(value).replaceAll('&', '&amp;').replaceAll('<', '&lt;').replaceAll('>', '&gt;').replaceAll('"', '&quot;');
const rgb = (hex) => hex.match(/[a-f\d]{2}/gi).map((part) => Number.parseInt(part, 16));
const hex = (parts) => `#${parts.map((part) => Math.round(Math.max(0, Math.min(255, part))).toString(16).padStart(2, '0')).join('')}`;
const mix = (a, b, amount) => hex(rgb(a).map((part, index) => part * (1 - amount) + rgb(b)[index] * amount));
const fmt = (value, entry) => {
  if (/radian/.test(entry.unit)) {
    const degrees = value * 180 / Math.PI;
    const shown = Number.isInteger(degrees) ? String(degrees) : degrees.toFixed(1).replace(/\.0$/, '');
    const signed = (entry.id === 'FaceAngularBiasRad' || entry.id === 'HairShiftAngleRad') && degrees > 0 ? '+' : '';
    return `${signed}${shown}°`;
  }
  if (entry.unit === 'output_resolution_pixel') return `${Number.isInteger(value) ? value : value.toFixed(1)} px`;
  if (entry.unit === 'unitless_signed_dot' || entry.unit === 'unitless_signed_dot_half_width') {
    return `${value > 0 && entry.id === 'TerminatorDotCenter' ? '+' : ''}${value.toFixed(2)}`;
  }
  if (entry.unit === 'linear_scene_multiplier' || entry.id === 'ChromaScale') return `${value.toFixed(2)}×`;
  return value.toFixed(2);
};
const sampleValues = (entry) => {
  const range = entry.recommendedRange;
  if (!range || range.length !== 2 || range.some((value) => typeof value !== 'number')) {
    throw new Error(`Missing finite recommended range for ${entry.id}.`);
  }
  return Array.from({ length: 5 }, (_, index) => range[0] + (range[1] - range[0]) * index / 4);
};
const fmtDefault = (entry) => fmt(entry.schemaDefault, entry);

let y = panel.y + topHeaderHeight;
let body = '';
let definitions = '';
let sphereCounter = 0;

function addText(x, y, text, size, color, weight = 400, anchor = 'start', extra = '') {
  return `<text x="${x}" y="${y}" text-anchor="${anchor}" fill="${color}" font-size="${size}" font-weight="${weight}" ${extra}>${esc(text)}</text>`;
}

function sphereMarkup(entry, meta, t, cx, cy, rowIndex) {
  const current = sphereCounter++;
  const clip = `clip-${current}`;
  const grad = `grad-${current}`;
  const baseColor = meta.color;
  const normalColor = entry.id === 'ChromaScale'
    ? (t < 0.5 ? mix('#92999a', baseColor, t * 2) : mix(baseColor, '#ef6e36', (t - 0.5) * 0.35))
    : baseColor;
  const hi = mix(normalColor, '#ffffff', 0.66);
  const mid = mix(normalColor, '#ffffff', 0.12);
  const dark = mix(normalColor, '#081a20', 0.58);
  definitions += `<clipPath id="${clip}"><circle cx="${cx}" cy="${cy}" r="${sphereRadius}"/></clipPath>`;
  definitions += `<radialGradient id="${grad}" cx="30%" cy="24%" r="82%"><stop offset="0" stop-color="${hi}"/><stop offset="0.24" stop-color="${mid}"/><stop offset="0.76" stop-color="${normalColor}"/><stop offset="1" stop-color="${dark}"/></radialGradient>`;
  let detail = '';
  const clipOpen = `<g clip-path="url(#${clip})">`;
  const clipClose = '</g>';
  const shadow = (center, softness) => {
    const edge = Math.max(0.02, softness);
    const left = cx - sphereRadius;
    const centerX = left + center * sphereRadius * 2;
    const edgeX0 = Math.max(left, centerX - edge * sphereRadius * 2);
    const edgeX1 = Math.min(cx + sphereRadius, centerX + edge * sphereRadius * 2);
    return `<rect x="${left}" y="${cy - sphereRadius}" width="${Math.max(0, edgeX0 - left)}" height="${sphereRadius * 2}" fill="#0a1720" opacity="0.78"/><rect x="${edgeX0}" y="${cy - sphereRadius}" width="${Math.max(1, edgeX1 - edgeX0)}" height="${sphereRadius * 2}" fill="#101a21" opacity="0.58"/>`;
  };

  switch (meta.kind) {
    case 'terminator':
      detail = shadow(0.2 + t * 0.6, 0.055);
      break;
    case 'soft-terminator':
      detail = shadow(0.5, 0.02 + t * 0.22);
      break;
    case 'spec-size':
      detail = `<ellipse cx="${cx - 6}" cy="${cy - 8}" rx="${4 + t * 14}" ry="${3 + t * 9}" fill="#ffffff" opacity="0.82"/><ellipse cx="${cx - 2}" cy="${cy - 2}" rx="${9 + t * 17}" ry="${5 + t * 9}" fill="#ffffff" opacity="${0.08 + t * 0.2}"/>`;
      break;
    case 'spec-softness':
      detail = `<ellipse cx="${cx - 7}" cy="${cy - 8}" rx="${7 + t * 8}" ry="${5 + t * 5}" fill="#ffffff" opacity="${0.9 - t * 0.18}"/><ellipse cx="${cx - 4}" cy="${cy - 5}" rx="${11 + t * 9}" ry="${7 + t * 7}" fill="#ffffff" opacity="${0.05 + t * 0.2}"/>`;
      break;
    case 'spec-peak':
      detail = `<ellipse cx="${cx - 8}" cy="${cy - 9}" rx="9" ry="6" fill="#ffffff" opacity="${t * 0.94}"/><ellipse cx="${cx - 5}" cy="${cy - 5}" rx="16" ry="10" fill="#ffffff" opacity="${t * 0.24}"/>`;
      break;
    case 'normal-detail': {
      const amount = 0.1 + t * 0.62;
      detail = `<path d="M ${cx - 22} ${cy + 4} Q ${cx - 10} ${cy - 11} ${cx + 1} ${cy + 4} T ${cx + 22} ${cy + 2}" fill="none" stroke="#effffc" stroke-width="${1 + t * 2}" opacity="${amount}"/><path d="M ${cx - 18} ${cy + 15} Q ${cx - 4} ${cy + 4} ${cx + 13} ${cy + 15}" fill="none" stroke="#062b35" stroke-width="${1 + t * 2}" opacity="${amount}"/><circle cx="${cx + 14}" cy="${cy - 12}" r="${2 + t * 2}" fill="#f2fffb" opacity="${amount}"/>`;
      break;
    }
    case 'macro-normal':
      detail = `<ellipse cx="${cx - 2 + t * 7}" cy="${cy - 2}" rx="${17 + t * 9}" ry="${14 + t * 8}" fill="#e4fff5" opacity="${0.08 + t * 0.28}"/><path d="M ${cx - 20} ${cy + 12} Q ${cx - 4} ${cy - 6 - t * 4} ${cx + 17} ${cy - 15}" fill="none" stroke="#0a2026" stroke-width="${1 + t * 2}" opacity="${0.16 + t * 0.35}"/>`;
      break;
    case 'rim-width': {
      const strokeWidth = 2 + t * 8;
      detail = `<circle cx="${cx}" cy="${cy}" r="${sphereRadius - strokeWidth * 0.5}" fill="none" stroke="#c9ffb5" stroke-width="${strokeWidth}" opacity="0.9"/>`;
      break;
    }
    case 'rim-softness':
      detail = `<circle cx="${cx}" cy="${cy}" r="${sphereRadius - 4}" fill="none" stroke="#c6ffb9" stroke-width="${3 + t * 10}" opacity="${0.1 + t * 0.18}"/><circle cx="${cx}" cy="${cy}" r="${sphereRadius - 3}" fill="none" stroke="#efffe8" stroke-width="${1.5 + t * 2}" opacity="0.94"/>`;
      break;
    case 'rim-peak':
      detail = `<circle cx="${cx}" cy="${cy}" r="${sphereRadius - 3}" fill="none" stroke="#c9ffb5" stroke-width="5" opacity="${t * 0.96}"/>`;
      break;
    case 'hair-shift': {
      const angle = -20 + t * 40;
      detail = `<g transform="rotate(${angle} ${cx} ${cy})"><path d="M ${cx - 25} ${cy + 10} Q ${cx} ${cy - 4} ${cx + 25} ${cy - 11}" fill="none" stroke="#123c51" stroke-width="10" opacity="0.5"/><path d="M ${cx - 22} ${cy + 4} Q ${cx} ${cy - 10} ${cx + 21} ${cy - 17}" fill="none" stroke="#ffffff" stroke-width="5" opacity="0.92"/><path d="M ${cx - 20} ${cy + 10} Q ${cx} ${cy - 3} ${cx + 20} ${cy - 9}" fill="none" stroke="#d8ffff" stroke-width="2" opacity="0.9"/></g>`;
      break;
    }
    case 'hair-stretch':
      detail = `<path d="M ${cx - 25} ${cy + 13} Q ${cx - 3} ${cy - 4} ${cx + 24} ${cy - 14}" fill="none" stroke="#123b4d" stroke-width="${4 + t * 8}" opacity="0.55"/><path d="M ${cx - 23} ${cy + 8} Q ${cx - 2} ${cy - 8} ${cx + 22} ${cy - 18}" fill="none" stroke="#ffffff" stroke-width="${2 + t * 5}" opacity="0.94"/><ellipse cx="${cx - 12}" cy="${cy - 6}" rx="${5 + t * 8}" ry="${3 + t * 2}" fill="#f7ffff" opacity="0.88"/>`;
      break;
    case 'outline':
      detail = `<circle cx="${cx}" cy="${cy}" r="${sphereRadius - 2}" fill="none" stroke="#12313b" stroke-width="${t * 7}"/>`;
      break;
    case 'matcap':
      detail = `<ellipse cx="${cx + 5}" cy="${cy + 5}" rx="${3 + t * 22}" ry="${3 + t * 16}" fill="#ffadd3" opacity="${t * 0.55}"/><ellipse cx="${cx + 8}" cy="${cy + 7}" rx="${2 + t * 12}" ry="${2 + t * 10}" fill="#fff5df" opacity="${t * 0.55}"/>`;
      break;
    case 'chroma':
      detail = `<ellipse cx="${cx - 7}" cy="${cy - 7}" rx="8" ry="5" fill="#ffffff" opacity="0.78"/>`;
      break;
    default:
      throw new Error(`No sphere drawing style for ${entry.id}.`);
  }

  const gloss = meta.kind.startsWith('spec-') || meta.kind.startsWith('hair-') ? ''
    : `<ellipse cx="${cx - 9}" cy="${cy - 11}" rx="5" ry="3.3" transform="rotate(-28 ${cx - 9} ${cy - 11})" fill="#ffffff" opacity="0.74"/>`;
  return `<circle cx="${cx}" cy="${cy}" r="${sphereRadius}" fill="url(#${grad})" stroke="#ffffff" stroke-opacity="0.58" stroke-width="1.2"/>${clipOpen}${detail}${gloss}${clipClose}<circle cx="${cx}" cy="${cy}" r="${sphereRadius}" fill="none" stroke="#102b32" stroke-opacity="0.22" stroke-width="1"/>`;
}

for (const group of byGroup) {
  body += `<rect x="${panel.x + 18}" y="${y}" width="${panel.width - 36}" height="${groupHeight}" rx="8" fill="#e7efec"/>`;
  body += `<rect x="${panel.x + 18}" y="${y}" width="6" height="${groupHeight}" rx="3" fill="${group.tint}"/>`;
  body += addText(labelX, y + 22, group.title, 17, '#28454a', 700);
  const profileNames = [...new Set(group.rows.flatMap((entry) => entry.materialProfiles))];
  const profiles = profileNames.join(' / ');
  body += addText(panel.x + panel.width - 36, y + 22, `适用：${profiles}`, 13, '#60777a', 500, 'end');
  y += groupHeight;

  for (const entry of group.rows) {
    const meta = presentation[entry.id];
    const rowY = y;
    const centerY = rowY + 34;
    const defaultText = fmtDefault(entry);
    body += `<line x1="${panel.x + 28}" y1="${rowY + rowHeight}" x2="${panel.x + panel.width - 28}" y2="${rowY + rowHeight}" stroke="#e9eeeb" stroke-width="1"/>`;
    body += addText(labelX, rowY + 30, meta.name, 19, '#1f353a', 700);
    body += addText(labelX, rowY + 55, `${entry.id}  ·  默认 ${defaultText}`, 12.5, '#567073', 500);
    if (meta.note) {
      body += addText(labelX, rowY + 74, meta.note, 11.5, '#9a6e27', 600);
    }
    const values = sampleValues(entry);
    values.forEach((value, index) => {
      const cx = gridStartX + cellWidth * (index + 0.5);
      body += sphereMarkup(entry, meta, index / 4, cx, centerY, sphereCounter);
      body += addText(cx, rowY + 75, fmt(value, entry), 12.5, '#52666a', 500, 'middle');
    });
    y += rowHeight;
  }
}

const panelHeight = y - panel.y + 20;
const svg = `<?xml version="1.0" encoding="UTF-8"?>
<svg xmlns="http://www.w3.org/2000/svg" width="${width}" height="${height}" viewBox="0 0 ${width} ${height}" role="img" aria-labelledby="title desc">
  <title id="title">Principled NPR 参数变化方向概念示意</title>
  <desc id="desc">展示参数契约中 17 个 Principled 参数沿各自建议范围的五个档位变化。球体仅为帮助理解视觉方向的概念示意，不是 UE 实测结果。</desc>
  <defs>
    <linearGradient id="background" x1="0" y1="0" x2="1" y2="1"><stop stop-color="#172326"/><stop offset="1" stop-color="#243437"/></linearGradient>
    <linearGradient id="panel" x1="0" y1="0" x2="0" y2="1"><stop stop-color="#fbfcfa"/><stop offset="1" stop-color="#f3f7f4"/></linearGradient>
    <linearGradient id="teal" x1="0" y1="0" x2="1" y2="0"><stop stop-color="#358e86"/><stop offset="1" stop-color="#42b6a7"/></linearGradient>
    ${definitions}
  </defs>
  <rect width="${width}" height="${height}" fill="url(#background)"/>
  <path d="M0 0H${width}V12H0Z" fill="url(#teal)"/>
  ${addText(76, 88, 'Principled 参数如何改变画面？', 52, '#f5faf8', 750)}
  ${addText(78, 132, '我们按每项参数自己的建议范围，展示五个由低到高的档位。', 23, '#c8d7d3', 400)}
  <rect x="1458" y="50" width="366" height="50" rx="25" fill="#315752" stroke="#54aa9d" stroke-width="1"/>
  <circle cx="1486" cy="75" r="6" fill="#62cbbc"/>
  ${addText(1504, 82, '概念示意 · 不是 UE 实测画面', 17, '#e7f5f0', 650)}
  <rect x="${panel.x}" y="${panel.y}" width="${panel.width}" height="${panelHeight}" rx="20" fill="url(#panel)"/>
  ${addText(labelX, panel.y + 39, '视觉控制', 17, '#486064', 700)}
  ${addText(gridStartX, panel.y + 31, '建议范围低端', 12.5, '#60777a', 500, 'middle')}
  ${addText(gridStartX + cellWidth, panel.y + 31, '25%', 12.5, '#60777a', 500, 'middle')}
  ${addText(gridStartX + cellWidth * 2, panel.y + 31, '50%', 12.5, '#60777a', 500, 'middle')}
  ${addText(gridStartX + cellWidth * 3, panel.y + 31, '75%', 12.5, '#60777a', 500, 'middle')}
  ${addText(gridStartX + cellWidth * 4, panel.y + 31, '建议范围高端', 12.5, '#60777a', 500, 'middle')}
  <line x1="${panel.x + 26}" y1="${panel.y + 48}" x2="${panel.x + panel.width - 26}" y2="${panel.y + 48}" stroke="#dce5e1" stroke-width="1"/>
  ${body}
  <line x1="78" y1="2112" x2="1822" y2="2112" stroke="#526a6b" stroke-opacity="0.6"/>
  ${addText(78, 2150, `状态：Draft / 未标定 / 未接入 Principled 渲染。当前共 ${entries.length} 项，默认值与推荐范围直接读取参数契约。`, 16, '#e0ece8', 500)}
  ${addText(78, 2184, '球体用于解释参数方向，不代表已经接通或通过 UE 渲染验证。Face、Hair 与形体法线控制还依赖对应资产数据。', 14, '#b8cbc7', 400)}
  ${addText(78, 2215, '描边功能已有独立实现；图中的 OutlineWidthPx 尚未自动映射到该链路。', 14, '#b8cbc7', 400)}
  ${addText(1822, 2254, 'Principled NPR · 参数契约 v1', 13, '#94aaa6', 500, 'end')}
</svg>`;

if (process.argv.includes('--check')) {
  let existing = '';
  try { existing = await readFile(outputPath, 'utf8'); } catch {}
  if (existing !== svg) throw new Error('Parameter chart is stale or missing; run npm run docs:generate.');
  console.log(`Parameter chart check PASS: ${entries.length} contract parameters.`);
} else {
  await mkdir(dirname(outputPath), { recursive: true });
  await writeFile(outputPath, svg, 'utf8');
  console.log(`Wrote ${outputPath}`);
}
