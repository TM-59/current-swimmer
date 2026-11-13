#include "render.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

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

/* Quantised colour of tanh(w / ref): 21 levels, red counter-clockwise, blue clockwise. */
static int wake_level(float w, float ref)
{
    int q = (int)lroundf(tanhf(w / ref) * 10.0f);
    return q < -10 ? -10 : q > 10 ? 10 : q;
}

int render_wake_svg(const Wake *w, const char *path, const char *title)
{
    int cell = 5, ml = 46, mt = 34, mb = 40, pw = w->nx * cell, ph = w->ny * cell;
    int width = ml + pw + 14, height = mt + ph + mb;
    FILE *fp = fopen(path, "w");
    if (!fp) return -1;
    float ref = 1.5f;  /* fixed scale in U / D so that runs can be compared */
    fprintf(fp, "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 %d %d\" width=\"%d\" height=\"%d\" "
                "font-family=\"sans-serif\" font-size=\"12\">\n", width, height, width, height);
    fprintf(fp, "<rect width=\"%d\" height=\"%d\" fill=\"#ffffff\"/>\n", width, height);
    fprintf(fp, "<text x=\"%d\" y=\"22\" font-size=\"15\" font-weight=\"bold\" fill=\"#222\">%s</text>\n", ml, title);
    fprintf(fp, "<g shape-rendering=\"crispEdges\">\n");
    /* One rectangle per run of equal colour along a row keeps the file small. */
    for (int j = w->ny - 1; j >= 0; j--) {
        int i = 0;
        while (i < w->nx) {
            int q = wake_level(wake_vorticity_at(w, i, j), ref), k = i + 1;
            while (k < w->nx && wake_level(wake_vorticity_at(w, k, j), ref) == q) k++;
            float a = (float)abs(q) / 10.0f;
            int r = (int)(255.0f - (q < 0 ? 200.0f * a : 40.0f * a));
            int g = (int)(255.0f - 190.0f * a);
            int b = (int)(255.0f - (q < 0 ? 40.0f * a : 200.0f * a));
            if (q != 0)
                fprintf(fp, "<rect x=\"%d\" y=\"%d\" width=\"%d\" height=\"%d\" fill=\"rgb(%d,%d,%d)\"/>\n",
                        ml + i * cell, mt + (w->ny - 1 - j) * cell, (k - i) * cell, cell, r, g, b);
            i = k;
        }
    }
    fprintf(fp, "</g>\n<rect x=\"%d\" y=\"%d\" width=\"%d\" height=\"%d\" fill=\"none\" stroke=\"#888\"/>\n", ml, mt, pw, ph);
    float px = (float)ml + w->cx / w->h * (float)cell, py = (float)mt + (float)ph - w->cy / w->h * (float)cell;
    fprintf(fp, "<circle cx=\"%.1f\" cy=\"%.1f\" r=\"%.1f\" fill=\"#333\"/>\n", px, py, w->r / w->h * (float)cell);
    for (int x = 0; x <= w->nx / w->n; x += 2)
        fprintf(fp, "<text x=\"%d\" y=\"%d\" text-anchor=\"middle\" fill=\"#444\">%d</text>\n", ml + x * w->n * cell, mt + ph + 16, x);
    for (int y = 0; y <= w->ny / w->n; y += 2)
        fprintf(fp, "<text x=\"%d\" y=\"%d\" text-anchor=\"end\" fill=\"#444\">%d</text>\n", ml - 6, mt + ph - y * w->n * cell + 4, y);
    fprintf(fp, "<text x=\"%d\" y=\"%d\" text-anchor=\"middle\" fill=\"#222\">x [D]   (red: counter-clockwise, blue: clockwise; colour = tanh(&#969; / 1.5 U/D))</text>\n",
            ml + pw / 2, height - 6);
    fprintf(fp, "<text transform=\"translate(14 %d) rotate(-90)\" text-anchor=\"middle\" fill=\"#222\">y [D]</text>\n", mt + ph / 2);
    fprintf(fp, "</svg>\n");
    return fclose(fp) == 0 ? 0 : -1;
}
