#include "render.h"

#include <math.h>
#include <stdio.h>

#define MAX_N 128

static const int NEG[7] = {236, 17, 18, 19, 20, 27, 39};
static const int POS[7] = {236, 52, 88, 124, 160, 196, 208};
#define BODY_COLOR 231
#define NOSE_COLOR 226

int render_rows(const Fluid *f)
{
    return f->n / 2;
}

static int colour_for(float w, float ref)
{
    float s = tanhf(w / ref);
    int level = (int)(fabsf(s) * 6.0f + 0.5f);
    if (level > 6) level = 6;
    return s < 0.0f ? NEG[level] : POS[level];
}

size_t render_frame(const Fluid *f, const Swimmer *s, char *buf, size_t cap)
{
    int n = f->n;
    if (n > MAX_N) return 0;
    static int px[MAX_N][MAX_N];
    float ref = fluid_max_abs_vorticity(f);
    if (ref < 1.0f) ref = 1.0f;
    ref *= 0.5f;

    for (int j = 0; j < n; j++)
        for (int i = 0; i < n; i++)
            px[j][i] = colour_for(fluid_vorticity_at(f, i + 1, j + 1), ref);

    int bx = (int)(s->x * (float)n), by = (int)(s->y * (float)n);
    int rr = (int)(s->radius * (float)n + 0.5f);
    if (rr < 1) rr = 1;
    for (int j = by - rr; j <= by + rr; j++)
        for (int i = bx - rr; i <= bx + rr; i++)
            if (i >= 0 && i < n && j >= 0 && j < n &&
                (i - bx) * (i - bx) + (j - by) * (j - by) <= rr * rr)
                px[j][i] = BODY_COLOR;
    int nx = bx + (int)lroundf(cosf(s->heading) * (float)(rr + 1));
    int ny = by + (int)lroundf(sinf(s->heading) * (float)(rr + 1));
    if (nx >= 0 && nx < n && ny >= 0 && ny < n) px[ny][nx] = NOSE_COLOR;

    size_t used = 0;
    /* Row j = 0 is the bottom of the water, so print from the top down. */
    for (int j = n - 1; j >= 1; j -= 2) {
        for (int i = 0; i < n; i++) {
            int written = snprintf(buf + used, cap - used, "\x1b[38;5;%dm\x1b[48;5;%dm\xe2\x96\x80",
                                   px[j][i], px[j - 1][i]);
            if (written < 0 || (size_t)written >= cap - used) return 0;
            used += (size_t)written;
        }
        int written = snprintf(buf + used, cap - used, "\x1b[0m\n");
        if (written < 0 || (size_t)written >= cap - used) return 0;
        used += (size_t)written;
    }
    return used;
}

int render_field_svg(const Fluid *f, const Swimmer *s, const char *path)
{
    int n = f->n, cell = 10, size = n * cell;
    FILE *fp = fopen(path, "w");
    if (!fp) return -1;
    float ref = fluid_max_abs_vorticity(f);
    if (ref < 1.0f) ref = 1.0f;
    ref *= 0.5f;
    fprintf(fp, "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 %d %d\" width=\"%d\" height=\"%d\" shape-rendering=\"crispEdges\">\n",
            size, size, size, size);
    for (int j = 1; j <= n; j++)
        for (int i = 1; i <= n; i++) {
            float t = tanhf(fluid_vorticity_at(f, i, j) / ref);
            float a = fabsf(t);
            int r = (int)(255.0f - (t < 0.0f ? 200.0f * a : 40.0f * a));
            int g = (int)(255.0f - 190.0f * a);
            int b = (int)(255.0f - (t < 0.0f ? 40.0f * a : 200.0f * a));
            fprintf(fp, "<rect x=\"%d\" y=\"%d\" width=\"%d\" height=\"%d\" fill=\"rgb(%d,%d,%d)\"/>\n",
                    (i - 1) * cell, (n - j) * cell, cell, cell, r, g, b);
        }
    float cx = s->x * (float)size, cy = (1.0f - s->y) * (float)size, rad = s->radius * (float)size;
    fprintf(fp, "<circle cx=\"%.1f\" cy=\"%.1f\" r=\"%.1f\" fill=\"#222\"/>\n", cx, cy, rad);
    fprintf(fp, "<line x1=\"%.1f\" y1=\"%.1f\" x2=\"%.1f\" y2=\"%.1f\" stroke=\"#ffd400\" stroke-width=\"4\"/>\n", cx, cy,
            cx + cosf(s->heading) * rad * 1.8f, cy - sinf(s->heading) * rad * 1.8f);
    fprintf(fp, "</svg>\n");
    return fclose(fp) == 0 ? 0 : -1;
}
