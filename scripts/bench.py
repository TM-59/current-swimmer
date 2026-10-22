"""Compare the unchanged 18 s route with the published C reference data."""
import csv
import math
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parent.parent
with tempfile.TemporaryDirectory(prefix='current-swimmer-bench-') as tmp:
    subprocess.run([str(root / 'current-swimmer'), '--headless', tmp], check=True)
    with (Path(tmp) / 'run.csv').open() as f:
        actual = list(csv.DictReader(f))
    with (root / 'docs/run.csv').open() as f:
        expected = list(csv.DictReader(f))
    assert len(actual) == len(expected) > 0
    # Allow only cross-compiler floating-point rounding, not an altered trajectory.
    for a, e in zip(actual, expected, strict=True):
        for key in e:
            assert math.isclose(float(a[key]), float(e[key]), rel_tol=3e-4, abs_tol=1e-5), (key, a, e)
    for name in ('wake.svg', 'speed.svg', 'energy.svg', 'vorticity.svg', 'reynolds.svg', 'track.svg'):
        assert (Path(tmp) / name).stat().st_size > 0, name
    peak = max(float(r['speed']) for r in actual)
    print(f'bench: {len(actual)} samples match the original route; peak speed={peak:.5f}, peak Re={max(float(r["reynolds"]) for r in actual):.2f}')
