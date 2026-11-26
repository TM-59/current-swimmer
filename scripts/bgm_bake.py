# Copyright 2026 TM-59
"""Gate the BGM candidates, cut a seamless loop from each and bake the picks to Ogg Vorbis.

Run it in the local-llm-ops music venv (numpy) with ffmpeg on PATH; no listening test is a gate
(project-context#276):

    ~/.local/share/local-llm-ops/genvenv/music/bin/python scripts/bgm_bake.py --takes <dir>

The gates are the ones of the earlier games (spot-gap #33, td-games #38), not loosened:

- every take: local-llm-ops tools/genmodels/mood.py. Ballads and story music use the soft gate
  (RMS, silence, clipping, key clarity, local key, flatness, timbre jump and motion); the voyage
  (battle role) uses PULSE_GATE (beat strength, noise, splicing, held notes) and a tempo within 5 %
  of the plan (double and half also count).
- loop: length bars x 4 beats; soft loops are 16 s or longer with a 2 s crossfade (shortened a
  bar at a time for the smallest timbre jump), pulse loops a 0.12 s crossfade with the beat
  phase checked.
  On the decoded Ogg: timbre jump at the seam <= 2x the median inside the loop; pulse also a beat
  phase jump <= 40 ms (or the loop's own median) and the tempo across the seam within 5 %.
- loudness: -16 LUFS +- 1.5 LU (ffmpeg ebur128), sample peak <= -1 dBFS (+0.5 for the decoder).
- dips: no stretch longer than 2 s where 0.5 s windows sit 12 dB under the loop's median; for pulse,
  at most 10 % of the windows that low.

Per scene the best passing take goes to web/bgm/<scene>.ogg, with web/bgm/list.json,
docs/bgm/report.json, docs/bgm/gate.md and a waveform and spectrum picture
docs/images/bgm-<scene>.png.
"""

from __future__ import annotations

import argparse
import importlib
import importlib.util
import json
import re
import shutil
import subprocess
import sys
import tempfile
from dataclasses import dataclass
from pathlib import Path
from typing import TYPE_CHECKING

import numpy as np

if TYPE_CHECKING:
    from collections.abc import Callable
    from types import ModuleType

    import numpy.typing as npt

    Wave = npt.NDArray[np.float64]

ROOT = Path(__file__).resolve().parent.parent
LLO = Path.home() / "dev/local-llm-ops"
SR = 48000
HOP = 1024
TARGET_LUFS, LUFS_TOL, PEAK_MAX_DB, PEAK_DECODE_SLACK = -16.0, 1.5, -1.0, 0.5
XF = 0.12
VORBIS_Q = "6"
SEAM_RATIO_MAX, PHASE_MS_MAX, TEMPO_TOL = 2.0, 40.0, 0.05
DIP_DB, DIP_MAX_S, LOW_FRAC_MAX = 12.0, 2.0, 0.10
SOFT_LOOP_MIN_S = 16.0
DECODE_SLACK_SAMPLES = 2  # a Vorbis round trip may differ by a sample or two
LOOP_HEAD_S = 0.6
SEAM_CONTEXT = 9
SEAM_WINDOW = 8
PHASE_WINDOW = 600
STRETCH = np.linspace(-0.006, 0.006, 13)
SOFT_KEYS = (
    "key",
    "key_clarity",
    "local_key",
    "chroma_continuity",
    "flatness",
    "timbre_jump_db",
    "timbre_motion_db",
)
Row = dict[str, object]
HEADER = (
    "| take | seed | gen s | RMS | silence | beat BPM / strength | key / clarity / local"
    " | flatness / jump / motion dB | loop s | LUFS | seam timbre / phase ms"
    " | dip s / low share | bytes | result |"
)


def ffmpeg() -> str:
    """Return the absolute path of ffmpeg."""
    path = shutil.which("ffmpeg")
    if path is None:
        message = "ffmpeg is required"
        raise SystemExit(message)
    return path


def load_gates() -> tuple[ModuleType, ModuleType]:
    """Import local-llm-ops tools/genmodels as a package from its folder (no sys.path change)."""
    folder = LLO / "tools/genmodels"
    spec = importlib.util.spec_from_file_location(
        "genmodels", folder / "__init__.py", submodule_search_locations=[str(folder)]
    )
    if spec is None or spec.loader is None:
        message = f"cannot load {folder}"
        raise SystemExit(message)
    pkg = importlib.util.module_from_spec(spec)
    sys.modules["genmodels"] = pkg
    spec.loader.exec_module(pkg)
    return importlib.import_module("genmodels.metrics"), importlib.import_module("genmodels.mood")


