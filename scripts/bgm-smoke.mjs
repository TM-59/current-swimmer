// Local-only browser check of the music (M63 #33, parent TM-59/project-context#598). Not in CI.
//   make web && node scripts/bgm-smoke.mjs   (port CS_BGM_PORT, default 22080; record docs/bgm/smoke.json)
// On the built app it matches screen -> track -> sound at the output (RMS):
//  1) nothing is fetched before the first press; the title button starts the ballad (pond),
//     a pause ducks it to 0.4x
//  2) Swim upstream opens a scene: story music at 0.7x; answering it starts the reach (upstream)
//  3) mute during the crossfade silences the fading track too (<= -100 dBFS after 0.8 s,
//     TM-59/project-context#668); the ♪ button is 44 px and mutes and restores
//  4) the cue of the heavy scenes, the ending and a paused reach (pure, from web/bgm.js)
//  5) the size of dist (10 MB)

import assert from 'node:assert/strict';
import { existsSync } from 'node:fs';
import { readdir, readFile, stat, writeFile } from 'node:fs/promises';
import { createServer } from 'node:http';
import { createRequire } from 'node:module';
import os from 'node:os';
import path from 'node:path';
import { cue, DUCK, STORY_GAIN } from '../web/bgm.js';

const require = createRequire(import.meta.url);
/** @type {import('playwright').BrowserType | undefined} */
let chromium;
for (const name of [process.env.PLAYWRIGHT_MODULE, 'playwright'].filter((n) => n !== undefined)) {
  try {
    ({ chromium } = require(name));
    break;
  } catch {}
}
if (!chromium) throw Error('Install the locked dev dependency with npm ci (Playwright).');
const libs = path.join(os.homedir(), '.local/lib/chromium-deps/usr/lib/x86_64-linux-gnu');
if (existsSync(libs))
  process.env.LD_LIBRARY_PATH =
    libs + (process.env.LD_LIBRARY_PATH ? `:${process.env.LD_LIBRARY_PATH}` : '');

const port = Number(process.env.CS_BGM_PORT || 22080);
const server = createServer(async (req, res) => {
  try {
    const pathname = new URL(req.url ?? '/', 'http://localhost').pathname;
    const file = path.resolve('dist', decodeURIComponent(pathname.slice(1)) || 'index.html');
    assert(file.startsWith(path.resolve('dist') + path.sep));
    const type = file.endsWith('.wasm')
      ? 'application/wasm'
      : file.endsWith('.js')
        ? 'text/javascript'
        : '';
    res.writeHead(200, type ? { 'Content-Type': type } : {});
    res.end(await readFile(file));
  } catch {
    if (!res.headersSent) res.writeHead(404);
    res.end();
  }
});
await new Promise((resolve) => server.listen(port, '127.0.0.1', () => resolve(undefined)));

