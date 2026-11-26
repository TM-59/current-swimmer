# Copyright 2026 TM-59
"""Make BGM candidates with ACE-Step 1.5 turbo (MIT) on the local CPU, no API key.

This is the fallback of the Colab route (project-context#598, local-llm-ops #260). Run it in the
local-llm-ops music venv, which has torch and ACE-Step; the model loads once for the whole plan:

    ~/.local/share/local-llm-ops/genvenv/music/bin/python scripts/bgm_gen.py --out <dir>

The Colab route writes the same files and is tried first:

    cd ~/dev/local-llm-ops && .venv/bin/python -m tools.colab bgm --repo <this repo> --out <dir>

The plan is docs/bgm/plan.json (scene x 2 captions x 6 seeds). Each take is
<out>/<scene>-c<i>-s<seed>.flac, with <out>/gen.json (seconds and peak memory per take). Takes
already on disk are skipped, so a run can stop and resume. scripts/bgm_bake.py gates and bakes them.
"""

from __future__ import annotations

import argparse
import importlib
import importlib.util
import json
import os
import resource
import shutil
import sys
import time
from dataclasses import dataclass
from pathlib import Path
from typing import TYPE_CHECKING

if TYPE_CHECKING:
    from types import ModuleType

ROOT = Path(__file__).resolve().parent.parent
PLAN = ROOT / "docs/bgm/plan.json"
LLO = Path.home() / "dev/local-llm-ops"


@dataclass(frozen=True)
class Job:
    """One take: a scene, one of its captions and one seed."""

    scene: str
    spec: dict[str, object]
    caption_i: int
    caption: str
    seed: int
    dst: Path


def load_package(name: str, folder: Path) -> ModuleType:
    """Import a package from a folder without touching sys.path."""
    spec = importlib.util.spec_from_file_location(
        name, folder / "__init__.py", submodule_search_locations=[str(folder)]
    )
    if spec is None or spec.loader is None:
        message = f"cannot load {folder}"
        raise SystemExit(message)
    pkg = importlib.util.module_from_spec(spec)
    sys.modules[name] = pkg
    spec.loader.exec_module(pkg)
    return pkg


def seeds_of(plan: dict[str, object], spec: dict[str, object]) -> list[int]:
    """Return the seeds of a scene: its own list, or the plan's."""
    seeds = spec.get("seeds", plan["seeds"])
    if not isinstance(seeds, list):
        message = "seeds must be a list"
        raise SystemExit(message)
    return [int(s) for s in seeds]


def plan_jobs(plan: dict[str, object], out: Path, only: str, check: object) -> list[Job]:
    """List the takes still missing, the first take of every scene first."""
    scenes = plan["scenes"]
    if not isinstance(scenes, dict):
        message = "plan.scenes must be an object"
        raise SystemExit(message)
    order = list(scenes)
    wanted = set(only.split(",")) if only else set(order)
    jobs: list[Job] = []
    for name, spec in scenes.items():
        if name not in wanted:
            continue
        for ci, cap in enumerate(spec["captions"]):
            if callable(check):
                check(cap)
            for seed in seeds_of(plan, spec):
                dst = out / f"{name}-c{ci}-s{seed}.flac"
                if not dst.exists():
                    jobs.append(Job(name, spec, ci, str(cap), seed, dst))
    jobs.sort(
        key=lambda j: (
            j.caption_i + seeds_of(plan, j.spec).index(j.seed) * 10,
            order.index(j.scene),
        )
    )
    return jobs


def params_of(job: Job, plan: dict[str, object]) -> dict[str, object]:
    """Return the ACE-Step generation parameters of one take."""
    spec = job.spec
    vocal = bool(spec.get("vocal"))
    return {
        "caption": job.caption,
        "duration": spec["seconds"],
        "instrumental": not vocal,
        "seed": job.seed,
        "vocal_language": spec.get("vocal_language", "unknown"),
        "inference_steps": plan["steps"],
        "thinking": False,
        "bpm": spec["bpm"],
        "keyscale": spec["key"] if spec.get("pass_key", True) else "",
        "timesignature": "4",
        "use_cot_metas": False,
        "use_cot_caption": False,
        "use_cot_language": False,
        "lyrics": spec["lyrics"] if vocal else "[Instrumental]",
    }