@dataclass(frozen=True)
class Scene:
    """One scene of docs/bgm/plan.json."""

    name: str
    bpm: float
    key: str
    bars: int
    seconds: float
    soft: bool
    xf: float

    @property
    def loop_samples(self) -> int:
        """Return the planned loop length: bars x 4 beats."""
        return round(self.bars * 4 * 60 / self.bpm * SR)


def scene_of(name: str, spec: dict[str, object]) -> Scene:
    """Read one scene of the plan."""
    return Scene(
        name=name,
        bpm=float(str(spec["bpm"])),
        key=str(spec["key"]),
        bars=int(str(spec["bars"])),
        seconds=float(str(spec["seconds"])),
        soft=spec.get("mood") == "soft",
        xf=float(str(spec.get("xf", XF))),
    )


def run(args: list[str]) -> subprocess.CompletedProcess[bytes]:
    """Run ffmpeg quietly and fail on error."""
    return subprocess.run([ffmpeg(), "-hide_banner", *args], check=True, capture_output=True)


def read_audio(path: Path) -> Wave:
    """Decode any audio file to 48 kHz stereo float samples, shape (n, 2)."""
    out = run(
        ["-loglevel", "error", "-i", str(path), "-f", "f32le", "-ac", "2", "-ar", str(SR), "-"]
    )
    return np.frombuffer(out.stdout, dtype="<f4").astype(np.float64).reshape(-1, 2)


def write_wav(y: Wave, dst: Path) -> None:
    """Write float samples as a 32-bit float WAV."""
    raw = np.ascontiguousarray(y, dtype="<f4").tobytes()
    head = [ffmpeg(), "-hide_banner", "-loglevel", "error", "-y", "-f", "f32le", "-ac", "2"]
    subprocess.run(
        [*head, "-ar", str(SR), "-i", "-", "-c:a", "pcm_f32le", str(dst)],
        input=raw,
        check=True,
        capture_output=True,
    )


def lufs(path: Path) -> tuple[float, float]:
    """Return the integrated loudness (LUFS) and the sample peak (dBFS) of a file."""
    err = run(["-nostats", "-i", str(path), "-af", "ebur128=peak=sample", "-f", "null", "-"])
    text = err.stderr.decode()
    level = float(re.findall(r"I:\s+(-?[\d.]+) LUFS", text)[-1])
    peak = re.findall(r"Peak:\s+(-?[\d.]+|-inf) dBFS", text)[-1]
    return level, float(peak) if peak != "-inf" else -120.0


def tempo_ok(bpm: float, want: float) -> bool:
    """Accept a detected tempo within 5 % of the plan, or of its double or half."""
    return any(abs(bpm * m - want) <= want * TEMPO_TOL for m in (1, 2, 0.5))


