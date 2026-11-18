"""Publish the common isolated runner's results in this repository."""

import json
import os
import platform
import shutil
import subprocess
import sys
import tempfile
import time
import uuid
from datetime import UTC, datetime
from pathlib import Path


def main() -> int:
    if len(sys.argv) != 2 or sys.argv[1] not in {"all", "unit", "trace", "perf", "e2e"}:
        raise SystemExit("usage: local-test.py all|unit|trace|perf|e2e")
    root = Path.cwd().resolve()
    expected = Path(os.environ.get("DEV_DIR", str(Path.home() / "dev"))) / "current-swimmer"
    if root != expected.resolve():
        raise SystemExit("local-test requires the inventory checkout at DEV_DIR/current-swimmer")
    output = root / "test-results"
    output.mkdir(exist_ok=True)
    latest = output / "summary.json"
    latest.unlink(missing_ok=True)
    now = datetime.now(UTC)
    run_id = now.strftime("%Y%m%dT%H%M%S%fZ") + "-" + uuid.uuid4().hex[:8]
    commit = subprocess.run(["git", "rev-parse", "HEAD"], capture_output=True, text=True, check=True).stdout.strip()
    dirty = bool(subprocess.run(["git", "status", "--porcelain"], capture_output=True, text=True, check=True).stdout)
    summary = {
        "schema_version": 1, "run_id": run_id, "repository": "TM-59/current-swimmer",
        "commit": commit, "dirty": dirty, "started_at": now.isoformat(), "finished_at": now.isoformat(),
        "duration_seconds": 0, "status": "incomplete", "exit_code": None,
        "environment": {"os": platform.system(), "architecture": platform.machine(), "tools": {}, "jobs": 1, "seed": 42},
        "counts": dict.fromkeys(("tests", "failures", "errors", "skipped"), 0),
        "coverage": {"status": "not_measured", "line_percent": None, "branch_percent": None, "reason": "Coverage is not collected."},
        "metrics": [], "suites": [],
    }

    def publish(data: dict) -> None:
        tmp = output / (".summary-" + run_id + ".json")
        tmp.write_text(json.dumps(data, indent=2) + "\n")
        os.replace(tmp, latest)

    publish(summary)
    started = time.monotonic()
    context = Path(os.environ.get("TM_TEST_CONTEXT", str(Path.home() / "project-context")))
    command = [sys.executable, str(context / "bin/tm-test.py"), "current-swimmer", "--seed", "42"]
    if sys.argv[1] != "all":
        command.extend(["--kind", sys.argv[1]])
    with tempfile.TemporaryDirectory(prefix=".tm-run-", dir=output) as temp:
        store = Path(temp)
        env = dict(os.environ, TM_TEST_STORE=str(store))
        try:
            result = subprocess.run(command, env=env, check=False)
            saved = store / "current-swimmer/summary.json"
            if not saved.is_file():
                raise RuntimeError("common runner did not publish a summary")
            data = json.loads(saved.read_text())
            source = store / "current-swimmer" / data["run_id"]
            reports = source / "test-results" / data["run_id"]
            destination = output / data["run_id"]
            destination.mkdir()
            if reports.is_dir():
                shutil.copytree(reports, destination, dirs_exist_ok=True)
            for name in ("summary.json", "trace.json", "trace.md"):
                if (source / name).is_file():
                    shutil.copy2(source / name, destination / name)
            if result.returncode and data["status"] == "pass":
                raise RuntimeError("common runner exited nonzero after reporting pass")
            publish(data)
            return result.returncode or (0 if data["status"] == "pass" else 1)
        except (OSError, ValueError, RuntimeError, subprocess.SubprocessError) as error:
            print(f"local test failed: {error}", file=sys.stderr)
            summary.update(status="error", exit_code=1, finished_at=datetime.now(UTC).isoformat(),
                           duration_seconds=round(time.monotonic() - started, 3))
            publish(summary)
            return 1


if __name__ == "__main__":
    raise SystemExit(main())
