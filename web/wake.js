'use strict';
// Behind a post (M2). Every number on this page is read from the C wake (src/wake.c via src/wake_web.c).
(async () => {
  const $ = id => document.getElementById(id);
  const CELLS = 6, BUDGET_MS = 10, MAX_STEPS = 12;
  let mod;
  try {
    mod = await createSwimmer();
  } catch (e) {
    $('wk-regime').textContent = 'COULD NOT LOAD';
    console.error(e); return;
  }
  const floats = (ptr, n) => mod.HEAPF32.subarray(ptr / 4, ptr / 4 + n);
  const state = () => {
    const s = floats(mod._wv_state(), 15);
    return {t: s[0], re: s[1], cd: s[2], cl: s[3], st: s[4], cdMean: s[5], clAmp: s[6], periods: s[7],
      nx: s[8], ny: s[9], n: s[10], lr: s[11], div: s[12], from: s[13], sweeps: s[14]};
  };
  const roshko = re => 0.212 * (1 - 21.2 / re);

  let paused = false;
  function start(re) {
    $('wk-re').value = re; $('wk-re-out').textContent = re;
    if (mod._wv_init(re, CELLS)) { $('wk-regime').textContent = 'OUT OF MEMORY'; return; }
    $('wk-fit').textContent = roshko(re).toFixed(3);
  }
  start(150);
  $('wk-re').addEventListener('input', () => { $('wk-re-out').textContent = $('wk-re').value; });
  $('wk-re').addEventListener('change', () => start(+$('wk-re').value));
  $('wk-pause').onclick = () => { paused = !paused; $('wk-pause').textContent = paused ? 'Resume' : 'Pause'; };
  $('wk-restart').onclick = () => start(+$('wk-re').value);
  for (const re of [30, 100, 200]) $(`wk-preset-${re}`).onclick = () => start(re);

  // ------------------------------------------------------------- drawing
  const water = $('wk-water'), wctx = water.getContext('2d');
  const chart = $('wk-forces'), cctx = chart.getContext('2d');
  const off = document.createElement('canvas'), octx = off.getContext('2d');
  function fit(canvas) {
    const r = canvas.getBoundingClientRect(), dpr = Math.min(2, window.devicePixelRatio || 1);
    const w = Math.max(1, Math.round(r.width * dpr)), h = Math.max(1, Math.round(r.height * dpr));
    if (canvas.width !== w || canvas.height !== h) { canvas.width = w; canvas.height = h; }
    return dpr;
  }
  function colour(w) {
    const t = Math.tanh(w / 1.5), a = Math.abs(t);
    // orange for counter-clockwise, blue for clockwise, on the dark water of the pool page
    const to = t >= 0 ? [255, 170, 90] : [110, 180, 255], from = [18, 51, 62];
    return from.map((b, k) => b + (to[k] - b) * a);
  }
  function drawWater(s) {
    fit(water);
    const nx = s.nx, ny = s.ny, f = floats(mod._wv_field(), nx * ny);
    if (off.width !== nx || off.height !== ny) { off.width = nx; off.height = ny; }
    const img = octx.createImageData(nx, ny);
    for (let j = 0; j < ny; j++) for (let i = 0; i < nx; i++) {
      const c = colour(f[i + nx * j]), k = 4 * (i + nx * (ny - 1 - j));
      img.data[k] = c[0]; img.data[k + 1] = c[1]; img.data[k + 2] = c[2]; img.data[k + 3] = 255;
    }
    octx.putImageData(img, 0, 0);
    wctx.imageSmoothingEnabled = true;
    wctx.drawImage(off, 0, 0, water.width, water.height);
    const sx = water.width / 16, sy = water.height / 8;
    wctx.fillStyle = '#0b1114';
    wctx.beginPath(); wctx.ellipse(4 * sx, 4 * sy, 0.5 * sx, 0.5 * sy, 0, 0, 2 * Math.PI); wctx.fill();
    wctx.strokeStyle = '#e5eee8'; wctx.lineWidth = Math.max(1, sx / 30); wctx.stroke();
  }
  function drawForces() {
    const dpr = fit(chart), W = chart.width, H = chart.height, n = mod._wv_trace_count();
    const tr = floats(mod._wv_trace(), 800), step = mod._wv_trace_dt();
    cctx.fillStyle = '#0a1f25'; cctx.fillRect(0, 0, W, H);
    const lo = -1.2, hi = 2.6, left = 48 * dpr, right = 10 * dpr, top = 8 * dpr, bottom = 26 * dpr;
    const X = k => left + (W - left - right) * k / 399, Y = v => top + (H - top - bottom) * (hi - v) / (hi - lo);
    cctx.font = `${14 * dpr}px system-ui, sans-serif`; cctx.fillStyle = '#a4bbb9'; cctx.textAlign = 'right'; cctx.textBaseline = 'middle';
    for (const v of [-1, 0, 1, 2]) {
      cctx.strokeStyle = v === 0 ? '#577179' : '#29464f'; cctx.lineWidth = dpr;
      cctx.beginPath(); cctx.moveTo(left, Y(v)); cctx.lineTo(W - right, Y(v)); cctx.stroke();
      cctx.fillText(String(v), left - 8 * dpr, Y(v));
    }
    cctx.textAlign = 'left'; cctx.textBaseline = 'alphabetic';
    cctx.fillText(`${(n * step).toFixed(0)} D/U shown`, left, H - 6 * dpr);
    for (const [offset, col] of [[400, '#ff8a65'], [0, '#9bc9ff']]) {
      cctx.strokeStyle = col; cctx.lineWidth = 2 * dpr; cctx.beginPath();
      for (let k = 0; k < n; k++) { const x = X(k + 400 - n), y = Y(tr[offset + k]); k ? cctx.lineTo(x, y) : cctx.moveTo(x, y); }
      cctx.stroke();
    }
  }
  function readouts(s) {
    $('wk-clock').textContent = `t = ${s.t.toFixed(1)} D/U · Re ${s.re.toFixed(0)}`;
    const measuring = s.t >= 20, shedding = s.periods > 0;
    $('wk-regime').textContent = !measuring ? 'STARTING (UNTIL 20 D/U)' : shedding ? 'SHEDDING' : s.clAmp < 0.02 ? 'STEADY: TWIN EDDIES' : 'SETTLING';
    $('wk-st').textContent = measuring && shedding ? s.st.toFixed(3) : '—';
    $('wk-cd').textContent = measuring ? s.cdMean.toFixed(2) : '—';
    $('wk-cl').textContent = measuring ? s.clAmp.toFixed(3) : '—';
    $('wk-lr').textContent = s.lr > 0 ? s.lr.toFixed(2) : '—';
    const pos = v => `${Math.max(0, Math.min(100, (v - 0.10) / 0.16 * 100))}%`;
    $('wk-mark-fit').style.left = pos(roshko(s.re));
    const mark = $('wk-mark-st');
    mark.hidden = !(measuring && shedding);
    if (!mark.hidden) mark.style.left = pos(s.st);
    const band = shedding && s.st >= 0.16 && s.st <= 0.22;
    $('wk-compare').textContent = !measuring ? 'Measuring starts after 20 D/U, over the newest half of the record.'
      : shedding ? `St ${s.st.toFixed(3)} over ${s.periods.toFixed(0)} lift periods: ${band ? 'inside' : 'outside'} the band 0.16–0.22; ${((s.st / roshko(s.re) - 1) * 100).toFixed(0)} % from Roshko's fit. Mean drag ${s.cdMean.toFixed(2)} against about 1.33 for a post in open water; this model reads 24–28 % high on every grid tried (see Limits).`
      : s.re < 47 ? `No swing: below Re about 47 the two eddies stay attached. Closed wake ${s.lr.toFixed(2)} D long.` : 'The swing is still growing; shedding starts slowly near Re 50.';
  }

  // ------------------------------------------------------------- loop and test hook
  function advance(steps) { mod._wv_step(steps); }
  window.wakeView = {state, advance, start, roshko};
  function frame() {
    if (!paused && !document.hidden) {
      const t0 = performance.now();
      let steps = 0;
      while (steps < MAX_STEPS && performance.now() - t0 < BUDGET_MS) { advance(1); steps++; }
    }
    const s = state();
    drawWater(s); drawForces(); readouts(s);
    requestAnimationFrame(frame);
  }
  requestAnimationFrame(frame);
})();