class Meter:
    """Measurements on decoded audio that use the local-llm-ops helpers."""

    def __init__(self, metrics: ModuleType) -> None:
        """Keep the metrics module."""
        self.metrics = metrics

    def mono(self, x: Wave) -> Wave:
        """Mix down to one channel."""
        return np.asarray(self.metrics.to_mono(x), dtype=np.float64)

    def spec_frames(self, x: Wave, n: int = 2048) -> Wave:
        """Return log energies in 32 bands up to about 9 kHz, one row per hop."""
        m = self.mono(x)
        k = 1 + max(0, (len(m) - n) // HOP)
        idx = np.arange(n)[None, :] + HOP * np.arange(k)[:, None]
        s = np.abs(np.fft.rfft(m[idx] * np.hanning(n), axis=1))[:, 1:400]
        bands = np.array_split(s, 32, axis=1)
        return np.log1p(np.stack([b.mean(axis=1) for b in bands], axis=1) * 100)

    def best_start(self, x: Wave, length: int, xf_s: float) -> int:
        """Find the loop start whose timbre best matches the timbre just after the loop end."""
        frames = self.spec_frames(x)
        last = (len(x) - length - int(xf_s * SR)) // HOP - SEAM_CONTEXT
        first = int(0.3 * SR / HOP) + SEAM_CONTEXT
        if last <= first:
            return int(0.3 * SR)
        best, arg = float("inf"), first
        w = SEAM_WINDOW
        for f in range(first, last):
            g = round(f + length / HOP)
            if g + SEAM_CONTEXT > len(frames):
                break
            d = float(
                np.mean(np.linalg.norm(frames[g - w : g + w] - frames[f - w : f + w], axis=1))
            )
            if d < best:
                best, arg = d, f
        return arg * HOP

    def onset_env(self, x: Wave) -> Wave:
        """Return a positive energy-flux envelope at 100 frames per second."""
        hop = SR // 100
        m = self.mono(x)
        k = len(m) // hop
        e = np.log1p((m[: k * hop].reshape(k, hop) ** 2).mean(axis=1) * 1e4)
        return np.maximum(np.diff(e, prepend=e[0]), 0)

    def window_db(self, z: Wave) -> Wave:
        """Return the level of each 0.5 s window in dB."""
        m = self.mono(z)
        n = SR // 2
        k = len(m) // n
        return 10 * np.log10(np.mean(m[: k * n].reshape(k, n) ** 2, axis=1) + 1e-12)

    def low_fraction(self, z: Wave) -> float:
        """Return the share of 0.5 s windows DIP_DB or more under the median level."""
        e = self.window_db(z)
        return round(float(np.mean(e < np.median(e) - DIP_DB)), 3) if len(e) else 0.0

    def longest_dip(self, z: Wave) -> float:
        """Return the longest stretch (s) of 0.5 s windows DIP_DB or more under the median level."""
        e = self.window_db(z)
        if not len(e):
            return 0.0
        best = current = 0
        for low in e < np.median(e) - DIP_DB:
            current = current + 1 if low else 0
            best = max(best, current)
        return best * 0.5

    def seam_report(self, y: Wave, bpm: float) -> Row:
        """Measure the seam of one loop by joining two copies; bpm is the detected beat."""
        two = np.concatenate([y, y])
        frames = self.spec_frames(two)
        gi = len(y) // HOP
        adj = np.linalg.norm(np.diff(frames[: gi - 1], axis=0), axis=1)
        across = float(np.linalg.norm(frames[gi] - frames[gi - 1]))
        env = self.onset_env(two)
        per = 60.0 / bpm
        c = len(y) * 100 // SR
        w = PHASE_WINDOW
        d = phase_gap(beat_phase_ms(env[c - w : c], per), beat_phase_ms(env[c : c + w], per), per)
        inside = [
            phase_gap(
                beat_phase_ms(env[s : s + w], per), beat_phase_ms(env[s + w : s + 2 * w], per), per
            )
            for s in range(0, c - 2 * w, w // 2)
        ]
        bs = self.metrics.beat_stability(two[len(y) - 4 * SR : len(y) + 4 * SR], SR)
        return {
            "seam_spec_ratio": round(across / max(float(np.median(adj)), 1e-9), 2),
            "seam_phase_ms": round(d, 1),
            "inside_phase_ms_median": round(float(np.median(inside)), 1) if inside else None,
            "seam_bpm": float(bs["bpm"]),
            "seam_beat_strength": float(bs["strength"]),
        }


def beat_phase_ms(env: Wave, period_s: float) -> float:
    """Return the phase (ms) of a beat of the given period in an onset envelope."""
    t = np.arange(len(env)) / 100.0
    z = complex(np.sum(env * np.exp(2j * np.pi * t / period_s)))
    return float((np.angle(z) % (2 * np.pi)) / (2 * np.pi) * period_s * 1000)


def phase_gap(a: float, b: float, period_s: float) -> float:
    """Return the circular distance (ms) between two beat phases."""
    q = abs(a - b)
    return min(q, period_s * 1000 - q)


def crossfaded(x: Wave, start: int, length: int, xf_s: float) -> Wave:
    """Cut x[start:start+length] and blend the audio after its end into its head."""
    xf = int(xf_s * SR)
    y = x[start : start + length].copy()
    fade = (np.sin(np.linspace(0, np.pi / 2, xf)) ** 2)[:, None]
    y[:xf] = x[start : start + xf] * fade + x[start + length : start + length + xf] * (1 - fade)
    return y


class Baker:
    """Gate, loop, normalise and encode the takes of one plan."""

    def __init__(self, metrics: ModuleType, mood: ModuleType) -> None:
        """Keep the gate modules."""
        self.metrics, self.mood, self.meter = metrics, mood, Meter(metrics)

    def make_loop(self, x: Wave, length: int, xf_s: float) -> tuple[int, Wave]:
        """Return the best start and the crossfaded loop of one length."""
        start = self.meter.best_start(x, length, xf_s)
        return start, crossfaded(x, start, length, xf_s)

    def cut_soft(self, x: Wave, sc: Scene) -> tuple[int, int, Wave]:
        """Shorten a bar at a time (16 s or longer) and keep the smallest timbre jump."""
        bar = sc.loop_samples / sc.bars
        best: tuple[float, int, int, Wave] | None = None
        for k in range(sc.bars, 0, -1):
            length = round(bar * k)
            if length < SOFT_LOOP_MIN_S * SR:
                break
            start, y = self.make_loop(x, length, sc.xf)
            ratio = float(str(self.meter.seam_report(y, sc.bpm)["seam_spec_ratio"]))
            if best is None or ratio < best[0]:
                best = (ratio, length, start, y)
        if best is None:
            start, y = self.make_loop(x, sc.loop_samples, sc.xf)
            return sc.loop_samples, start, y
        return best[1], best[2], best[3]

    def cut_pulse(self, x: Wave, sc: Scene, beat: float) -> tuple[int, int, Wave]:
        """Try the planned length +-0.6 % and keep the smallest beat phase jump at the seam."""
        best: tuple[tuple[float, float], int, int, Wave] | None = None
        for d in STRETCH:
            length = round(sc.loop_samples * (1 + float(d)))
            if len(x) < length + int(sc.xf * SR) + int(LOOP_HEAD_S * SR):
                continue
            start, y = self.make_loop(x, length, sc.xf)
            sm = self.meter.seam_report(y, beat)
            key = (float(str(sm["seam_phase_ms"])), float(str(sm["seam_spec_ratio"])))
            if best is None or key < best[0]:
                best = (key, length, start, y)
        if best is None:
            start, y = self.make_loop(x, sc.loop_samples, sc.xf)
            return sc.loop_samples, start, y
        return best[1], best[2], best[3]

    def limit(self, y: Wave) -> Wave:
        """Apply a look-ahead limiter (-2 dBFS) on three joined loops and keep the middle one."""
        with tempfile.TemporaryDirectory(prefix="bgm-") as td:
            a, b = Path(td) / "a.wav", Path(td) / "b.wav"
            write_wav(np.concatenate([y, y, y]), a)
            limiter = "alimiter=limit=0.79:attack=5:release=80:level=false:latency=true"
            run(
                [
                    "-loglevel",
                    "error",
                    "-y",
                    "-i",
                    str(a),
                    "-af",
                    limiter,
                    "-c:a",
                    "pcm_f32le",
                    str(b),
                ]
            )
            z = read_audio(b)
        return z[len(y) : 2 * len(y)]

    def normalize(self, y: Wave) -> tuple[Wave, float]:
        """Bring the loop to TARGET_LUFS; limit first when the peak would pass PEAK_MAX_DB."""
        with tempfile.TemporaryDirectory(prefix="bgm-") as td:
            p = Path(td) / "n.wav"
            write_wav(y, p)
            level, _ = lufs(p)
        g = 10 ** ((TARGET_LUFS - level) / 20)
        cap = 10 ** (PEAK_MAX_DB / 20)
        gain_db = round(20 * float(np.log10(g)), 2)
        if float(np.max(np.abs(y))) * g <= cap:
            return y * g, gain_db
        z = self.limit(y * g)
        with tempfile.TemporaryDirectory(prefix="bgm-") as td:
            p = Path(td) / "n.wav"
            write_wav(z, p)
            level, _ = lufs(p)
        g2 = min(10 ** ((TARGET_LUFS - level) / 20), cap / max(float(np.max(np.abs(z))), 1e-9))
        return z * g2, gain_db

    def _encode(self, wav: Path, dst: Path) -> Wave:
        """Write dst from a WAV and decode it back."""
        run(
            [
                "-loglevel",
                "error",
                "-y",
                "-i",
                str(wav),
                "-c:a",
                "libvorbis",
                "-q:a",
                VORBIS_Q,
                str(dst),
            ]
        )
        return read_audio(dst)

    def take_gate(self, x: Wave, sc: Scene) -> tuple[Row, list[str]]:
        """Run the mood gate (and the tempo for pulse) on a whole take."""
        g = self.mood.mood_gate(x, SR, "soft" if sc.soft else "pulse")
        rep: dict[str, object] = dict(g["report"])
        fail = [str(f) for f in g["fail"]]
        beat = float(str(rep["beat_bpm"]))
        if not sc.soft and not tempo_ok(beat, sc.bpm):
            fail.append(f"tempo {beat} != {sc.bpm}+-5%")
        row: Row = {"trim_seconds": rep["seconds"], "gate": "soft" if sc.soft else "pulse"}
        for k in ("rms_dbfs", "longest_silence_s", "clip_ratio", "beat_bpm", "beat_strength"):
            row[k] = rep[k]
        for k in SOFT_KEYS:
            row[f"soft_{k}"] = rep[k]
        return row, fail

    def shipped_gate(self, z: Wave, y: Wave, sc: Scene, beat: float) -> tuple[Row, list[str]]:
        """Gate the decoded Ogg: dips, clipping, length and the seam."""
        fail: list[str] = []
        row: Row = {"dip_s": self.meter.longest_dip(z), "low_frac": self.meter.low_fraction(z)}
        if float(str(row["dip_s"])) > DIP_MAX_S:
            fail.append(f"dip {row['dip_s']}s")
        if not sc.soft and float(str(row["low_frac"])) > LOW_FRAC_MAX:
            fail.append(f"low {row['low_frac']}")
        if float(self.metrics.clip_ratio(z)) > float(self.metrics.MUSIC_GATE["clip_ratio_max"]):
            fail.append("clip after encode")
        if abs(len(z) - len(y)) > DECODE_SLACK_SAMPLES:
            fail.append(f"decoded length {len(z)} != {len(y)}")
        pulse = beat if tempo_ok(beat, sc.bpm) else sc.bpm
        sm = self.meter.seam_report(z, pulse)
        row.update(sm)
        row["phase_pulse_bpm"] = pulse
        ratio = float(str(sm["seam_spec_ratio"]))
        if ratio > SEAM_RATIO_MAX:
            fail.append(f"seam spec {ratio}")
        if sc.soft:
            return row, fail
        inside = sm["inside_phase_ms_median"]
        limit = max(PHASE_MS_MAX, float(str(inside)) if inside is not None else 0.0)
        if float(str(sm["seam_phase_ms"])) > limit:
            fail.append(f"seam phase {sm['seam_phase_ms']}ms")
        if not tempo_ok(float(str(sm["seam_bpm"])), sc.bpm):
            fail.append(f"seam tempo {sm['seam_bpm']}")
        return row, fail

    def bake_take(self, path: Path, sc: Scene, outdir: Path) -> Row:
        """Gate one take; when it is long enough, cut, normalise, encode and gate the result."""
        x = np.asarray(self.metrics.trim_tail(read_audio(path), SR), dtype=np.float64)
        row, fail = self.take_gate(x, sc)
        row = {"take": path.stem, **row}
        xf = int(sc.xf * SR)
        if len(x) < sc.loop_samples + xf + int(LOOP_HEAD_S * SR):
            return {**row, "fail": [*fail, f"short {row['trim_seconds']}s"]}
        beat = float(str(row["beat_bpm"]))
        pulse = beat if tempo_ok(beat, sc.bpm) else sc.bpm
        length, start, y = self.cut_soft(x, sc) if sc.soft else self.cut_pulse(x, sc, pulse)
        row.update(
            loop_start_s=round(start / SR, 3),
            loop_seconds=round(length / SR, 3),
            loop_beats=round(length / SR * sc.bpm / 60),
        )
        y, gain_db = self.normalize(y)
        dst = outdir / f"{path.stem}.ogg"
        z = self._encode_wave(y, dst)
        level, peak = lufs(dst)
        row.update(
            gain_db=gain_db,
            lufs=level,
            peak_dbfs=peak,
            bytes=dst.stat().st_size,
            decoded_seconds=round(len(z) / SR, 3),
        )
        if abs(level - TARGET_LUFS) > LUFS_TOL:
            fail.append(f"lufs {level}")
        if peak > PEAK_MAX_DB + PEAK_DECODE_SLACK:
            fail.append(f"peak {peak}")
        shipped, more = self.shipped_gate(z, y, sc, beat)
        row.update(shipped)
        row["fail"] = fail + more
        return row

    def _encode_wave(self, y: Wave, dst: Path) -> Wave:
        """Encode float samples to dst and decode them back."""
        with tempfile.TemporaryDirectory(prefix="bgm-") as td:
            wav = Path(td) / "a.wav"
            write_wav(y, wav)
            return self._encode(wav, dst)

    def score(self, sc: Scene) -> Callable[[Row], float]:
        """Return the pick score: soft key clarity x local key / seam, pulse beat / seam / phase."""

        def seam(r: Row) -> float:
            return max(float(str(r.get("seam_spec_ratio", 1))), 0.3)

        def soft(r: Row) -> float:
            return float(str(r["soft_key_clarity"])) * float(str(r["soft_local_key"])) / seam(r)

        def pulse(r: Row) -> float:
            phase = float(str(r.get("seam_phase_ms", 0)))
            return float(str(r["beat_strength"])) / seam(r) / (1 + phase / PHASE_MS_MAX)

        return soft if sc.soft else pulse


def picture(ogg: Path, png: Path) -> None:
    """Draw the waveform and a log-frequency spectrogram of two joined loops (seam mid-way)."""
    graph = (
        "[0:a][0:a]concat=n=2:v=0:a=1,aformat=channel_layouts=mono,asplit[a][b];"
        "[a]showwavespic=s=960x180:colors=0x4aa3df[w];"
        "[b]showspectrumpic=s=960x260:legend=0:fscale=log:color=magma[s];"
        "[w][s]vstack"
    )
    run(
        [
            "-loglevel",
            "error",
            "-y",
            "-i",
            str(ogg),
            "-filter_complex",
            graph,
            "-frames:v",
            "1",
            str(png),
        ]
    )


def gate_lines(report: dict[str, object], soft_gate: dict[str, object]) -> list[str]:
    """Return the header of docs/bgm/gate.md: every gate and its value."""
    g = report["gate"]
    if not isinstance(g, dict):
        return []
    sg = soft_gate
    every = (
        f"Every take: RMS {g['rms_dbfs_min']}..{g['rms_dbfs_max']} dBFS, longest silence <= "
        f"{g['longest_silence_s_max']} s, clipping <= {g['clip_ratio_max']}, "
        f"{report['target_lufs']} LUFS +-{LUFS_TOL}, peak <= {PEAK_MAX_DB} dBFS."
    )
    pulse = (
        f"Pulse (voyage): PULSE_GATE, beat strength >= {g['beat_strength_min']}, tempo "
        f"+-{int(TEMPO_TOL * 100)} % (double or half too), seam beat phase <= {PHASE_MS_MAX} ms "
        f"(or the loop's own median), seam tempo +-5 %, windows {DIP_DB:g} dB under the median "
        f"<= {LOW_FRAC_MAX:g} of the loop."
    )
    soft = (
        f"Soft (ballad, story, ending): key clarity >= {sg['key_clarity_min']}, local key >= "
        f"{sg['local_key_min']}, flatness <= {sg['flatness_max']}, timbre jump <= "
        f"{sg['timbre_jump_db_max']} dB, motion >= {sg['timbre_motion_db_min']} dB; "
        f"loops {SOFT_LOOP_MIN_S:g} s or longer."
    )
    loop = (
        f"Every loop: seam timbre jump <= {SEAM_RATIO_MAX}x the median inside, no dip "
        f"({DIP_DB:g} dB under the median) longer than {DIP_MAX_S:g} s."
    )
    model = f"Model {report['engine']} (steps {report['steps']}, DiT only), all instrumental."
    title = "# BGM gate values (generated by scripts/bgm_bake.py; do not edit)"
    return [
        title,
        "",
        model,
        every,
        pulse,
        soft,
        loop,
        "",
    ]


def take_line(r: Row) -> str:
    """Return one table row of docs/bgm/gate.md."""
    fail = r.get("fail")
    reasons = "; ".join(str(f) for f in fail) if isinstance(fail, list) and fail else "pass"
    cells = [
        r["take"],
        r.get("seed"),
        r.get("gen_s"),
        r.get("rms_dbfs"),
        r.get("longest_silence_s"),
        f"{r.get('beat_bpm')} / {r.get('beat_strength')}",
        f"{r.get('soft_key')} / {r.get('soft_key_clarity')} / {r.get('soft_local_key')}",
        " / ".join(
            str(r.get(k)) for k in ("soft_flatness", "soft_timbre_jump_db", "soft_timbre_motion_db")
        ),
        r.get("loop_seconds", "-"),
        r.get("lufs", "-"),
        f"{r.get('seam_spec_ratio', '-')} / {r.get('seam_phase_ms', '-')}",
        f"{r.get('dip_s', '-')} / {r.get('low_frac', '-')}",
        r.get("bytes", "-"),
        reasons,
    ]
    return "| " + " | ".join(str(c) for c in cells) + " |"


def write_table(report: dict[str, object], soft_gate: dict[str, object]) -> None:
    """Write docs/bgm/gate.md: every candidate with its gate values."""
    out = gate_lines(report, soft_gate)
    scenes = report["scenes"]
    if not isinstance(scenes, dict):
        return
    for name, sc in scenes.items():
        out += [
            f"## {name} ({sc['bpm']} BPM, {sc['key']}, {sc['mood']})",
            "",
            f"Picked: {sc['picked'] or 'none (no take passed)'}",
            "",
            HEADER,
            "|---|---|---|---|---|---|---|---|---|---|---|---|---|---|",
        ]
        out += [take_line(r) for r in sc["takes"]]
        out.append("")
    (ROOT / "docs/bgm/gate.md").write_text("\n".join(out) + "\n")


def bake_scene(baker: Baker, sc: Scene, takes: Path, work: Path, gen: dict[str, Row]) -> list[Row]:
    """Bake every take of one scene."""
    rows: list[Row] = []
    for p in sorted(takes.glob(f"{sc.name}-c*-s*.flac")):
        r = baker.bake_take(p, sc, work)
        g = gen.get(p.stem, {})
        r.update(caption=g.get("caption"), seed=g.get("seed"), gen_s=g.get("gen_s"))
        rows.append(r)
        sys.stdout.write(f"{sc.name} {p.stem} {r['fail'] or 'PASS'}\n")
    return rows


def main(argv: list[str] | None = None) -> int:
    """Bake every scene of docs/bgm/plan.json from a folder of takes."""
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0] if __doc__ else "")
    ap.add_argument("--takes", required=True, help="the --out folder of bgm_gen.py or tools.colab")
    ap.add_argument("--work", default=None, help="where the candidates go (default <takes>/baked)")
    a = ap.parse_args(argv)
    metrics, mood = load_gates()
    baker = Baker(metrics, mood)
    plan = json.loads((ROOT / "docs/bgm/plan.json").read_text(encoding="utf-8"))
    takes = Path(a.takes)
    work = Path(a.work or takes / "baked")
    work.mkdir(parents=True, exist_ok=True)
    gen = {
        str(g["take"]): g
        for g in json.loads((takes / "gen.json").read_text())
        if "take" in g and "error" not in g
    }
    gate = {**metrics.MUSIC_GATE, "tempo_tol": TEMPO_TOL, "lufs_tol": LUFS_TOL}
    scenes: dict[str, object] = {}
    report: dict[str, object] = {
        "engine": plan["engine"],
        "steps": plan["steps"],
        "target_lufs": TARGET_LUFS,
        "gate": gate,
        "soft_gate": dict(mood.SOFT_GATE),
        "scenes": scenes,
    }
    web = ROOT / "web/bgm"
    web.mkdir(parents=True, exist_ok=True)
    for name, spec in plan["scenes"].items():
        sc = scene_of(name, spec)
        rows = bake_scene(baker, sc, takes, work, gen)
        ok = [r for r in rows if not r["fail"]]
        pick = max(ok, key=baker.score(sc)) if ok else None
        if pick is not None:
            shutil.copyfile(work / f"{pick['take']}.ogg", web / f"{name}.ogg")
            picture(web / f"{name}.ogg", ROOT / f"docs/images/bgm-{name}.png")
        elif (web / f"{name}.ogg").exists():
            (web / f"{name}.ogg").unlink()
        scenes[name] = {
            "bpm": sc.bpm,
            "key": sc.key,
            "mood": "soft" if sc.soft else "pulse",
            "picked": pick["take"] if pick else None,
            "takes": rows,
        }
    picked = sorted(n for n, s in scenes.items() if isinstance(s, dict) and s["picked"])
    (web / "list.json").write_text(json.dumps(picked) + "\n")
    (ROOT / "docs/bgm/report.json").write_text(
        json.dumps(report, ensure_ascii=False, indent=1) + "\n"
    )
    write_table(report, dict(mood.SOFT_GATE))
    sys.stdout.write(f"scenes with a passing take: {len(picked)}/{len(scenes)}\n")
    return 0


if __name__ == "__main__":
    sys.exit(main())
