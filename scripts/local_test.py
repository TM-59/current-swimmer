# Copyright 2026 TM-59
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
from typing import Any

KINDS = frozenset({"all", "unit", "trace", "perf", "e2e"})


class UsageError(SystemExit):
    """Wrong arguments or the wrong checkout."""

    def __init__(self, reason: str) -> None:
        """Exit with `reason` as the message."""
        super().__init__(reason)


class RunnerError(RuntimeError):
    """The common runner did not leave a usable result."""


def program(name: str) -> str:
    """Absolute path of `name` on PATH (git has no fixed location across machines)."""
    path = shutil.which(name)
    if path is None:
        message = f"{name} is not on PATH"
        raise RunnerError(message)
    return path


def check_arguments(argv: list[str]) -> str:
    """Return the requested kind, or exit with the usage line."""
    if len(argv) != len(("program", "kind")) or argv[1] not in KINDS:
        reason = "usage: local_test.py all|unit|trace|perf|e2e"
        raise UsageError(reason)
    root = Path.cwd().resolve()
    expected = Path(os.environ.get("DEV_DIR", str(Path.home() / "dev"))) / "current-swimmer"
    if root != expected.resolve():
        reason = "local_test requires the inventory checkout at DEV_DIR/current-swimmer"
        raise UsageError(reason)
    return argv[1]


def git_state(git: str) -> tuple[str, bool]:
    """Return the checked-out commit and whether the working tree has changes."""
    commit = subprocess.run(
        [git, "rev-parse", "HEAD"], capture_output=True, text=True, check=True
    ).stdout.strip()
    dirty = bool(
        subprocess.run(
            [git, "status", "--porcelain"], capture_output=True, text=True, check=True
        ).stdout
    )
    return commit, dirty


def collect(store: Path, output: Path) -> dict[str, Any]:
    """Copy the runner's reports out of its scratch store and return its summary."""
    saved = store / "current-swimmer/summary.json"
    if not saved.is_file():
        message = "common runner did not publish a summary"
        raise RunnerError(message)
    data: dict[str, Any] = json.loads(saved.read_text())
    source = store / "current-swimmer" / data["run_id"]
    reports = source / "test-results" / data["run_id"]
    destination = output / data["run_id"]
    destination.mkdir()
    if reports.is_dir():
        shutil.copytree(reports, destination, dirs_exist_ok=True)
    for name in ("summary.json", "trace.json", "trace.md"):
        if (source / name).is_file():
            shutil.copy2(source / name, destination / name)
    return data


def main() -> int:
    """Run the common runner for the requested kind and publish test-results/summary.json."""
    kind = check_arguments(sys.argv)
    root = Path.cwd().resolve()
    output = root / "test-results"
    output.mkdir(exist_ok=True)
    latest = output / "summary.json"
    latest.unlink(missing_ok=True)
    now = datetime.now(UTC)
    run_id = now.strftime("%Y%m%dT%H%M%S%fZ") + "-" + uuid.uuid4().hex[:8]
    commit, dirty = git_state(program("git"))
    summary: dict[str, Any] = {
        "schema_version": 1,
        "run_id": run_id,
        "repository": "TM-59/current-swimmer",
        "commit": commit,
        "dirty": dirty,
        "started_at": now.isoformat(),
        "finished_at": now.isoformat(),
        "duration_seconds": 0,
        "status": "incomplete",
        "exit_code": None,
        "environment": {
            "os": platform.system(),
            "architecture": platform.machine(),
            "tools": {},
            "jobs": 1,
            "seed": 42,
        },
        "counts": dict.fromkeys(("tests", "failures", "errors", "skipped"), 0),
        "coverage": {
            "status": "not_measured",
            "line_percent": None,
            "branch_percent": None,
            "reason": "Coverage is not collected.",
        },
        "metrics": [],
        "suites": [],
    }

    def publish(data: dict[str, Any]) -> None:
        tmp = output / (".summary-" + run_id + ".json")
        tmp.write_text(json.dumps(data, indent=2) + "\n")
        tmp.replace(latest)

    publish(summary)
    started = time.monotonic()
    context = Path(os.environ.get("TM_TEST_CONTEXT", str(Path.home() / "project-context")))
    command = [sys.executable, str(context / "bin/tm-test.py"), "current-swimmer", "--seed", "42"]
    if kind != "all":
        command.extend(["--kind", kind])
    with tempfile.TemporaryDirectory(prefix=".tm-run-", dir=output) as temp:
        store = Path(temp)
        env = dict(os.environ, TM_TEST_STORE=str(store))
        try:
            result = subprocess.run(command, env=env, check=False)
            data = collect(store, output)
            if result.returncode and data["status"] == "pass":
                message = "common runner exited nonzero after reporting pass"
                raise RunnerError(message)
            publish(data)
            return result.returncode or (0 if data["status"] == "pass" else 1)
        except (OSError, ValueError, RuntimeError, subprocess.SubprocessError) as error:
            sys.stderr.write(f"local test failed: {error}\n")
            summary.update(
                status="error",
                exit_code=1,
                finished_at=datetime.now(UTC).isoformat(),
                duration_seconds=round(time.monotonic() - started, 3),
            )
            publish(summary)
            return 1


if __name__ == "__main__":
    raise SystemExit(main())
