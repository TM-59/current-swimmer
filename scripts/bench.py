# Copyright 2026 TM-59
"""Compare the unchanged 18 s route with the published C reference data."""

import csv
import math
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
CHARTS = ("wake.svg", "speed.svg", "energy.svg", "vorticity.svg", "reynolds.svg", "track.svg")


class BenchError(AssertionError):
    """The route or its charts differ from the published reference."""


def read_rows(path: Path) -> list[dict[str, str]]:
    """Return the rows of a CSV file with a header line."""
    with path.open() as f:
        return list(csv.DictReader(f))


def check(actual: list[dict[str, str]], expected: list[dict[str, str]], folder: Path) -> None:
    """Raise BenchError unless every value matches within cross-compiler rounding."""
    if not len(actual) == len(expected) > 0:
        message = f"{len(actual)} rows against {len(expected)} in docs/run.csv"
        raise BenchError(message)
    # Allow only cross-compiler floating-point rounding, not an altered trajectory.
    for a, e in zip(actual, expected, strict=True):
        for key in e:
            if not math.isclose(float(a[key]), float(e[key]), rel_tol=3e-4, abs_tol=1e-5):
                message = f"{key}: {a} against {e}"
                raise BenchError(message)
    for name in CHARTS:
        if (folder / name).stat().st_size <= 0:
            raise BenchError(name)


def main() -> None:
    """Run the terminal program headless into a scratch folder and compare."""
    with tempfile.TemporaryDirectory(prefix="current-swimmer-bench-") as tmp:
        program = ROOT / "current-swimmer"
        subprocess.run([str(program), "--headless", tmp], check=True)
        actual = read_rows(Path(tmp) / "run.csv")
        check(actual, read_rows(ROOT / "docs/run.csv"), Path(tmp))
        peak = max(float(r["speed"]) for r in actual)
        reynolds = max(float(r["reynolds"]) for r in actual)
        sys.stdout.write(
            f"bench: {len(actual)} samples match the original route; peak speed={peak:.5f}, "
            f"peak Re={reynolds:.2f}\n"
        )


if __name__ == "__main__":
    main()
