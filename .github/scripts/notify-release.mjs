// master に入ったエンジンの版を、ドキュメントの更新履歴の項目から Discord のお知らせにして送る
import { execFileSync } from 'node:child_process';
import { readFileSync } from 'node:fs';

const VERSION_FILE = 'Project/Engine/Src/EngineSystem/EngineVersion.h';
const DOCS_FILE = 'Project/Engine/Docs/index.html';
const LEAD_MAX = 500;
const WARNING_MAX = 600;
const HEADING_MAX = 12;
const RETRY_WAITS_MS = [2000, 5000, 15000];
const POST_INTERVAL_MS = 1000;

const KINDS = {
  major: { label: '大きな版', color: 0x8250df, changes: '変わったところ' },
  minor: { label: '機能追加', color: 0x2ea043, changes: '変わったところ' },
  patch: { label: '不具合の修正', color: 0xd29922, changes: '直したところ' },
  sample: { label: 'サンプルだけ', color: 0x8b949e, changes: '変わったところ' },
};
const BREAKING_COLOR = 0xcf222e;

const env = process.env;
const repository = env.GITHUB_REPOSITORY || 'tanahara0310/CoreEngine';
const dryRun = env.DRY_RUN === '1';

function sleep(ms) {
  return new Promise((resolve) => setTimeout(resolve, ms));
}

function git(args) {
  return execFileSync('git', args, { encoding: 'utf8', stdio: ['ignore', 'pipe', 'pipe'] }).trim();
}

function parseVersion(text) {
  const match = /kEngineVersion\s*=\s*"(\d+\.\d+\.\d+)"/.exec(text);
  return match ? match[1] : null;
}

function compareVersions(a, b) {
  const x = a.split('.').map(Number);
  const y = b.split('.').map(Number);
  for (let i = 0; i < 3; i++) {
    if (x[i] !== y[i]) return x[i] - y[i];
  }
  return 0;
}

function isCommit(sha) {
  if (!/^[0-9a-f]{40}$/.test(sha || '') || /^0+$/.test(sha)) return false;
  try {
    git(['cat-file', '-e', `${sha}^{commit}`]);
    return true;
  } catch {
    return false;
  }
}

function versionAt(rev) {
  try {
    return parseVersion(git(['show', `${rev}:${VERSION_FILE}`]));
  } catch {
    return null;
  }
}

// 版の番号を初めて書いたコミット
function findIntroducingCommit(version, range) {
  const out = git(['log', '--reverse', '--format=%H', `-S"${version}"`, range, '--', VERSION_FILE]);
  return out.split('\n')[0] || null;
}

