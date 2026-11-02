// Local-only browser checks. Every screenshot comes from the built C/WASM app.
import {createRequire} from 'node:module';
import {createServer} from 'node:http';
import {readFile, mkdir, writeFile, stat} from 'node:fs/promises';
import {existsSync} from 'node:fs';
import path from 'node:path';
import os from 'node:os';
import assert from 'node:assert/strict';

const require = createRequire(import.meta.url);
let chromium;
for (const name of [process.env.PLAYWRIGHT_MODULE, 'playwright', path.resolve('../exam-coach/node_modules/playwright')].filter(Boolean)) {
  try { ({chromium} = require(name)); break; } catch {}
}
if (!chromium) throw Error('Install the locked dev dependency with npm ci (Playwright + Chromium required).');
const libs = path.join(os.homedir(), '.local/lib/chromium-deps/usr/lib/x86_64-linux-gnu');
if (existsSync(libs)) process.env.LD_LIBRARY_PATH = libs + (process.env.LD_LIBRARY_PATH ? `:${process.env.LD_LIBRARY_PATH}` : '');
const types = {'.html':'text/html', '.js':'text/javascript', '.css':'text/css', '.wasm':'application/wasm', '.png':'image/png', '.jpg':'image/jpeg'};
const server = createServer(async (req,res) => {
  try {
    const pathname = new URL(req.url,'http://localhost').pathname;
    assert(pathname.startsWith('/swim/'));
    const file = path.resolve('dist', decodeURIComponent(pathname.slice(6)) || 'index.html');
    assert(file.startsWith(path.resolve('dist') + path.sep));
    res.writeHead(200, {'Content-Type': types[path.extname(file)] || 'application/octet-stream'});
    res.end(await readFile(file));
  } catch { if (!res.headersSent) res.writeHead(404); res.end(); }
});
await new Promise(resolve => server.listen(0, '127.0.0.1', resolve));
const url = `http://127.0.0.1:${server.address().port}/swim/`;
const checks = [], errors = [], external = [];
const browser = await chromium.launch({headless:true,args:['--no-sandbox']});
const images = 'docs/images'; await mkdir(images,{recursive:true}); await mkdir('out',{recursive:true});
const inspect = page => page.evaluate(() => window.swimmerView);
const waitTime = (page,time) => page.waitForFunction(t => window.swimmerView.state[9] >= t,time);
const track = page => {
  page.on('pageerror', e => errors.push(String(e)));
  page.on('request', req => { if (new URL(req.url()).origin !== new URL(url).origin) external.push(req.url()); });
};
const shot = (page,name) => page.screenshot({path:`${images}/${name}.jpg`,type:'jpeg',quality:86,fullPage:true});
try {
  const page = await browser.newPage({viewport:{width:1280,height:800}}); track(page);
  const responseErrors = [];
  page.on('response', r => { if(r.status() >= 400) responseErrors.push(`${r.status()} ${r.url()}`); });
  await page.goto(url); await page.waitForFunction(() => window.swimmerView);
  let s = await inspect(page);
  assert.deepEqual(s.art, [true,true]); assert.equal(s.mode,'title'); assert.equal(s.state[9],0);
  await shot(page,'title'); checks.push('title uses both decoded original assets; subpath loads all files');
  await page.locator('#start').click();
  await page.keyboard.down('w'); await waitTime(page,.8);
  s = await inspect(page); assert(s.state[3] > .05 && s.state[0] > .3 && s.state[5] > 0);
  const beforeTurn = s.state[2];
  await page.keyboard.down('a'); await waitTime(page,s.state[9]+.6);
  await page.keyboard.up('a'); await page.keyboard.up('w');
  s = await inspect(page); assert(s.state[2] > beforeTurn + .5);
  await waitTime(page,s.state[9]+.4); s=await inspect(page);
  assert.equal(s.state[5],0); assert(s.state[11] > 0); checks.push('stroke, turn and release drive measured C physics');
  await page.locator('#pause').click();
  const paused = await inspect(page); await page.waitForTimeout(250);
  assert.deepEqual((await inspect(page)).state,paused.state); checks.push('pause freezes all physical values');
  await shot(page,'swim');
  await page.locator('#disc').check(); await page.locator('#vorticity').uncheck();
  await shot(page,'model');
  checks.push('field and model-disc controls render independent views');
  await page.locator('#pause').click(); await page.keyboard.down('w');
  await waitTime(page,paused.state[9]+.3);
  await page.evaluate(() => window.dispatchEvent(new Event('blur')));
  s = await inspect(page); assert(s.paused && s.held.length === 0);
  await page.keyboard.up('w'); checks.push('focus loss pauses and clears held input');
  await page.locator('#reset').click(); await page.locator('#pause').click();
  s = await inspect(page); assert(s.state[9] < .15 && s.state[7] === 0 && s.trail <= 1);
  checks.push('reset clears water, measurements and history');
  await page.goto(url); await page.locator('#demo').click(); await waitTime(page,1);
  assert.equal((await inspect(page)).mode,'demo');
  await page.keyboard.down('d'); await page.keyboard.up('d'); assert.equal((await inspect(page)).mode,'swim');
  checks.push('scripted demo yields to manual controls');
  assert.deepEqual(responseErrors,[]);

  // Upstream: play the whole journey through the real UI with an in-page swimmer that only presses keys.
  await page.goto(url); await page.waitForFunction(() => window.swimmerView);
  await page.locator('#upstream').click();
  await page.evaluate(() => {
    let resting = false;
    const press = (key, down) => window.dispatchEvent(new KeyboardEvent(down ? 'keydown' : 'keyup', {key}));
    window.__pilot = setInterval(() => {
      const v = window.swimmerView, j = v.journey;
      if (j.scene || v.paused) { for (const k of ['a', 'd', 'w']) press(k, false); return; }
      const [x, y, h] = v.state, want = Math.atan2(j.mark[1] - y, j.mark[0] - x);
      const err = ((want - h) % (2 * Math.PI) + 3 * Math.PI) % (2 * Math.PI) - Math.PI;
      press('a', err > .12); press('d', err < -.12);
      const level = j.stamina / j.staminaMax;
      if (level < .15) resting = true; else if (level > .65) resting = false;
      press('w', !resting);
    }, 20);
  });
  const seen = [], scene = async () => { await page.waitForFunction(() => window.swimmerView.journey.scene, null, {timeout: 120000}); return inspect(page); };
  const choose = async (option) => { await page.locator(`#scene-options button[data-option="${option}"]`).click(); await page.waitForFunction(() => !window.swimmerView.journey.scene || window.swimmerView.journey.scene === 'taste'); };
  s = await scene(); assert.equal(s.journey.scene, 'outlet'); assert.match(s.journey.sceneText, /Colder than your fins\./); seen.push('outlet');
  assert.equal(await page.locator('#scene-options button').count(), 1); await choose(0);
  await waitTime(page, 1); await shot(page, 'journey'); checks.push('Upstream: HUD shows reach, stamina, Silt and the next mark while swimming');
  s = await scene(); assert.equal(s.journey.scene, 'culvert'); seen.push('culvert');
  assert.deepEqual(await page.locator('#scene-options button').allTextContents(), ['Squeeze through the pipe', 'Go round by the flooded ditch (wait at the weir)']);
  await choose(0);
  s = await scene(); assert.equal(s.journey.scene, 'eddy'); assert.match(s.journey.sceneText, /shaking from the pipe/); seen.push('eddy'); await choose(0);
  s = await scene(); assert.equal(s.journey.scene, 'warm'); seen.push('warm'); await page.locator('#scene-options button').click();
  await page.waitForFunction(() => window.swimmerView.journey.scene === 'taste'); s = await inspect(page);
  assert.match(s.journey.sceneText, /never warm/); seen.push('taste'); await choose(0);
  s = await scene(); assert.equal(s.journey.scene, 'reeds'); seen.push('reeds');
  assert(await page.locator('#scene.heavy').isVisible() && await page.locator('#banner').isHidden());
  assert.equal(s.journey.bannerShown, false); await shot(page, 'reeds');
  const loud = s.journey.loud; await choose(1);
  s = await inspect(page); assert.equal(s.journey.silt, 0); assert.equal(s.journey.loud, loud);
  s = await scene(); assert.equal(s.journey.scene, 'surge'); seen.push('surge');
  assert(await page.locator('#scene.heavy').isVisible() && await page.locator('#banner').isHidden());
  assert.match(s.journey.sceneText, /left the reeds early/); await choose(0);
  s = await scene(); assert.equal(s.journey.scene, 'spring'); seen.push('spring');
  assert.equal(s.journey.ending, 1); assert.match(s.journey.sceneText, /never looked back/);
  assert(await page.locator('#scene.ending').isVisible() && await page.locator('#banner').isHidden()); await shot(page, 'spring');
  assert(s.journey.heavy.length === 2 && s.journey.heavy.every(h => h.banner === 0));
  await page.evaluate(() => clearInterval(window.__pilot));
  checks.push(`Upstream played through in ${s.journey.time.toFixed(1)} s on the clock with ${s.journey.sweeps} sweeps: ${seen.join(' > ')}; heavy scenes reeds/surge with no banner`);
  await page.locator('#scene-options button').click(); s = await inspect(page);
  assert.equal(s.journey.scene, 'outlet'); assert(s.journey.time === 0); checks.push('Upstream restarts from the ending');

  const mobile = await browser.newContext({viewport:{width:390,height:844},isMobile:true,hasTouch:true,deviceScaleFactor:1});
  const touch = await mobile.newPage(); track(touch); await touch.goto(url); await touch.locator('#start').tap();
  const stroke = await touch.locator('[data-control=stroke]').boundingBox();
  const left = await touch.locator('[data-control=left]').boundingBox();
  const cdp = await mobile.newCDPSession(touch);
  const point = (box,id) => ({x:box.x+box.width/2,y:box.y+box.height/2,id,radiusX:3,radiusY:3,force:1});
  await cdp.send('Input.dispatchTouchEvent',{type:'touchStart',touchPoints:[point(stroke,1)]});
  await waitTime(touch,.5);
  await cdp.send('Input.dispatchTouchEvent',{type:'touchStart',touchPoints:[point(stroke,1),point(left,2)]});
  s = await inspect(touch); await waitTime(touch,s.state[9]+.5);
  await cdp.send('Input.dispatchTouchEvent',{type:'touchEnd',touchPoints:[]});
  await touch.waitForFunction(() => window.swimmerView.held.length === 0);
  await touch.locator('#pause').tap(); s = await inspect(touch);
  assert(s.state[3] > .05 && s.state[2] > .5 && s.held.length === 0);
  assert(await touch.evaluate(() => document.documentElement.scrollWidth <= innerWidth));
  await shot(touch,'mobile'); checks.push('390 px: simultaneous real touch strokes and steering; no horizontal overflow');
  await touch.goto(url); await touch.locator('#upstream').tap();
  await touch.waitForFunction(() => window.swimmerView.journey.scene === 'outlet');
  const sizes390 = await touch.evaluate(() => [...document.querySelectorAll('#scene p, #scene-options button')].map(e => [parseFloat(getComputedStyle(e).fontSize), e.getBoundingClientRect().height, e.tagName]));
  assert(sizes390.every(([font]) => font >= 14) && sizes390.filter(([,, tag]) => tag === 'BUTTON').every(([, h]) => h >= 44));
  assert(await touch.evaluate(() => document.documentElement.scrollWidth <= innerWidth));
  await touch.locator('#scene-options button').tap(); await touch.waitForFunction(() => !window.swimmerView.journey.scene);
  checks.push('390 px Upstream: scene text >= 14 px, buttons >= 44 px, no horizontal overflow');
  await mobile.close();

  const fallback = await browser.newPage({viewport:{width:1280,height:800}}); track(fallback);
  await fallback.route('**/assets/**', route => route.fulfill({status:404,body:''}));
  await fallback.goto(url); await fallback.waitForFunction(() => window.swimmerView);
  assert.deepEqual((await inspect(fallback)).art,[false,false]);
  assert(await fallback.locator('#art-warning').isVisible());
  await fallback.locator('#start').click(); await fallback.keyboard.down('w'); await waitTime(fallback,.5); await fallback.keyboard.up('w');
  assert((await inspect(fallback)).state[3] > .05); checks.push('both assets missing: readable marker and C physics remain usable');
  await fallback.close();
  assert.deepEqual(errors,[]); assert.deepEqual(external,[]);
  checks.push('page errors 0; external requests 0');
  const sizes = [];
  for (const file of ['before','title','swim','model','mobile','journey','reeds','spring']) sizes.push({file:`${images}/${file}.jpg`,bytes:(await stat(`${images}/${file}.jpg`)).size});
  const summary = {checks,errors,externalRequests:external,images:sizes};
  await writeFile('out/smoke.json',JSON.stringify(summary,null,2)+'\n'); console.log(JSON.stringify(summary,null,2));
} finally { await browser.close(); await new Promise(resolve => server.close(resolve)); }
