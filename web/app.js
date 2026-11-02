'use strict';
// Words for "Upstream". The scenes open where the C journey (src/journey.c) says; the text lives here.
const STORY = {
  outlet: {lines: [
    ['', 'The board at the outlet lifts with a groan. The pond begins to leave.'],
    ['Silt', "The spring's up past the culvert. Cold water. Colder than your fins."],
    ['Silt', "Push when I push. Rest when I rest. Don't race me."],
    ['Copperfin', 'I can keep up. Watch.'],
    ['', 'How to swim: tap the water and Copperfin swims there; hold to keep swimming. Reach the dashed ring to clear each of the five reaches.']],
    options: ['Follow Silt']},
  culvert: {lines: [
    ['', 'Below the road the stream splits in two.'],
    ['Silt', "Pipe's short. It squeezes the water, and the water squeezes you."],
    ['Silt', "The ditch goes round by the field. It's slack and easy on the fins, but halfway round there's a weir. You hold still at the lip till it fills, and the pond keeps draining while you hold."],
    ['Copperfin', 'Sit still while the pond drains?']],
    options: ['Squeeze through the pipe (hard water, quick)', 'Go round by the ditch (easy water, hold at the weir)']},
  eddy: {lines: [
    ['', route => route === 1 ? 'The weir took its time. The pond took more of itself while they waited.' : "Copperfin's fins are still shaking from the pipe."],
    ['', 'Behind a stone the water folds back on itself. In the fold, nothing pulls.'],
    ['Silt', 'Here. Breathe.'],
    ['Copperfin', "It's like the water forgot about us."]],
    options: ['Rest, then go on']},
  warm: {lines: [
    ['Copperfin', "It's warm! The current's gone! We made it, Silt!"],
    ['', 'Copperfin turns a full circle just because it can.']],
    options: ['Call Silt over']},
  taste: {lines: [
    ['Silt', 'Warm.'],
    ['Silt', 'The spring is never warm. This is the glasshouse drain.'],
    ['Silt', "Thin water. Your breath won't come back in it."],
    ['Copperfin', 'Then why did you let me cheer?'],
    ['Silt', "You were happy. I'm old, not cruel."]],
    options: ['Swim on through the warm water']},
  reeds: {heavy: true, lines: [
    ['', 'Past the drain, Silt slows, then stops among the reed stems.'],
    ['Silt', "That's my fins done."],
    ['Silt', 'You know it now. Push, then let go. Let the water carry you between strokes.'],
    ['Copperfin', "I'm not leaving you in a ditch that's drying up."],
    ['Silt', "Reeds hold water longest. I'll be wet till the rain."]],
    options: ['Stay beside Silt until he breathes easier (the last board comes out while you wait)', 'Leave now, before the second board comes out']},
  surge: {heavy: true, lines: [
    ['', 'A second groan behind you. The last board is out.'],
    ['', 'Where the pond was, there is mud and sky.'],
    ['Copperfin', 'That was home. You could have waited one more day.'],
    ['', (route, stay) => stay === 1 ? 'You left the reeds early. The worst of the flood is still behind the stone.' : 'You stayed until his gills moved slow and even. The board came out while you waited, and the whole flood is in front of you.']],
    options: ['Keep swimming']},
  spring: {endings: [
    [['', 'The water turns cold all at once. Colder than your fins.'],
     ['', 'Copperfin pushes once, then lets go, and the spring carries it the last length.'],
     ['', "When it left the reeds, Silt's gills were moving slow and even."],
     ['', 'Now it waits at the mouth of the spring, pushing when the water pushes, resting when it rests, for whatever small thing comes up the pipe next.']],
    [['', 'The water turns cold all at once. Colder than your fins.'],
     ['', 'Copperfin pushes, then lets go, and the spring takes the last length.'],
     ['', "It never looked back to see whether Silt's gills were still moving. The light held. That was the trade."],
     ['', 'It keeps to the mouth of the spring alone, listening for a second tail in the dark water. None comes.']]],
    options: ['Swim upstream again']},
};
const SCENES = ['outlet', 'culvert', 'eddy', 'warm', 'taste', 'reeds', 'surge', 'spring'];
const REACHES = ['Outlet pool', 'The culvert', 'Eddy stone to the drain', 'Warm water', 'Last reach'];

