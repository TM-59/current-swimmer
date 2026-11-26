// Background music for the browser view (M63 #33, parent TM-59/project-context#598).
// Ballad base, a separate pulse track for the Upstream reaches, quiet story music for the scenes and
// an ending track. Tracks are original ACE-Step 1.5 (MIT) renders gated by scripts/bgm_bake.py
// (docs/bgm.md). Nothing is fetched before the first press, so the first load stays as it was;
// a track that fails to load or decode leaves the swim silent and running.
// Mute cuts the output (master) too, so a track fading out in a crossfade is silenced at once.

/** @typedef {'pond' | 'upstream' | 'story' | 'spring'} Track */
/** @typedef {{ track: Track, gain: number }} Cue */

/** Every track, the base first: the order the player loads them in. */
export const TRACKS = /** @type {const} */ (['pond', 'upstream', 'story', 'spring']);
/** @type {ReadonlySet<string>} */
const NAMES = new Set(TRACKS);
/** Level of the scene music under narration. */
export const STORY_GAIN = 0.7;
/** Level while paused or in a heavy scene (Silt in the reeds, the last board). */
export const DUCK = 0.4;
const BASE = 0.45;
const FADE = 2.5;

/**
 * Which track the screen asks for. Pure: the same screen always gives the same cue.
 * @param {{ mode: string, scene: string | null, heavy: boolean, paused: boolean }} view
 * @returns {Cue}
 */
export function cue(view) {
  const duck = view.paused ? DUCK : 1;
  if (view.mode !== 'journey') return { track: 'pond', gain: duck };
  if (view.scene === 'spring') return { track: 'spring', gain: 1 };
  if (view.scene) return { track: 'story', gain: view.heavy ? DUCK : STORY_GAIN };
  return { track: 'upstream', gain: duck };
}

export class BgmPlayer {
  /** @param {string} base */
  constructor(base = 'bgm/') {
    this.base = base;
    /** @type {AudioContext | null} */
    this.ctx = null;
    /** @type {GainNode | null} */
    this.master = null;
    /** @type {AnalyserNode | null} */
    this.meter = null;
    /** @type {Set<string> | null} tracks that passed the gate (null until list.json is read) */
    this.list = null;
    /** @type {{ name: string, src: AudioBufferSourceNode, g: GainNode } | null} */
    this.current = null;
    /** @type {Map<string, AudioBuffer>} */
    this.buffers = new Map();
    /** @type {Map<string, Promise<void>>} */
    this.pending = new Map();
    /** @type {Track | null} */
    this.want = null;
    this.gain = 1;
    this.muted = false;
    this.unlocked = false;
    /** @type {string[]} */
    this.fetched = [];
    /** @type {string[]} */
    this.loaded = [];
    /** @type {string[]} */
    this.failed = [];
  }

  /** Call from a press: creates the audio context and starts loading. Later calls resume it. */
  unlock() {
    if (this.unlocked) {
      if (this.ctx?.state === 'suspended') void this.ctx.resume();
      return;
    }
    this.unlocked = true;
    try {
      this.ctx = new AudioContext();
    } catch {
      return;
    }
    const ctx = this.ctx;
    this.master = ctx.createGain();
    this.master.gain.value = this.muted ? 0 : 1;
    this.meter = ctx.createAnalyser();
    this.meter.fftSize = 2048;
    this.master.connect(this.meter);
    this.meter.connect(ctx.destination);
    fetch(`${this.base}list.json`)
      .then((r) => r.json())
      .then((j) => {
        this.list = new Set(
          Array.isArray(j) ? j.filter((n) => typeof n === 'string' && NAMES.has(n)) : [],
        );
        if (this.want) void this.load(this.want);
        /** @param {number} ms */
        const wait = (ms) => new Promise((r) => setTimeout(r, ms));
        void (async () => {
          await wait(4000);
          for (const name of TRACKS) {
            await this.load(name);
            await wait(1500);
          }
        })();
      })
      .catch(() => {
        this.list = new Set();
      });
  }

  /** @param {string} name */
  load(name) {
    const busy = this.pending.get(name);
    if (busy) return busy;
    const ctx = this.ctx;
    if (this.buffers.has(name) || !this.list?.has(name) || !ctx) return Promise.resolve();
    this.fetched.push(name);
    const job = fetch(`${this.base}${name}.ogg`)
      .then((r) => {
        if (!r.ok) throw Error(String(r.status));
        return r.arrayBuffer();
      })
      .then((a) => ctx.decodeAudioData(a))
      .then((b) => {
        this.buffers.set(name, b);
        this.loaded.push(name);
        this.apply();
      })
      .catch(() => {
        this.failed.push(name);
      })
      .finally(() => this.pending.delete(name));
    this.pending.set(name, job);
    return job;
  }

  target() {
    return BASE * this.gain;
  }

  apply() {
    const ctx = this.ctx,
      master = this.master;
    if (!ctx || !master || !this.want) return;
    if (this.current?.name === this.want) {
      this.current.g.gain.setTargetAtTime(this.target(), ctx.currentTime, 0.08);
      return;
    }
    const buffer = this.buffers.get(this.want);
    if (!buffer) {
      void this.load(this.want); // the previous track plays on until this one arrives
      return;
    }
    const now = ctx.currentTime;
    this.fadeOut(now);
    const src = ctx.createBufferSource();
    src.buffer = buffer;
    src.loop = true;
    const g = ctx.createGain();
    g.gain.setValueAtTime(0, now);
    g.gain.linearRampToValueAtTime(this.target(), now + FADE);
    src.connect(g);
    g.connect(master);
    src.start(now);
    this.current = { name: this.want, src, g };
  }

  /** @param {number} now */
  fadeOut(now) {
    const old = this.current;
    if (!old) return;
    old.g.gain.cancelScheduledValues(now);
    old.g.gain.setValueAtTime(old.g.gain.value, now);
    old.g.gain.linearRampToValueAtTime(0, now + FADE);
    old.src.stop(now + FADE + 0.05);
    this.current = null;
  }

  /**
   * Call every frame with the screen's cue.
   * @param {Cue} next
   * @param {boolean} muted
   */
  set(next, muted) {
    if (muted !== this.muted && this.ctx && this.master)
      this.master.gain.setTargetAtTime(muted ? 0 : 1, this.ctx.currentTime, 0.05);
    this.muted = muted;
    if (next.track === this.want && next.gain === this.gain) return;
    this.want = next.track;
    this.gain = next.gain;
    this.apply();
  }

  /** Output RMS in dBFS, for the local bgm smoke. */
  level() {
    if (!this.meter) return -120;
    const a = new Float32Array(this.meter.fftSize);
    this.meter.getFloatTimeDomainData(a);
    let e = 0;
    for (const v of a) e += v * v;
    return 10 * Math.log10(e / a.length + 1e-12);
  }

  state() {
    return {
      want: this.want,
      playing: this.current?.name ?? '',
      gain: this.target(),
      muted: this.muted,
      unlocked: this.unlocked,
      running: this.ctx?.state === 'running',
      fetched: [...this.fetched],
      loaded: [...this.loaded],
      failed: [...this.failed],
      level: this.level(),
    };
  }
}