function decodeEntities(text) {
  return text
    .replace(/&#x([0-9a-f]+);/gi, (_, hex) => String.fromCodePoint(parseInt(hex, 16)))
    .replace(/&#(\d+);/g, (_, dec) => String.fromCodePoint(Number(dec)))
    .replace(/&nbsp;/g, ' ')
    .replace(/&lt;/g, '<')
    .replace(/&gt;/g, '>')
    .replace(/&quot;/g, '"')
    .replace(/&#39;|&apos;/g, "'")
    .replace(/&amp;/g, '&');
}

function escapeMarkdown(text) {
  return text.replace(/[\\*_~|`]/g, '\\$&');
}

// HTML の断片を Discord の文に直す（コードとパスはバッククォートで囲む）
function toText(html) {
  const codes = [];
  const marked = html.replace(/<(?:code|span class="path")>([^<]*)<\/(?:code|span)>/g, (_, inner) => {
    codes.push(decodeEntities(inner).replace(/`/g, "'"));
    return `\u0000${codes.length - 1}\u0000`;
  });
  const plain = escapeMarkdown(decodeEntities(marked.replace(/<[^>]*>/g, '')));
  return plain
    .replace(/\u0000(\d+)\u0000/g, (_, index) => `\`${codes[Number(index)]}\``)
    .replace(/\s+/g, ' ')
    .trim();
}

function truncate(text, max) {
  if (text.length <= max) return text;
  const sentenceEnd = text.lastIndexOf('。', max - 1);
  if (sentenceEnd >= max / 2) return text.slice(0, sentenceEnd + 1);
  let cut = text.slice(0, max - 1);
  if ((cut.match(/(?<!\\)`/g) || []).length % 2 === 1) cut += '`';
  return `${cut}…`;
}

// 更新履歴の各版の見出し・最初の段落・太字の見出し・警告の囲みを読む
function readChangelog(html) {
  const entries = new Map();
  const heading = /<h3 id="changelog-v(\d+)-(\d+)-(\d+)">([\s\S]*?)<\/h3>/g;
  let match;
  while ((match = heading.exec(html)) !== null) {
    const version = `${match[1]}.${match[2]}.${match[3]}`;
    const rest = html.slice(heading.lastIndex);
    const end = rest.search(/<h[23][\s>]|<\/article>/);
    const body = end < 0 ? rest : rest.slice(0, end);
    const date = /（(\d{4}-\d{2}-\d{2})）/.exec(match[4]);
    const lead = /<p>([\s\S]*?)<\/p>/.exec(body);
    entries.set(version, {
      version,
      date: date ? date[1] : '',
      sampleOnly: /<span class="badge">サンプルだけ<\/span>/.test(match[4]),
      lead: lead ? toText(lead[1]) : '',
      headings: [...body.matchAll(/<li><strong>([\s\S]*?)<\/strong>/g)].map((h) => toText(h[1])),
      warnings: [...body.matchAll(/<div class="callout warn" data-title="([^"]*)">([\s\S]*?)<\/div>/g)]
        .map((w) => ({ title: toText(w[1]), text: toText(w[2]) })),
    });
  }
  return entries;
}

function previousVersion(versions, version) {
  return versions
    .filter((v) => compareVersions(v, version) < 0)
    .sort(compareVersions)
    .pop() ?? null;
}

function kindOf(entry, previous) {
  if (entry.sampleOnly) return KINDS.sample;
  if (!previous) return KINDS.minor;
  const [major, minor] = entry.version.split('.').map(Number);
  const [prevMajor, prevMinor] = previous.split('.').map(Number);
  if (major !== prevMajor) return KINDS.major;
  if (minor !== prevMinor) return KINDS.minor;
  return KINDS.patch;
}

async function github(path) {
  const headers = {
    Accept: 'application/vnd.github+json',
    'X-GitHub-Api-Version': '2022-11-28',
    'User-Agent': 'CoreEngine-release-notify',
  };
  if (env.GITHUB_TOKEN) headers.Authorization = `Bearer ${env.GITHUB_TOKEN}`;
  const res = await fetch(`https://api.github.com${path}`, { headers });
  if (!res.ok) {
    console.log(`::warning::GitHub API ${path} が ${res.status} を返しました`);
    return null;
  }
  return res.json();
}

// コミットを含む PR のうち、master へ最初にマージされたもの
async function findPullRequest(sha) {
  const pulls = await github(`/repos/${repository}/commits/${sha}/pulls`);
  if (!Array.isArray(pulls)) return null;
  const merged = pulls
    .filter((pull) => pull.merged_at)
    .sort((a, b) => a.merged_at.localeCompare(b.merged_at));
  return merged.find((pull) => pull.base?.ref === 'master') ?? merged[0] ?? null;
}

function fallbackEntry(version, pull) {
  return {
    version,
    date: '',
    sampleOnly: false,
    lead: pull ? escapeMarkdown(pull.title.replace(/\s*（v\d+\.\d+\.\d+）\s*$/, '')) : '',
    headings: [],
    warnings: [],
  };
}

function buildPayload(entry, kind, link, pull, roleId) {
  const breaking = entry.warnings.length > 0;
  const sections = [];
  if (entry.lead) sections.push(truncate(entry.lead, LEAD_MAX));
  if (entry.sampleOnly) sections.push('エンジンの機能は変わりません。付属のサンプルだけの更新です。');
  if (entry.headings.length > 0) {
    const shown = entry.headings.slice(0, HEADING_MAX);
    const more = entry.headings.length - shown.length;
    sections.push(`**${kind.changes}**：${shown.join('／')}${more > 0 ? `　ほか ${more} 件` : ''}`);
  }
  for (const warning of entry.warnings) {
    sections.push(`⚠️ **${warning.title}**\n${truncate(warning.text, WARNING_MAX)}`);
  }
  sections.push('取り込んだら Help → ドキュメントを開く の「更新履歴」で詳しく読めます。');

  const embed = {
    title: `CoreEngine v${entry.version} ― ${kind.label}`,
    url: link,
    description: sections.join('\n\n'),
    color: breaking ? BREAKING_COLOR : kind.color,
  };
  const footer = [pull ? `PR #${pull.number}` : '', entry.date].filter(Boolean).join(' ・ ');
  if (footer) embed.footer = { text: footer };

  const payload = { embeds: [embed], allowed_mentions: { parse: [] } };
  if (breaking && roleId) {
    payload.content = `<@&${roleId}> ゲーム側で直すことがある更新です`;
    payload.allowed_mentions.roles = [roleId];
  }
  return payload;
}

async function postDiscord(url, payload) {
  for (let attempt = 0; ; attempt++) {
    const res = await fetch(url, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(payload),
    });
    if (res.ok) return;
    const body = await res.text();
    const retryable = res.status === 429 || res.status >= 500;
    if (!retryable || attempt >= RETRY_WAITS_MS.length) {
      throw new Error(`Discord が ${res.status} を返しました: ${body.slice(0, 300)}`);
    }
    let wait = RETRY_WAITS_MS[attempt];
    try {
      const retryAfter = Number(JSON.parse(body).retry_after);
      if (retryAfter > 0) wait = Math.max(wait, retryAfter * 1000);
    } catch {
      // 本文が JSON でなければ決めた間隔で待つ
    }
    console.log(`Discord が ${res.status} を返したので ${wait} ms 待って送り直します`);
    await sleep(wait);
  }
}

// 送る版：指定があればその版、無ければ push の前の版より新しく今の版までの版
function selectVersions(changelog, current) {
  const requested = (env.VERSION || '').trim().replace(/^v/, '');
  if (requested) {
    if (!changelog.has(requested)) throw new Error(`更新履歴に v${requested} がありません`);
    return { versions: [requested], range: 'HEAD' };
  }
  const before = isCommit(env.BEFORE_SHA) ? versionAt(env.BEFORE_SHA) : null;
  if (!before) return { versions: [current], range: 'HEAD' };
  if (compareVersions(current, before) <= 0) {
    console.log(`版が v${before} → v${current} で上がっていないので送りません`);
    return { versions: [], range: 'HEAD' };
  }
  const versions = [...changelog.keys()]
    .filter((v) => compareVersions(v, before) > 0 && compareVersions(v, current) <= 0)
    .sort(compareVersions);
  if (!versions.includes(current)) versions.push(current);
  return { versions, range: `${env.BEFORE_SHA}..HEAD` };
}

async function main() {
  const webhook = (env.DISCORD_WEBHOOK_URL || '').trim();
  if (!webhook && !dryRun) {
    console.log('::warning::Secret の DISCORD_RELEASE_WEBHOOK_URL が無いので送りません');
    return;
  }
  let roleId = (env.DISCORD_ROLE_ID || '').trim();
  if (roleId && !/^\d+$/.test(roleId)) {
    console.log('::warning::DISCORD_RELEASE_ROLE_ID が数字でないので名指しを省きます');
    roleId = '';
  }

  const changelog = readChangelog(readFileSync(DOCS_FILE, 'utf8'));
  const current = parseVersion(readFileSync(VERSION_FILE, 'utf8'));
  if (!current) throw new Error(`${VERSION_FILE} から版を読めません`);

  const { versions, range } = selectVersions(changelog, current);
  const known = [...changelog.keys()];
  for (const [index, version] of versions.entries()) {
    const sha = findIntroducingCommit(version, range);
    const pull = sha ? await findPullRequest(sha) : null;
    const link = pull?.html_url
      ?? (sha ? `https://github.com/${repository}/commit/${sha}` : `https://github.com/${repository}`);
    const entry = changelog.get(version) ?? fallbackEntry(version, pull);
    const payload = buildPayload(entry, kindOf(entry, previousVersion(known, version)), link, pull, roleId);

    if (dryRun) {
      console.log(JSON.stringify(payload, null, 2));
      continue;
    }
    if (index > 0) await sleep(POST_INTERVAL_MS);
    await postDiscord(webhook, payload);
    console.log(`v${version} を送りました`);
  }
}

main().catch((error) => {
  console.log(`::error::${error.message}`);
  process.exit(1);
});
