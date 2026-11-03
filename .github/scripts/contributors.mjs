// Gera o contributors.svg do README: o criador em cima, depois todo mundo que contribuiu, por quantidade.
// Conta commits e issues do GitHub (bug -> relato de bug, enhancement -> ideia, i18n -> tradução) e quem
// contribuiu pela Workshop (.github/contributors.json). O workflow contributors.yml publica a imagem no
// branch `contributors` (commit fora do main não entra na lista de contribuidores do GitHub). Local:
//   GITHUB_TOKEN=$(gh auth token) node .github/scripts/contributors.mjs [saída, padrão build/contributors.svg]
import { readFileSync, writeFileSync, mkdirSync } from 'node:fs';
import { dirname, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

const repo = process.env.GITHUB_REPOSITORY || 'LucasSKrewer/kenshi-achievements';
const creator = 'LucasSKrewer';
const excluded = new Set(['claude', 'github-actions[bot]', 'dependabot[bot]']);
const root = new URL('../../', import.meta.url);
const out = resolve(process.argv[2] || resolve(fileURLToPath(root), 'build/contributors.svg'));
const extra = JSON.parse(readFileSync(new URL('.github/contributors.json', root), 'utf8'));

const headers = { Accept: 'application/vnd.github+json', 'User-Agent': 'contributors-panel' };
if (process.env.GITHUB_TOKEN) headers.Authorization = `Bearer ${process.env.GITHUB_TOKEN}`;

async function all(path) {
  const items = [];
  for (let page = 1; ; ++page) {
    const res = await fetch(`https://api.github.com${path}${path.includes('?') ? '&' : '?'}per_page=100&page=${page}`, { headers });
    if (!res.ok) throw new Error(`${path}: ${res.status} ${await res.text()}`);
    const batch = await res.json();
    items.push(...batch);
    if (batch.length < 100) return items;
  }
}

// login (minúsculo) -> pessoa
const people = new Map();
const person = (login, avatar) => {
  const k = login.toLowerCase();
  if (!people.has(k)) people.set(k, { name: login, avatar, commits: 0, bugs: 0, ideas: 0, translations: 0, github: true });
  const p = people.get(k);
  if (!p.avatar) p.avatar = avatar;
  return p;
};

for (const c of await all(`/repos/${repo}/contributors`)) {
  if (c.type === 'User' && !excluded.has(c.login.toLowerCase())) person(c.login, c.avatar_url).commits = c.contributions;
}

// Issues e PRs de outras pessoas. PR com i18n conta como tradução; issue sem etiqueta conta como relato.
for (const i of await all(`/repos/${repo}/issues?state=all`)) {
  const login = i.user?.login;
  if (!login || login === creator || excluded.has(login.toLowerCase()) || i.user.type !== 'User') continue;
  const labels = new Set(i.labels.map(l => l.name));
  if (labels.has('invalid') || labels.has('duplicate')) continue;
  const p = person(login, i.user.avatar_url);
  if (labels.has('i18n') && i.pull_request) p.translations++;
  else if (i.pull_request) continue; // código já entra pelos commits
  else if (labels.has('enhancement')) p.ideas++;
  else p.bugs++;
}

for (const w of extra.workshop ?? []) {
  people.set(`workshop:${w.name.toLowerCase()}`, {
    name: w.name, commits: 0, bugs: w.bugs ?? 0, ideas: w.ideas ?? 0, translations: w.translations ?? 0, workshop: true,
  });
}

const total = p => p.commits + p.bugs + p.ideas + p.translations;
const me = people.get(creator.toLowerCase());
const others = [...people.entries()].filter(([k]) => k !== creator.toLowerCase()).map(([, p]) => p)
  .filter(p => total(p) > 0)
  .sort((a, b) => total(b) - total(a) || a.name.localeCompare(b.name));

// O que a pessoa fez, sem números (os commits de uma tradução não viram "Code")
const word = (n, one, many) => (n === 1 ? one : many);
function subtitle(p) {
  const parts = [];
  if (p.translations) parts.push(word(p.translations, 'Translation', 'Translations'));
  else if (p.commits) parts.push('Code');
  if (p.ideas) parts.push(word(p.ideas, 'Idea', 'Ideas'));
  if (p.bugs) parts.push(word(p.bugs, 'Bug report', 'Bug reports'));
  return parts.join(' · ');
}

async function dataUri(url) {
  if (!url) return null;
  const res = await fetch(`${url}${url.includes('?') ? '&' : '?'}s=96`);
  if (!res.ok) return null;
  const type = res.headers.get('content-type') || 'image/png';
  return `data:${type};base64,${Buffer.from(await res.arrayBuffer()).toString('base64')}`;
}

// Transparente, com cores de tom médio que leem nos dois temas do GitHub
const W = 960, CARD = 240, AV = 56, ROW = 92, PER_ROW = 4;
const muted = '#8a8f98', accent = '#d29922', font = 'Segoe UI, -apple-system, Helvetica, Arial, sans-serif';
const esc = s => String(s).replace(/[&<>"]/g, c => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;' }[c]));
const cut = (s, n) => (s.length > n ? `${s.slice(0, n - 1)}…` : s);

let clip = 0;
async function card(p, x, y, role) {
  const id = `a${clip++}`;
  const cx = x + AV / 2, cy = y + AV / 2;
  const img = p.github ? await dataUri(p.avatar) : null;
  const pic = img
    ? `<clipPath id="${id}"><circle cx="${cx}" cy="${cy}" r="${AV / 2}"/></clipPath>`
      + `<image href="${img}" x="${x}" y="${y}" width="${AV}" height="${AV}" clip-path="url(#${id})"/>`
    : `<circle cx="${cx}" cy="${cy}" r="${AV / 2}" fill="${muted}" fill-opacity="0.25"/>`
      + `<text x="${cx}" y="${cy + 7}" font-size="22" text-anchor="middle" fill="${muted}">${esc(p.name[0].toUpperCase())}</text>`;
  const tx = x + AV + 12;
  return `${pic}`
    + `<text x="${tx}" y="${y + 22}" font-size="15" font-weight="600" fill="${accent}">${esc(cut(p.name, 21))}</text>`
    + `<text x="${tx}" y="${y + 42}" font-size="12.5" fill="${muted}">${esc(cut(role, 26))}</text>`;
}

const parts = [];
const rows = Math.ceil(others.length / PER_ROW);
const H = 24 + ROW + rows * ROW + 8;
// Sem a contagem de commits do criador: mudaria a cada push e o workflow commitaria toda vez
if (me) parts.push(await card(me, (W - CARD) / 2, 16, 'Creator'));
for (let i = 0; i < others.length; ++i) {
  const row = Math.floor(i / PER_ROW), inRow = Math.min(PER_ROW, others.length - row * PER_ROW);
  const x = (W - inRow * CARD) / 2 + (i % PER_ROW) * CARD;
  const p = others[i];
  parts.push(await card(p, x, 16 + ROW + row * ROW, p.workshop ? `Workshop · ${subtitle(p)}` : subtitle(p)));
}

const svg = `<svg xmlns="http://www.w3.org/2000/svg" width="${W}" height="${H}" viewBox="0 0 ${W} ${H}" font-family="${font}">\n`
  + `${parts.join('\n')}\n</svg>\n`;
mkdirSync(dirname(out), { recursive: true });
writeFileSync(out, svg);
console.log(`contributors.svg: ${others.length + (me ? 1 : 0)} people`);
for (const p of [me, ...others].filter(Boolean)) console.log(`  ${p.name}: ${p === me ? 'creator' : subtitle(p)}`);
