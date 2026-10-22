#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
if ! command -v emcc >/dev/null 2>&1; then
  sdk="${EMSDK:-$HOME/.local/emsdk}"
  if [[ ! -f "$sdk/emsdk_env.sh" ]]; then
    echo 'Emscripten required: put emcc on PATH, or set EMSDK to an installed SDK.' >&2
    exit 1
  fi
  source "$sdk/emsdk_env.sh" >/dev/null 2>&1
fi
mkdir -p dist/assets
emcc src/fluid.c src/swimmer.c src/view.c -std=c11 -O2 -Wall -Wextra -Wpedantic -Werror -lm \
  --no-entry -sMODULARIZE=1 -sEXPORT_NAME=createSwimmer -sENVIRONMENT=web \
  -sFILESYSTEM=0 -sINITIAL_MEMORY=16777216 \
  '-sEXPORTED_FUNCTIONS=["_view_init","_view_step","_view_state","_view_field"]' \
  '-sEXPORTED_RUNTIME_METHODS=["HEAPF32"]' -o dist/physics.js
cp web/index.html web/style.css web/app.js dist/
cp assets/hero-fish.png assets/pond.jpg dist/assets/
python3 - <<'PY'
from pathlib import Path
files = sorted(p for p in Path('dist').rglob('*') if p.is_file())
size = sum(p.stat().st_size for p in files)
for p in files:
    print(f'{p}: {p.stat().st_size:,} B')
print(f'Browser distribution: {size:,} B ({len(files)} files)')
if size > 10_000_000:
    raise SystemExit('Browser distribution exceeds 10 MB')
PY