(async () => {
  const $ = id => document.getElementById(id);
  const canvas = $('water'), ctx = canvas.getContext('2d');
  const chart = $('speed-plot').getContext('2d');
  const fish = new Image(), pond = new Image();
  fish.src = 'assets/hero-fish.png'; pond.src = 'assets/pond.jpg';
  const artwork = await Promise.all([fish.decode(), pond.decode()].map(p => p.then(() => true, () => false)));
  $('art-warning').hidden = artwork.every(Boolean);
  document.querySelector('.specimen img').hidden = !artwork[0];
  let mod;
  try {
    mod = await createSwimmer();
    if (mod._view_init() || mod._jw_init()) throw Error('Water allocation failed');
  } catch (e) {
    $('status').textContent = 'WATER COULD NOT LOAD';
    $('welcome').querySelector('p:not(.eyebrow)').textContent = 'Reload to try again. This view needs its local physics.wasm file.';
    console.error(e); return;
  }
  const floats = (ptr, n) => mod.HEAPF32.subarray(ptr / 4, ptr / 4 + n);
  const journeyState = () => floats(mod._jw_state(), 33);
  // Both modes are read through the free-swim layout: x y heading speed Re thrust drag energy div time radius |ω|.
  const state = () => {
    if (mode !== 'journey') return floats(mod._view_state(), 12);
    const j = journeyState();
    return [j[0], j[1], j[2], j[3], j[4], j[5], 0, j[28], j[29], j[13], j[6], j[7]];
  };
  const field = () => floats(mode === 'journey' ? mod._jw_field() : mod._view_field(), 48 * 48 * 3);

  // ---------------------------------------------------------------- canvas geometry
  // The canvas fills its box at the device's pixel density (up to 2x); the unit-square pool is drawn centred in it.
  let W = canvas.width, H = canvas.height, S = Math.min(W, H), OX = 0, OY = 0, dpr = 1;
  function fit() {
    const r = canvas.getBoundingClientRect();
    if (r.width < 1 || r.height < 1) return;
    dpr = Math.min(2, window.devicePixelRatio || 1);
    const w = Math.round(r.width * dpr), h = Math.round(r.height * dpr);
    if (canvas.width !== w || canvas.height !== h) { canvas.width = w; canvas.height = h; }
    W = w; H = h; S = Math.min(W, H); OX = (W - S) / 2; OY = (H - S) / 2;
  }
  const PX = x => OX + x * S, PY = y => OY + (1 - y) * S;
  fit();

  let mode = 'title', paused = false, last = performance.now(), accumulator = 0, previousSample = -1, shownScene = -2, resetArmed = 0;
  const keyboard = new Set(), pointers = new Map(), trail = [], speeds = [], heavyLog = [], drawnTexts = [];
  // Touch on the water. A tap sets a destination: Copperfin turns and swims there, push then let go (0.6 s strokes,
  // 0.26 s glides), until it arrives or 8 s pass. Holding or dragging steers towards the finger while it is down;
  // on release Copperfin goes back to the tapped destination, if there is one.
  const aim = {active: false, held: false, id: null, x: 0, y: 0, phase: 0, down: 0, sx: 0, sy: 0,
    dest: false, dx: 0, dy: 0, left: 0};
  const control = name => keyboard.has(name) || [...pointers.values()].includes(name);
  function clearInput() { keyboard.clear(); pointers.clear(); aim.active = aim.held = aim.dest = false; aim.id = null; paintControls(); }
  function paintControls() {
    document.querySelectorAll('[data-control]').forEach(b => b.classList.toggle('held', control(b.dataset.control)));
  }
  function setPaused(value) {
    paused = value; accumulator = 0; clearInput();
    $('pause').textContent = paused ? 'Resume' : 'Pause';
  }
  function begin(next) {
    mode = next;
    if (next === 'journey') mod._jw_init(); else mod._view_init();
    setPaused(false); shownScene = -2; heavyLog.length = 0; resetArmed = 0;
    trail.length = speeds.length = 0; previousSample = -1;
    $('welcome').hidden = true; $('pause').disabled = $('reset').disabled = false;
    $('journey-hud').hidden = next !== 'journey';
    $('story').hidden = next === 'journey';
    $('hint').hidden = false;
    $('reset').textContent = next === 'journey' ? 'Start the journey again' : 'Reset water';
    showScene();
  }
  $('start').disabled = $('demo').disabled = $('upstream').disabled = false;
  $('start').onclick = () => begin('swim');
  $('demo').onclick = () => begin('demo');
  $('upstream').onclick = () => begin('journey');
  $('story').onclick = () => begin('journey');
  $('pause').onclick = () => setPaused(!paused);
  // A journey under way asks for a second tap before it is thrown away.
  $('reset').onclick = () => {
    const j = journeyState();
    if (mode === 'journey' && (j[10] > 0 || j[13] > 3) && performance.now() > resetArmed) { resetArmed = performance.now() + 3000; return; }
    begin(mode === 'journey' ? 'journey' : 'swim');
  };

  // ---------------------------------------------------------------- scenes
  const sceneBox = $('scene');
  function showScene() {
    const j = journeyState(), index = mode === 'journey' ? j[11] : -1;
    if (index === shownScene) return;
    shownScene = index;
    if (index < 0) { sceneBox.hidden = true; return; }
    const id = SCENES[index], s = STORY[id], route = j[18], stay = j[19];
    const lines = s.endings ? s.endings[j[22] === 1 ? 1 : 0] : s.lines;
    clearInput();
    sceneBox.hidden = false;
    sceneBox.className = s.heavy ? 'heavy' : s.endings ? 'ending' : '';
    sceneBox.dataset.scene = id;
    if (s.heavy) heavyLog.push({id, banner: j[12], loud: j[24]});
    const text = $('scene-text');
    text.replaceChildren(...lines.map(([who, words]) => {
      const p = document.createElement('p');
      const said = typeof words === 'function' ? words(route, stay) : words;
      if (who) { const b = document.createElement('b'); b.textContent = who; p.append(b, ' ', said); }
      else { p.className = 'narration'; p.textContent = said; }
      return p;
    }));
    const choices = $('scene-options');
    choices.replaceChildren(...s.options.map((label, option) => {
      const b = document.createElement('button');
      b.textContent = label; b.dataset.option = option;
      b.onclick = () => answer(option);
      return b;
    }));
    sceneBox.scrollTop = 0;
    choices.firstChild.focus({preventScroll: true});
  }
  function answer(option) {
    const j = journeyState();
    if (SCENES[j[11]] === 'spring') { begin('journey'); return; }
    mod._jw_answer(option);
    showScene();
  }

  const keys = {a:'left', ArrowLeft:'left', d:'right', ArrowRight:'right', w:'stroke', ' ':'stroke', ArrowUp:'stroke'};
  window.addEventListener('keydown', e => {
    if (e.target instanceof Element && e.target.matches('button,input,summary,a') && (e.key === ' ' || e.key === 'Enter')) return;
    if (keys[e.key]) {
      e.preventDefault();
      if (mode === 'demo') mode = 'swim';
      if ((mode === 'swim' || mode === 'journey') && !paused && sceneBox.hidden) keyboard.add(keys[e.key]);
      paintControls();
    } else if (e.key.toLowerCase() === 'p' && !e.repeat && mode !== 'title') setPaused(!paused);
  });
  window.addEventListener('keyup', e => { if (keys[e.key]) { keyboard.delete(keys[e.key]); paintControls(); } });
  window.addEventListener('blur', () => { if (mode !== 'title') setPaused(true); });
  document.addEventListener('visibilitychange', () => { if (document.hidden && mode !== 'title') setPaused(true); });
  document.querySelectorAll('[data-control]').forEach(b => {
    b.addEventListener('pointerdown', e => {
      if (mode === 'title' || paused || !sceneBox.hidden) return;
      e.preventDefault(); if (mode === 'demo') mode = 'swim'; b.setPointerCapture(e.pointerId);
      pointers.set(e.pointerId, b.dataset.control); paintControls();
    });
    for (const event of ['pointerup','pointercancel','lostpointercapture']) b.addEventListener(event, e => { pointers.delete(e.pointerId); paintControls(); });
  });
  function setAim(e) {
    const r = canvas.getBoundingClientRect();
    const cx = (e.clientX - r.left) / r.width * W, cy = (e.clientY - r.top) / r.height * H;
    aim.x = Math.max(0, Math.min(1, (cx - OX) / S)); aim.y = Math.max(0, Math.min(1, 1 - (cy - OY) / S));
  }
  canvas.addEventListener('pointerdown', e => {
    if (mode === 'title' || !sceneBox.hidden) return;
    e.preventDefault();
    if (paused) { setPaused(false); return; }  // tapping the water resumes a paused swim
    if (mode === 'demo') mode = 'swim';
    try { canvas.setPointerCapture(e.pointerId); } catch {}
    aim.active = aim.held = true; aim.id = e.pointerId; aim.phase = 0; aim.down = performance.now(); aim.sx = e.clientX; aim.sy = e.clientY; setAim(e);
  });
  canvas.addEventListener('pointermove', e => { if (aim.held && e.pointerId === aim.id) setAim(e); });
  for (const event of ['pointerup', 'pointercancel']) canvas.addEventListener(event, e => {
    if (e.pointerId !== aim.id) return;
    aim.held = false; aim.id = null;
    const tap = event === 'pointerup' && performance.now() - aim.down < 300 && Math.hypot(e.clientX - aim.sx, e.clientY - aim.sy) < 12;
    if (tap) { aim.dest = true; aim.dx = aim.x; aim.dy = aim.y; aim.left = 400; }
    aim.active = aim.dest;
  });

  // Steering towards the touch point: the same turn and stroke inputs the keys give, nothing else.
  function aimControls() {
    if (!aim.held) { aim.x = aim.dx; aim.y = aim.dy; }
    const s = state(), dx = aim.x - s[0], dy = aim.y - s[1];
    const err = ((Math.atan2(dy, dx) - s[2]) % (2 * Math.PI) + 3 * Math.PI) % (2 * Math.PI) - Math.PI;
    const far = Math.hypot(dx, dy);
    let stroke = far > .02 && aim.phase++ % 43 < 30;
    if (!aim.held && (far < .04 || --aim.left <= 0)) aim.dest = false;  // arrived, or gave up after 8 s
    if (mode === 'journey') {
      const j = journeyState();
      if (j[31] > 0 && Math.hypot(j[16] - s[0], j[17] - s[1]) < j[32]) stroke = aim.held && far > j[32];  // glide inside a hold ring
    }
    aim.active = aim.held || aim.dest;
    return [Math.max(-1, Math.min(1, err * 2.5)), stroke];
  }

  const heat = document.createElement('canvas'); heat.width = heat.height = 48;
  const hctx = heat.getContext('2d'), pixels = hctx.createImageData(48, 48);
  function label(text, x, y, px, fill, stroke) {
    ctx.font = `bold ${px}px system-ui`; ctx.textAlign = 'center'; ctx.textBaseline = 'middle';
    if (stroke) { ctx.strokeStyle = stroke; ctx.lineWidth = px / 4; ctx.strokeText(text, x, y); }
    ctx.fillStyle = fill; ctx.fillText(text, x, y);
    drawnTexts.push({text, px});
  }
  function drawFish(x, y, heading, width, time, stroking) {
    ctx.save(); ctx.translate(x, y); ctx.rotate(-heading);
    if (artwork[0]) {
      // Bend the original image at the tail; the head stays at the model centre.
      for (let col = 0; col < 32; col++) {
        const bend = stroking ? Math.sin(time * 22 + col * .12) * Math.max(0, .6 - col / 32) * width * .07 : 0;
        ctx.drawImage(fish, col * 16, 0, 16, 512, (col / 32 - .62) * width, -width / 2 + bend, width / 32 + .2, width);
      }
    } else {
      ctx.fillStyle = '#e5eee8'; ctx.beginPath(); ctx.arc(0, 0, width / 5, 0, Math.PI * 2); ctx.fill();
      ctx.strokeStyle = '#edb16e'; ctx.lineWidth = 3; ctx.beginPath(); ctx.moveTo(0, 0); ctx.lineTo(width / 3, 0); ctx.stroke();
    }
    ctx.restore();
  }
  function drawJourney() {
    const j = journeyState();
    // The sluice at the right wall and the next mark the swimmer is heading for.
    ctx.fillStyle = '#d7eae430'; ctx.fillRect(PX(.955), PY(.56), S * .045, S * .12);
    const need = j[31], held = j[30];
    if (j[11] < 0 && j[10] < 5) {
      const x = PX(j[16]), y = PY(j[17]);
      ctx.strokeStyle = j[26] + 1 >= j[25] ? '#edb16e' : '#b4e2c3'; ctx.lineWidth = 2 * dpr; ctx.setLineDash([5 * dpr, 5 * dpr]);
      ctx.beginPath(); ctx.arc(x, y, (need > 0 ? j[32] : .07) * S, 0, Math.PI * 2); ctx.stroke(); ctx.setLineDash([]);
      if (need > 0) {
        // The hold ring fills while Copperfin stays inside it.
        ctx.strokeStyle = '#edb16e'; ctx.lineWidth = 5 * dpr; ctx.beginPath();
        ctx.arc(x, y, j[32] * S, -Math.PI / 2, -Math.PI / 2 + Math.min(1, held / need) * Math.PI * 2); ctx.stroke();
      }
    }
    const reach = Math.min(4, j[10]);
    $('reach').textContent = `${Math.min(5, reach + 1)} / 5 · ${REACHES[reach]}`;
    const level = Math.max(0, Math.min(1, j[8] / 1.5));
    $('stamina-fill').style.width = `${level * 100}%`;
    $('stamina-fill').classList.toggle('low', j[8] < .2 * j[9]);
    $('stamina-label').textContent = j[21] ? 'WINDED' : `${Math.round(j[8] / j[9] * 100)} %`;
    $('silt').textContent = j[20] ? 'Silt swims with you' : need > 0 ? 'Beside Silt in the reeds' : 'Silt stayed in the reeds';
    $('sweeps').textContent = `Swept back ${j[15]}×`;
    const hold = $('hold'), showHold = need > 0 && j[11] < 0;
    hold.hidden = !showHold;
    $('hint').textContent = showHold ? 'Stay inside the orange ring until it fills. Tap inside the ring to drift there.' : 'Next: swim to the dashed ring. Tap the water to swim there; hold to keep swimming.';
    if (showHold) hold.textContent = `${j[10] === 1 ? 'Weir: hold in the ring till the lip fills' : 'Hold in the ring beside Silt'} · ${Math.ceil(need - held)} s`;
    const banner = $('banner');
    banner.hidden = !j[12] || j[27] === 1;
    banner.textContent = j[12] === 2 ? 'Swept back' : 'Reach cleared';
    banner.className = j[12] === 2 ? 'swept' : '';
  }
  function drawCover(img) {
    const k = Math.max(W / img.naturalWidth, H / img.naturalHeight), w = img.naturalWidth * k, h = img.naturalHeight * k;
    ctx.drawImage(img, (W - w) / 2, (H - h) / 2, w, h);
  }
  function draw() {
    fit();
    drawnTexts.length = 0;
    const s = state(), f = field();
    ctx.fillStyle = '#10313d'; ctx.fillRect(0, 0, W, H);
    if (artwork[1]) drawCover(pond);
    ctx.fillStyle = mode === 'title' ? '#071c2940' : '#071c2988'; ctx.fillRect(0, 0, W, H);
    if (mode !== 'title' && (W !== H)) {
      // Outside the unit-square pool: the bank, darker, with the pool's edge drawn.
      ctx.fillStyle = '#04121899';
      ctx.fillRect(0, 0, W, OY); ctx.fillRect(0, OY + S, W, H - OY - S); ctx.fillRect(0, OY, OX, S); ctx.fillRect(OX + S, OY, W - OX - S, S);
      ctx.strokeStyle = '#d7eae466'; ctx.lineWidth = dpr; ctx.strokeRect(OX + .5, OY + .5, S - 1, S - 1);
    }
    const range = Math.max(1, s[11]) * .5;
    if (mode !== 'title' && $('vorticity').checked) {
      for (let j = 0; j < 48; j++) for (let i = 0; i < 48; i++) {
        const w = f[(j * 48 + i) * 3], p = ((47 - j) * 48 + i) * 4, strength = Math.abs(Math.tanh(w / range));
        const color = w >= 0 ? [255,174,91] : [82,157,255];
        pixels.data[p] = color[0]; pixels.data[p+1] = color[1]; pixels.data[p+2] = color[2]; pixels.data[p+3] = strength * 235;
      }
      hctx.putImageData(pixels, 0, 0); ctx.imageSmoothingEnabled = true;
      ctx.drawImage(heat, OX, OY, S, S);
      // Signs identify extrema independently of colour (clockwise is negative).
      for (const sign of [-1, 1]) {
        let best = -1, peak = .2;
        for (let i = 0; i < 48 * 48; i++) if (f[i * 3] * sign > peak) { best = i; peak = f[i * 3] * sign; }
        if (best >= 0) label(sign > 0 ? '+' : '−', PX(((best % 48) + .5) / 48), PY((Math.floor(best / 48) + .5) / 48), Math.round(18 * dpr), '#fff', '#092330');
      }
    }
    if (mode !== 'title' && $('arrows').checked) {
      ctx.strokeStyle = '#d7eae4b0'; ctx.lineWidth = dpr;
      for (let j = 2; j < 48; j += 3) for (let i = 2; i < 48; i += 3) {
        const k = (j * 48 + i) * 3, u = f[k+1], v = f[k+2], speed = Math.hypot(u,v);
        if (speed < .003) continue;
        const len = Math.min(18, speed * 100) * S / 720, dx = u / speed * len, dy = -v / speed * len;
        const x = PX((i + .5) / 48), y = PY((j + .5) / 48);
        ctx.beginPath(); ctx.moveTo(x - dx / 2, y - dy / 2); ctx.lineTo(x + dx / 2, y + dy / 2);
        ctx.lineTo(x + dx / 2 - dx * .3 - dy * .2, y + dy / 2 - dy * .3 + dx * .2); ctx.stroke();
      }
    }
    if (mode === 'journey') drawJourney();
    if (mode !== 'title' && trail.length) {
      ctx.strokeStyle = '#d0e5d366'; ctx.lineWidth = dpr; ctx.setLineDash([2 * dpr, 7 * dpr]); ctx.beginPath();
      trail.forEach(([x,y], i) => i ? ctx.lineTo(PX(x), PY(y)) : ctx.moveTo(PX(x), PY(y))); ctx.stroke(); ctx.setLineDash([]);
    }
    if (mode !== 'title' && aim.active) {
      ctx.strokeStyle = '#ffffffaa'; ctx.lineWidth = 1.5 * dpr; ctx.beginPath(); ctx.arc(PX(aim.x), PY(aim.y), 10 * dpr, 0, Math.PI * 2); ctx.stroke();
    }
    if (mode !== 'title' && $('disc').checked) {
      ctx.strokeStyle = '#fff'; ctx.lineWidth = 1.5 * dpr; ctx.beginPath(); ctx.arc(PX(s[0]), PY(s[1]), s[10] * S, 0, Math.PI * 2); ctx.stroke();
    }
    if (mode === 'title') drawFish(W - S * .25, H - S * .13, s[2], S * .305, s[9], s[5] > 0);
    else drawFish(PX(s[0]), PY(s[1]), s[2], s[10] * S * 5, s[9], s[5] > 0);
    $('clock').textContent = `${s[9].toFixed(2)} s`;
    $('status').textContent = mode === 'title' ? 'READY TO EXPLORE' : paused ? 'PAUSED · TAP THE WATER' : mode === 'demo' ? 'SCRIPTED SWIM' : mode === 'journey' ? 'UPSTREAM' : 'IN THE WATER';
    $('speed').textContent = s[3].toFixed(3); $('re').textContent = s[4].toFixed(1); $('vort').textContent = s[11].toFixed(2);
    $('scale').textContent = `ω ±${range.toFixed(2)} s⁻¹ · auto`;
    $('stroke-state').textContent = mode === 'title' ? 'READY' : paused ? 'PAUSED' : s[5] > 0 ? 'STROKING' : 'COASTING';
    $('energy').textContent = `Water energy: ${s[7].toExponential(2)} · Max divergence: ${s[8].toExponential(2)} (domain units)`;
    const armed = mode === 'journey' && performance.now() < resetArmed, reset = $('reset');
    reset.classList.toggle('confirm', armed);
    if (mode !== 'title') reset.textContent = armed ? 'Tap again to restart' : mode === 'journey' ? 'Start the journey again' : 'Reset water';
    chart.clearRect(0,0,280,68); chart.strokeStyle = '#31505a'; chart.beginPath(); chart.moveTo(0,64); chart.lineTo(280,64); chart.stroke();
    if (speeds.length) {
      const peak = Math.max(.4, ...speeds.map(p => p[1]));
      chart.strokeStyle = '#b4e2c3'; chart.lineWidth = 1.8; chart.beginPath();
      speeds.forEach(([t,v],i) => { const x = (t - Math.max(0,s[9]-20)) / 20 * 280, y = 62 - v / peak * 54; i ? chart.lineTo(x,y) : chart.moveTo(x,y); }); chart.stroke();
    }
  }
  // Read-only diagnostics used by smoke checks; no path to change simulation state.
  Object.defineProperty(window, 'swimmerView', {get: () => {
    const j = journeyState();
    return {mode, paused, art: [...artwork], state: [...state()], held: [...keyboard,...pointers.values()], trail: trail.length, aim: aim.active,
      journey: {scene: j[11] >= 0 ? SCENES[j[11]] : null, reach: j[10], banner: j[12], bannerShown: !$('banner').hidden && mode === 'journey',
        stamina: j[8], staminaMax: j[9], mark: [j[16], j[17]], time: j[13], sweeps: j[15], silt: j[20], route: j[18], stay: j[19], ending: j[22], loud: j[24],
        hold: [j[30], j[31]], heavy: heavyLog.map(h => ({...h})), sceneText: sceneBox.hidden ? '' : $('scene-text').textContent}};
  }});
  // Shared playability-gate hook (read-only): where things are on the canvas, in canvas pixels, and what the first goal is.
  const centreOf = el => {
    const r = el.getBoundingClientRect(), c = canvas.getBoundingClientRect();
    return {x: (r.left + r.width / 2 - c.left) / c.width * W, y: (r.top + r.height / 2 - c.top) / c.height * H};
  };
  window.__tmGate = {
    version: 1,
    get logical() { return {w: W, h: H}; },
    scene: () => mode === 'title' ? 'title' : mode !== 'journey' ? mode : journeyState()[11] >= 0 ? `scene:${SCENES[journeyState()[11]]}` : paused ? 'paused' : 'play',
    // The first goal: the first reach of Upstream cleared (the outlet pool swum to its mark).
    goal: () => mode === 'journey' && journeyState()[10] >= 1,
    texts: () => drawnTexts.map(t => ({...t})),
    targets: () => [],
    player: () => { const s = state(); return {x: PX(s[0]), y: PY(s[1])}; },
    // Where a player who reads the screen would tap next: the story button, the scene's first answer, or the dashed ring.
    hint: () => {
      if (mode === 'title') return centreOf($('upstream'));
      if (mode !== 'journey') return centreOf($('story'));
      const j = journeyState();
      if (j[11] >= 0) return centreOf($('scene-options').firstChild);
      if (paused) return {x: W / 2, y: H / 2};
      return {x: PX(j[16]), y: PY(j[17])};
    },
  };
  function frame(now) {
    const delta = Math.min(.1, Math.max(0,(now-last)/1000)); last = now;
    if (mode !== 'title' && !paused && sceneBox.hidden) {
      accumulator += delta;
      while (accumulator >= .02) {
        const t = state()[9];
        let turn = Number(control('left')) - Number(control('right')), stroke = control('stroke');
        if (mode === 'demo') { turn = t < 7 ? .5 : -.5; stroke = t % 2.4 < .5; }
        else if (aim.active && !control('left') && !control('right') && !stroke) [turn, stroke] = aimControls();
        if (mode === 'journey') mod._jw_step(turn, Number(stroke)); else mod._view_step(turn, Number(stroke));
        accumulator -= .02;
        const s = state(), sample = Math.floor((s[9] + .001) * 5);
        if (sample !== previousSample) {
          previousSample = sample; trail.push([s[0],s[1]]); speeds.push([s[9],s[3]]);
          if (trail.length > 160) trail.shift(); while (speeds.length && s[9]-speeds[0][0] > 20) speeds.shift();
        }
        if (mode === 'journey' && journeyState()[11] >= 0) { accumulator = 0; trail.length = 0; showScene(); break; }
      }
    }
    draw(); requestAnimationFrame(frame);
  }
  requestAnimationFrame(frame);
})();