def main(argv: list[str] | None = None) -> int:
    """Generate the missing takes of the plan."""
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0] if __doc__ else "")
    ap.add_argument("--out", required=True)
    ap.add_argument("--only", default="")
    ap.add_argument("--threads", type=int, default=int(os.environ.get("GEN_THREADS", "16")))
    ap.add_argument(
        "--max-seconds", type=float, default=2700, help="stop starting takes after this"
    )
    a = ap.parse_args(argv)
    tools = load_package("tools", LLO / "tools")
    compose = importlib.import_module("tools.musicgen.compose")
    plan = json.loads(PLAN.read_text(encoding="utf-8"))
    out = Path(a.out).resolve()
    out.mkdir(parents=True, exist_ok=True)
    jobs = plan_jobs(plan, out, a.only, compose.check_caption)
    sys.stdout.write(f"jobs {len(jobs)} ({tools.__name__})\n")
    if not jobs:
        return 0
    eng = compose.CONFIG["engines"][compose.CONFIG["default_engine"]]
    os.environ.setdefault("ACESTEP_CHECKPOINTS_DIR", str(compose.prepare_checkpoints(eng)))
    os.environ["HF_HUB_OFFLINE"] = "1"
    importlib.import_module("torch").set_num_threads(a.threads)
    downloads = importlib.import_module("acestep.core.generation.handler.init_service_downloads")
    parts = (eng["dit"], "vae", "Qwen3-Embedding-0.6B")
    downloads.__dict__["check_main_model_exists"] = lambda ck: all(
        any((Path(ck) / c).glob("*.safetensors")) for c in parts
    )
    handler = importlib.import_module("acestep.handler")
    inference = importlib.import_module("acestep.inference")
    t0 = time.time()
    dit = handler.AceStepHandler()
    status, ok = dit.initialize_service(
        project_root=str(Path(eng["src"]).expanduser()),
        config_path=eng["dit"],
        device="cpu",
        use_mlx_dit=False,
    )
    if not ok:
        message = f"initialisation failed: {status}"
        raise SystemExit(message)
    log_path = out / "gen.json"
    log = json.loads(log_path.read_text()) if log_path.exists() else []
    log.append({"event": "load", "s": round(time.time() - t0, 1)})
    fields = inference.GenerationParams.__dataclass_fields__
    for job in jobs:
        if time.time() - t0 > a.max_seconds:
            sys.stdout.write("out of time; the next run continues\n")
            break
        kw = {k: v for k, v in params_of(job, plan).items() if k in fields}
        cfg = inference.GenerationConfig(
            batch_size=1, audio_format="flac", use_random_seed=False, seeds=[job.seed]
        )
        t1 = time.time()
        res = inference.generate_music(
            dit, None, inference.GenerationParams(**kw), cfg, save_dir=str(out / "tmp")
        )
        if not res.success:
            log.append({"take": job.dst.stem, "error": str(res.error)})
            continue
        shutil.move(res.audios[0]["path"], job.dst)
        log.append(
            {
                "take": job.dst.stem,
                "scene": job.scene,
                "caption_i": job.caption_i,
                "caption": job.caption,
                "seed": job.seed,
                "bpm": job.spec["bpm"],
                "key": job.spec["key"],
                "seconds_req": job.spec["seconds"],
                "gen_s": round(time.time() - t1, 1),
                "max_rss_mb": resource.getrusage(resource.RUSAGE_SELF).ru_maxrss // 1024,
            }
        )
        log_path.write_text(json.dumps(log, ensure_ascii=False, indent=1))
        sys.stdout.write(json.dumps(log[-1], ensure_ascii=False) + "\n")
    log_path.write_text(json.dumps(log, ensure_ascii=False, indent=1))
    return 0


if __name__ == "__main__":
    sys.exit(main())
