# current-swimmer

- C11 + libm is the physics source of truth: `src/fluid.c` and `src/swimmer.c`.
- Keep native terminal, headless CSV/SVG, and GIF modes working. The optional browser
  view compiles the same C physics with Emscripten; do not reimplement it in JavaScript.
- `make test`, `make bench`, `make web`, `make smoke` are the local checks.
  Browser automation stays local. CI runs `docker build .` (native C tests in the
  Dockerfile's Debian 13 toolchain) on pull requests only.
- Without a host compiler, run checks as
  `docker run --rm --user $(id -u):$(id -g) -v $PWD:/src -w /src <debian13+build-essential image> make test`.
- The theory handbook is `docs/theory/`. `make theory-figs` regenerates its figures and
  `figures/values.md` from `tests/theory_figs.c`; `make theory-check` fails on a statement
  without a figure, a broken link, a vanished function/test/value, or a public function
  missing from the tables. Add the page section, figure and table row in the same PR as a
  new law or rule. The README's "20 statements" must match the table.
- No persistence or external network services. Do not add saving as part of art work.
- Artwork must not imply that the disc collision/drag model has become a fish-body
  simulation. Label the physical diameter and use domain units for measurements.

## Story layer: Upstream (M5)

- `src/journey.c` is a game layer over the unchanged fluid and disc model. Story beats open
  at fixed progress points and change only the sluice jet strength and the stamina budget,
  each behind one `JE_*` bit so the bench can remove it. Scene words live in `web/app.js`.
- `make story` rewrites `docs/narrative/{play-order,ablation,tension}.json` (about 320 journeys,
  `STORY_JOBS` processes; CPU heavy). Never edit those files by hand.
  `make test` re-runs 8 rows and requires them verbatim; `make story-check` compares all rows.
  Generate them with the Debian 13 toolchain so the bytes match CI.
- Bench seeds are stirred with `sub_seed` (splitmix64) per row and run; `story_bench --check`
  fails on any duplicate key. Do not derive seeds by XOR or sums.
- Heavy scenes (Silt stops in the reeds, the last board comes out) and the ending show no
  cleared banner. `beats.json` emotion values are self-scored until an independent review.

## Original artwork exception (M4)

`assets/hero-fish.png` and `assets/pond.jpg` are original artwork made with Codex's
built-in image_gen under subscription authentication. They are used directly in
the browser renderer. No existing characters, third-party images, audio, fonts,
model API keys, or paid generation services are permitted. Keep prompts, adoption
decisions, provenance, and visual limitations in `docs/art-direction.md`. Keep the
whole browser distribution below 10 MB. Inspect every generated image and every
README screenshot. Failed image loads must leave a readable physics view.
