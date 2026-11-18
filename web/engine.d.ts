// Types of the WebAssembly module that `make web` (scripts/build-web.sh) builds as dist/physics.js
// from the C sources (the EXPORTED_FUNCTIONS list). Used only by `tsc --checkJs`; nothing is emitted.

/** Pointers are byte offsets into HEAPF32's buffer. */
interface SwimmerModule {
  HEAPF32: Float32Array;
  _view_init(): number;
  _view_step(turn: number, stroke: number): void;
  _view_state(): number;
  _view_field(): number;
  _jw_init(): number;
  _jw_step(turn: number, stroke: number): void;
  _jw_answer(option: number): number;
  _jw_state(): number;
  _jw_field(): number;
  _wv_init(reynolds: number, cellsPerDiameter: number): number;
  _wv_step(steps: number): void;
  _wv_state(): number;
  _wv_field(): number;
  _wv_trace(): number;
  _wv_trace_count(): number;
  _wv_trace_dt(): number;
}

/** Loaded by the classic script dist/physics.js before web/app.js and web/wake.js run. */
declare function createSwimmer(): Promise<SwimmerModule>;

/** One heavy scene as it opened: whether a banner or a loud readout was still showing. */
interface HeavyEntry {
  id: string;
  banner: number;
  loud: number;
  bannerShown: boolean;
}

/** window.swimmerView: read-only diagnostics of web/app.js for scripts/smoke.mjs. */
interface SwimmerView {
  mode: string;
  paused: boolean;
  art: boolean[];
  /** The C state of the running mode (see state() in web/app.js). */
  state: number[];
  held: string[];
  trail: number;
  aim: boolean;
  journey: {
    scene: string | null;
    reach: number;
    banner: number;
    bannerShown: boolean;
    stamina: number;
    staminaMax: number;
    mark: number[];
    time: number;
    sweeps: number;
    silt: number;
    route: number;
    stay: number;
    ending: number;
    loud: number;
    hold: number[];
    heavy: HeavyEntry[];
    sceneText: string | null;
  };
}

/** The C wake readout of web/wake.js (src/wake_web.c, by name). */
interface WakeReadout {
  t: number;
  re: number;
  cd: number;
  cl: number;
  st: number;
  cdMean: number;
  clAmp: number;
  periods: number;
  nx: number;
  ny: number;
  n: number;
  lr: number;
  div: number;
  from: number;
  sweeps: number;
}

/** window.wakeView: the wake page's readout and controls for scripts/smoke.mjs. */
interface WakeView {
  state(): WakeReadout;
  advance(steps: number): void;
  start(re: number): void;
  roshko(re: number): number;
}

/** Common test hook of the playability gate (read-only; canvas pixels). */
interface TmGate {
  version: number;
  readonly logical: { w: number; h: number };
  scene(): string;
  goal(): boolean;
  texts(): { text: string; px: number }[];
  targets(): unknown[];
  player(): { x: number; y: number } | null;
  hint(): { x: number; y: number } | null;
}

interface Window {
  readonly swimmerView?: SwimmerView;
  wakeView?: WakeView;
  __tmGate?: TmGate;
  /** The interval of the in-page pilot of scripts/smoke.mjs. */
  __pilot?: ReturnType<typeof setInterval>;
}