/** @type {{ checks: object[], ok?: boolean, error?: string, seconds?: number, when?: string, fetched?: string[] }} */
const out = { checks: [] };
const t0 = Date.now();
/** @param {string} dir @returns {Promise<number>} */
async function size(dir) {
  let n = 0;
  for (const f of await readdir(dir)) {
    const p = path.join(dir, f);
    const s = await stat(p);
    n += s.isDirectory() ? await size(p) : s.size;
  }
  return n;
}
const browser = await chromium.launch({
  headless: true,
  args: ['--no-sandbox', '--autoplay-policy=user-gesture-required'],
});
try {
  /** @type {string[]} */
  const errors = [];
  /** @type {string[]} */
  const fetched = [];
  const page = await browser.newPage({ viewport: { width: 1280, height: 800 } });
  page.on('pageerror', (e) => errors.push(String(e)));
  page.on('request', (r) => {
    if (r.url().includes('/bgm/')) fetched.push(r.url().split('/').pop() ?? '');
  });
  await page.goto(`http://127.0.0.1:${port}/`);
  await page.waitForFunction(() => window.swimmerView);
  const music = async () => {
    const m = await page.evaluate(() => window.swimmerView?.music);
    if (!m) throw Error('no music view');
    return m;
  };
  /** @param {string} track */
  const audible = async (track) => {
    await page.waitForFunction(
      (t) => {
        const m = window.swimmerView?.music;
        return m?.playing === t && m.loaded.includes(t);
      },
      track,
      { timeout: 60000, polling: 100 },
    );
    await page.waitForTimeout(2800); // the crossfade (2.5 s) is over
    let s = await music();
    for (let i = 0; i < 10; i++) {
      await page.waitForTimeout(250);
      const n = await music();
      if (n.level > s.level) s = n;
    }
    out.checks.push({
      what: `${track} plays`,
      cue: s.cue,
      level: +s.level.toFixed(1),
      gain: s.gain,
    });
    assert(s.running, 'AudioContext running');
    assert(s.level > -45, `${track}: ${s.level.toFixed(1)} dBFS`);
    return s;
  };

  // 1) title -> pond; nothing before the first press; pause ducks
  await page.waitForFunction(() => !document.querySelector('#start')?.hasAttribute('disabled'));
  await page.waitForTimeout(1000);
  assert.equal(fetched.length, 0, 'no music is fetched before the first press');
  out.checks.push({ what: 'nothing fetched before the first press', fetched: fetched.length });
  await page.click('#start');
  const p0 = await audible('pond');
  assert.equal(p0.cue.track, 'pond');
  await page.keyboard.press('p');
  await page.waitForFunction(() => window.swimmerView?.paused);
  await page.waitForTimeout(800);
  const p1 = await music();
  assert(Math.abs(p1.gain / p0.gain - DUCK) < 0.01, 'a pause ducks to 0.4x');
  assert(p1.level > -60 && p1.level < p0.level, 'still audible, quieter');
  out.checks.push({
    what: 'pause ducks',
    gain: [p0.gain, p1.gain],
    level: [+p0.level.toFixed(1), +p1.level.toFixed(1)],
  });
  await page.keyboard.press('p');

  // 2) a story scene -> story; answering it -> the reach (upstream)
  await page.click('#story');
  await page.waitForFunction(() => window.swimmerView?.journey.scene === 'outlet');
  const s0 = await audible('story');
  assert.equal(s0.cue.gain, STORY_GAIN);
  await page.click('#scene-options button');
  await page.waitForFunction(() => window.swimmerView?.journey.scene === null);
  await page.waitForFunction(() => window.swimmerView?.music.playing === 'upstream', null, {
    timeout: 60000,
    polling: 50,
  });

  // 3) mute in the middle of the crossfade: the fading story track goes silent too
  await page.keyboard.press('m');
  await page.waitForTimeout(800);
  const xm = await music();
  out.checks.push({
    what: 'mute during a crossfade silences the fading track too',
    level: +xm.level.toFixed(1),
  });
  assert(xm.muted && xm.level <= -100, `mute during crossfade: ${xm.level.toFixed(1)} dBFS`);
  await page.keyboard.press('m');
  await audible('upstream');
  const box = await page.locator('#mute').boundingBox();
  assert(box && box.width >= 44 && box.height >= 44, `♪ is 44 px: ${JSON.stringify(box)}`);
  await page.click('#mute');
  await page.waitForTimeout(800);
  const bm = await music();
  assert(bm.muted && bm.level <= -100, `♪ mutes: ${bm.level.toFixed(1)}`);
  await page.click('#mute');
  let bu = await music();
  for (let i = 0; i < 16 && bu.level <= -45; i++) {
    await page.waitForTimeout(250);
    bu = await music();
  }
  assert(!bu.muted && bu.level > -45, 'the ♪ button restores the music');
  out.checks.push({
    what: '♪ mutes and restores',
    size: box,
    level: [+bm.level.toFixed(1), +bu.level.toFixed(1)],
  });

  // 4) the cues of the scenes the check does not swim to
  const view = { mode: 'journey', paused: false, heavy: false, scene: null };
  assert.deepEqual(cue({ ...view, scene: 'reeds', heavy: true }), { track: 'story', gain: DUCK });
  assert.deepEqual(cue({ ...view, scene: 'surge', heavy: true }), { track: 'story', gain: DUCK });
  assert.deepEqual(cue({ ...view, scene: 'spring' }), { track: 'spring', gain: 1 });
  assert.deepEqual(cue({ ...view, paused: true }), { track: 'upstream', gain: DUCK });
  assert.deepEqual(cue({ ...view, mode: 'demo' }), { track: 'pond', gain: 1 });
  out.checks.push({
    what: 'cues: heavy scenes story 0.4x, ending spring, paused reach 0.4x, demo pond',
  });

  // 5) dist
  const bytes = await size('dist');
  out.checks.push({ what: 'dist bytes', bytes });
  assert(bytes <= 10_000_000, `dist ${bytes} > 10 MB`);
  out.fetched = [...new Set(fetched)];
  assert.deepEqual(errors, [], 'no page errors');
  out.ok = true;
  console.log('bgm-smoke OK', JSON.stringify(out.checks));
} catch (e) {
  out.ok = false;
  out.error = String(e instanceof Error ? e.stack : e);
  console.error(e);
  process.exitCode = 1;
} finally {
  out.seconds = +((Date.now() - t0) / 1000).toFixed(1);
  out.when = new Date().toISOString();
  await writeFile('docs/bgm/smoke.json', `${JSON.stringify(out, null, 1)}\n`);
  await browser.close();
  server.close();
}
