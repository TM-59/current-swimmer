'use strict';
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
    if (mod._view_init()) throw Error('Water allocation failed');
  } catch (e) {
    $('status').textContent = 'WATER COULD NOT LOAD';
    $('welcome').querySelector('p:not(.eyebrow)').textContent = 'Reload to try again. This view needs its local physics.wasm file.';
    console.error(e); return;
  }
  const state = () => mod.HEAPF32.subarray(mod._view_state() / 4, mod._view_state() / 4 + 12);
  const field = () => mod.HEAPF32.subarray(mod._view_field() / 4, mod._view_field() / 4 + 48 * 48 * 3);
  let mode = 'title', paused = false, last = performance.now(), accumulator = 0, previousSample = -1;
  const keyboard = new Set(), pointers = new Map(), trail = [], speeds = [];
  const control = name => keyboard.has(name) || [...pointers.values()].includes(name);
  function clearInput() { keyboard.clear(); pointers.clear(); paintControls(); }
  function paintControls() {
    document.querySelectorAll('[data-control]').forEach(b => b.classList.toggle('held', control(b.dataset.control)));
  }
  function setPaused(value) {
    paused = value; accumulator = 0; clearInput();
    $('pause').textContent = paused ? 'Resume' : 'Pause';
  }
  function begin(next) {
    mod._view_init(); mode = next; setPaused(false);
    trail.length = speeds.length = 0; previousSample = -1;
    $('welcome').hidden = true; $('pause').disabled = $('reset').disabled = false;
  }
  $('start').disabled = $('demo').disabled = false;
  $('start').onclick = () => begin('swim');
  $('demo').onclick = () => begin('demo');
  $('pause').onclick = () => setPaused(!paused);
  $('reset').onclick = () => begin('swim');
  const keys = {a:'left', ArrowLeft:'left', d:'right', ArrowRight:'right', w:'stroke', ' ':'stroke', ArrowUp:'stroke'};
  window.addEventListener('keydown', e => {
    if (e.target.matches('button,input,summary,a') && (e.key === ' ' || e.key === 'Enter')) return;
    if (keys[e.key]) {
      e.preventDefault();
      if (mode === 'demo') mode = 'swim';
      if (mode === 'swim' && !paused) keyboard.add(keys[e.key]);
      paintControls();
    } else if (e.key.toLowerCase() === 'p' && !e.repeat && mode !== 'title') setPaused(!paused);
  });
  window.addEventListener('keyup', e => { if (keys[e.key]) { keyboard.delete(keys[e.key]); paintControls(); } });
  window.addEventListener('blur', () => { if (mode !== 'title') setPaused(true); });
  document.addEventListener('visibilitychange', () => { if (document.hidden && mode !== 'title') setPaused(true); });
  document.querySelectorAll('[data-control]').forEach(b => {
    b.addEventListener('pointerdown', e => {
      if (mode === 'title' || paused) return;
      e.preventDefault(); mode = 'swim'; b.setPointerCapture(e.pointerId);
      pointers.set(e.pointerId, b.dataset.control); paintControls();
    });
    for (const event of ['pointerup','pointercancel','lostpointercapture']) b.addEventListener(event, e => { pointers.delete(e.pointerId); paintControls(); });
  });
  const heat = document.createElement('canvas'); heat.width = heat.height = 48;
  const hctx = heat.getContext('2d'), pixels = hctx.createImageData(48, 48);
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
  function draw() {
    const s = state(), f = field(), size = canvas.width;
    ctx.fillStyle = '#10313d'; ctx.fillRect(0, 0, size, size);
    if (artwork[1]) ctx.drawImage(pond, 0, 0, size, size);
    ctx.fillStyle = mode === 'title' ? '#071c2940' : '#071c2988'; ctx.fillRect(0, 0, size, size);
    const range = Math.max(1, s[11]) * .5;
    if (mode !== 'title' && $('vorticity').checked) {
      for (let j = 0; j < 48; j++) for (let i = 0; i < 48; i++) {
        const w = f[(j * 48 + i) * 3], p = ((47 - j) * 48 + i) * 4, strength = Math.abs(Math.tanh(w / range));
        const color = w >= 0 ? [255,174,91] : [82,157,255];
        pixels.data[p] = color[0]; pixels.data[p+1] = color[1]; pixels.data[p+2] = color[2]; pixels.data[p+3] = strength * 235;
      }
      hctx.putImageData(pixels, 0, 0); ctx.imageSmoothingEnabled = true;
      ctx.drawImage(heat, 0, 0, size, size);
      // Signs identify extrema independently of colour (clockwise is negative).
      for (const sign of [-1, 1]) {
        let best = -1, peak = .2;
        for (let i = 0; i < 48 * 48; i++) if (f[i * 3] * sign > peak) { best = i; peak = f[i * 3] * sign; }
        if (best >= 0) {
          ctx.font = 'bold 18px system-ui'; ctx.textAlign = 'center'; ctx.textBaseline = 'middle';
          ctx.strokeStyle = '#092330'; ctx.lineWidth = 4; ctx.fillStyle = '#fff';
          const x = ((best % 48) + .5) / 48 * size, y = (1 - (Math.floor(best / 48) + .5) / 48) * size;
          ctx.strokeText(sign > 0 ? '+' : '−', x, y); ctx.fillText(sign > 0 ? '+' : '−', x, y);
        }
      }
    }
    if (mode !== 'title' && $('arrows').checked) {
      ctx.strokeStyle = '#d7eae4b0'; ctx.lineWidth = 1;
      for (let j = 2; j < 48; j += 3) for (let i = 2; i < 48; i += 3) {
        const k = (j * 48 + i) * 3, u = f[k+1], v = f[k+2], speed = Math.hypot(u,v);
        if (speed < .003) continue;
        const len = Math.min(18, speed * 100), dx = u / speed * len, dy = -v / speed * len;
        const x = (i + .5) / 48 * size, y = (1 - (j + .5) / 48) * size;
        ctx.beginPath(); ctx.moveTo(x - dx / 2, y - dy / 2); ctx.lineTo(x + dx / 2, y + dy / 2);
        ctx.lineTo(x + dx / 2 - dx * .3 - dy * .2, y + dy / 2 - dy * .3 + dx * .2); ctx.stroke();
      }
    }
    if (mode !== 'title' && trail.length) {
      ctx.strokeStyle = '#d0e5d366'; ctx.lineWidth = 1; ctx.setLineDash([2,7]); ctx.beginPath();
      trail.forEach(([x,y], i) => i ? ctx.lineTo(x * size, (1-y) * size) : ctx.moveTo(x * size, (1-y) * size)); ctx.stroke(); ctx.setLineDash([]);
    }
    if (mode !== 'title' && $('disc').checked) {
      ctx.strokeStyle = '#fff'; ctx.lineWidth = 1.5; ctx.beginPath(); ctx.arc(s[0] * size, (1-s[1]) * size, s[10] * size, 0, Math.PI * 2); ctx.stroke();
    }
    drawFish(mode === 'title' ? size * .75 : s[0]*size, mode === 'title' ? size * .87 : (1-s[1])*size, s[2], mode === 'title' ? 220 : s[10]*size*5, s[9], s[5] > 0);
    $('clock').textContent = `${s[9].toFixed(2)} s`;
    $('status').textContent = mode === 'title' ? 'READY TO EXPLORE' : paused ? 'PAUSED' : mode === 'demo' ? 'SCRIPTED SWIM' : 'IN THE WATER';
    $('speed').textContent = s[3].toFixed(3); $('re').textContent = s[4].toFixed(1); $('vort').textContent = s[11].toFixed(2);
    $('scale').textContent = `ω ±${range.toFixed(2)} s⁻¹ · auto`;
    $('stroke-state').textContent = mode === 'title' ? 'READY' : paused ? 'PAUSED' : s[5] > 0 ? 'STROKING' : 'COASTING';
    $('energy').textContent = `Water energy: ${s[7].toExponential(2)} · Max divergence: ${s[8].toExponential(2)} (domain units)`;
    chart.clearRect(0,0,280,68); chart.strokeStyle = '#31505a'; chart.beginPath(); chart.moveTo(0,64); chart.lineTo(280,64); chart.stroke();
    if (speeds.length) {
      const peak = Math.max(.4, ...speeds.map(p => p[1]));
      chart.strokeStyle = '#b4e2c3'; chart.lineWidth = 1.8; chart.beginPath();
      speeds.forEach(([t,v],i) => { const x = (t - Math.max(0,s[9]-20)) / 20 * 280, y = 62 - v / peak * 54; i ? chart.lineTo(x,y) : chart.moveTo(x,y); }); chart.stroke();
    }
  }
  // Read-only diagnostics used by smoke checks; no path to change simulation state.
  Object.defineProperty(window, 'swimmerView', {get: () => ({mode, paused, art: [...artwork], state: [...state()], held: [...keyboard,...pointers.values()], trail: trail.length})});
  function frame(now) {
    const delta = Math.min(.1, Math.max(0,(now-last)/1000)); last = now;
    if (mode !== 'title' && !paused) {
      accumulator += delta;
      while (accumulator >= .02) {
        const t = state()[9];
        const turn = mode === 'demo' ? (t < 7 ? .5 : -.5) : Number(control('left')) - Number(control('right'));
        const stroke = mode === 'demo' ? t % 2.4 < .5 : control('stroke');
        mod._view_step(turn, Number(stroke)); accumulator -= .02;
        const s = state(), sample = Math.floor((s[9] + .001) * 5);
        if (sample !== previousSample) {
          previousSample = sample; trail.push([s[0],s[1]]); speeds.push([s[9],s[3]]);
          if (trail.length > 160) trail.shift(); while (speeds.length && s[9]-speeds[0][0] > 20) speeds.shift();
        }
      }
    }
    draw(); requestAnimationFrame(frame);
  }
  requestAnimationFrame(frame);
})();
