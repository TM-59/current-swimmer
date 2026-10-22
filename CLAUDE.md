# current-swimmer

- C11 + libm is the physics source of truth: `src/fluid.c` and `src/swimmer.c`.
- Keep native terminal, headless CSV/SVG, and GIF modes working. The optional browser
  view compiles the same C physics with Emscripten; do not reimplement it in JavaScript.
- `make test`, `make bench`, `make web`, `make smoke` are the local checks.
  Browser automation stays local. CI runs native C unit tests only, on pull requests.
- No persistence or external network services. Do not add saving as part of art work.
- Artwork must not imply that the disc collision/drag model has become a fish-body
  simulation. Label the physical diameter and use domain units for measurements.

## Original artwork exception (M4)

`assets/hero-fish.png` and `assets/pond.jpg` are original artwork made with Codex's
built-in image_gen under subscription authentication. They are used directly in
the browser renderer. No existing characters, third-party images, audio, fonts,
model API keys, or paid generation services are permitted. Keep prompts, adoption
decisions, provenance, and visual limitations in `docs/art-direction.md`. Keep the
whole browser distribution below 10 MB. Inspect every generated image and every
README screenshot. Failed image loads must leave a readable physics view.
