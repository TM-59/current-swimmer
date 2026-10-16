#ifndef RENDER_H
#define RENDER_H

#include <stddef.h>

#include "fluid.h"
#include "swimmer.h"

/* Number of text rows needed to draw an n x n field with half-block characters. */
int render_rows(const Fluid *f);
/* Draw vorticity as a blue/red field with the swimmer on top (ANSI 256 colours,
 * two grid rows per text row). Returns bytes written, or 0 if cap is too small. */
size_t render_frame(const Fluid *f, const Swimmer *s, char *buf, size_t cap);

/* Write the vorticity field and the swimmer as a self-contained SVG. Returns 0 on success. */
int render_field_svg(const Fluid *f, const Swimmer *s, const char *path);

#endif
