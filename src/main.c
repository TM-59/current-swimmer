#define _POSIX_C_SOURCE 200809L

#include <math.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#include "fluid.h"
#include "gif.h"
#include "render.h"
#include "svg.h"
#include "swimmer.h"

#define GRID 48
#define VISC 0.0004f
#define DT 0.02f

static volatile sig_atomic_t stop_flag;
static struct termios saved_tty;
static int tty_saved;

static void restore_tty(void)
{
    if (tty_saved) {
        tcsetattr(STDIN_FILENO, TCSANOW, &saved_tty);
        fputs("\x1b[0m\x1b[?25h\n", stdout);
        fflush(stdout);
        tty_saved = 0;
    }
}

static void on_signal(int sig)
{
    (void)sig;
    stop_flag = 1;
}

/* Deterministic route used by --headless and --snapshot: beat the tail, coast, and trace two opposite loops. */
static void scripted(float t, Controls *c)
{
    float phase = fmodf(t, 2.4f);
    c->stroke = phase < 0.5f;
    c->turn = t < 7.0f ? 0.5f : -0.5f;
}

typedef struct {
    float *t, *speed, *energy, *vort, *re, *x, *y;
    int count;
} Log;

static int run_headless(const char *dir)
{
    int steps = (int)(18.0f / DT);
    Fluid f;
    Swimmer s;
    Log log = {0};
    if (fluid_init(&f, GRID, VISC)) return 1;
    swimmer_init(&s, 0.3f, 0.5f, 0.0f);
    float **cols[] = {&log.t, &log.speed, &log.energy, &log.vort, &log.re, &log.x, &log.y};
    for (size_t i = 0; i < sizeof cols / sizeof *cols; i++) {
        *cols[i] = calloc((size_t)steps, sizeof(float));
        if (!*cols[i]) return 1;
    }

    char path[512];
    snprintf(path, sizeof path, "%s/run.csv", dir);
    FILE *csv = fopen(path, "w");
    if (!csv) { fprintf(stderr, "cannot write %s\n", path); return 1; }
    fputs("t,x,y,speed,thrust,drag,fluid_energy,max_vorticity,reynolds\n", csv);

    for (int k = 0; k < steps; k++) {
        Controls c;
        float t = (float)k * DT;
        scripted(t, &c);
        swimmer_step(&s, &f, &c, DT);
        fluid_step(&f, DT);
        float speed = swimmer_speed(&s);
        float re = reynolds_number(speed, 2.0f * s.radius, f.visc);
        log.t[k] = t; log.speed[k] = speed; log.energy[k] = fluid_kinetic_energy(&f);
        log.vort[k] = fluid_max_abs_vorticity(&f); log.re[k] = re; log.x[k] = s.x; log.y[k] = s.y;
        fprintf(csv, "%.2f,%.4f,%.4f,%.5f,%.5f,%.5f,%.6g,%.4f,%.2f\n", t, s.x, s.y, speed, s.thrust, s.drag,
                log.energy[k], log.vort[k], re);
    }
    fclose(csv);
    log.count = steps;

    struct { const char *file, *title, *ylabel, *name, *color; const float *y; } plots[] = {
        {"speed.svg", "Swimmer speed", "speed [domain/s]", "speed", "#1f77b4", log.speed},
        {"vorticity.svg", "Peak vorticity in the wake", "max |vorticity| [1/s]", "max |w|", "#d62728", log.vort},
        {"reynolds.svg", "Reynolds number of the swimmer", "Re = U * 2r / nu", "Re", "#2ca02c", log.re},
        {"energy.svg", "Kinetic energy of the water", "energy per cell", "fluid energy", "#9467bd", log.energy},
    };
    for (size_t i = 0; i < sizeof plots / sizeof *plots; i++) {
        snprintf(path, sizeof path, "%s/%s", dir, plots[i].file);
        Series one = {plots[i].name, plots[i].y, plots[i].color};
        if (svg_plot(path, plots[i].title, "time [s]", plots[i].ylabel, log.t, steps, &one, 1)) return 1;
    }
    snprintf(path, sizeof path, "%s/track.svg", dir);
    Series track = {"path", log.y, "#ff7f0e"};
    if (svg_plot(path, "Swimmer track (top view)", "x", "y", log.x, steps, &track, 1)) return 1;

    snprintf(path, sizeof path, "%s/wake.svg", dir);
    if (render_field_svg(&f, &s, path)) return 1;

    float peak = 0.0f;
    for (int k = 0; k < steps; k++) if (log.speed[k] > peak) peak = log.speed[k];
    printf("steps=%d peak_speed=%.3f peak_re=%.0f final_energy=%.3g max_div=%.3g\n", steps, peak,
           reynolds_number(peak, 2.0f * s.radius, f.visc), log.energy[steps - 1], fluid_max_divergence(&f));
    for (size_t i = 0; i < sizeof cols / sizeof *cols; i++) free(*cols[i]);
    fluid_free(&f);
    return 0;
}

static int run_snapshot(void)
{
    Fluid f;
    Swimmer s;
    if (fluid_init(&f, GRID, VISC)) return 1;
    swimmer_init(&s, 0.3f, 0.5f, 0.0f);
    for (int k = 0; k < (int)(6.0f / DT); k++) {
        Controls c;
        scripted((float)k * DT, &c);
        swimmer_step(&s, &f, &c, DT);
        fluid_step(&f, DT);
    }
    size_t cap = (size_t)GRID * (size_t)GRID * 48u;
    char *buf = malloc(cap);
    if (!buf) return 1;
    size_t used = render_frame(&f, &s, buf, cap);
    fwrite(buf, 1, used, stdout);
    free(buf);
    fluid_free(&f);
    return used ? 0 : 1;
}

/* Palette: 0 still water, 1..6 clockwise (blue), 7..12 counter-clockwise (red), 13 body, 14 nose. */
static int vorticity_index(float w, float ref)
{
    float t = tanhf(w / ref);
    int level = (int)(fabsf(t) * 6.0f + 0.5f);
    if (level > 6) level = 6;
    return level == 0 ? 0 : (t < 0.0f ? level : 6 + level);
}

/* Pixels of one frame: n x n cells, `scale` pixels per cell, row 0 of the fluid at the bottom. */
static void paint_frame(const Fluid *f, const Swimmer *s, int scale, unsigned char *px)
{
    int n = f->n, size = n * scale;
    float ref = fluid_max_abs_vorticity(f);
    if (ref < 1.0f) ref = 1.0f;
    ref *= 0.5f;
    for (int j = 0; j < n; j++)
        for (int i = 0; i < n; i++) {
            unsigned char c = (unsigned char)vorticity_index(fluid_vorticity_at(f, i + 1, j + 1), ref);
            for (int dy = 0; dy < scale; dy++)
                memset(px + ((n - 1 - j) * scale + dy) * size + i * scale, c, (size_t)scale);
        }
    float bx = s->x * (float)size, by = (1.0f - s->y) * (float)size, rad = s->radius * (float)size;
    float nx = bx + cosf(s->heading) * (rad + (float)scale), ny = by - sinf(s->heading) * (rad + (float)scale);
    for (int y = 0; y < size; y++)
        for (int x = 0; x < size; x++) {
            float dx = (float)x + 0.5f - bx, dy = (float)y + 0.5f - by;
            float ex = (float)x + 0.5f - nx, ey = (float)y + 0.5f - ny;
            if (dx * dx + dy * dy <= rad * rad) px[y * size + x] = 13;
            else if (ex * ex + ey * ey <= (float)(scale * scale)) px[y * size + x] = 14;
        }
}

/* Scripted swim (the --headless route) as an animated GIF of the vorticity field. */
static int run_gif(const char *path)
{
    enum { SCALE = 8, STRIDE = 9 };
    Fluid f;
    Swimmer s;
    if (fluid_init(&f, GRID, VISC)) return 1;
    swimmer_init(&s, 0.3f, 0.5f, 0.0f);
    unsigned char pal[GIF_COLOURS][3] = {{12, 16, 30},   {30, 50, 100},  {40, 75, 150},  {55, 105, 195},
                                         {80, 140, 230}, {120, 175, 250}, {170, 210, 255}, {100, 40, 40},
                                         {150, 50, 45},  {200, 70, 55},  {235, 105, 60},  {250, 150, 85},
                                         {255, 200, 130}, {245, 245, 245}, {255, 220, 40}};
    int size = GRID * SCALE;
    unsigned char *px = malloc((size_t)size * (size_t)size);
    Gif *g = px ? gif_open(path, size, size, pal, 6) : NULL;
    if (!g) { free(px); fluid_free(&f); return 1; }
    int steps = (int)(18.0f / DT), rc = 0;
    for (int k = 0; k <= steps && !rc; k++) {
        if (k % STRIDE == 0 || k == steps) {
            paint_frame(&f, &s, SCALE, px);
            rc = gif_frame(g, px, k == steps ? 250 : 0);
        }
        Controls c;
        scripted((float)k * DT, &c);
        swimmer_step(&s, &f, &c, DT);
        fluid_step(&f, DT);
    }
    rc |= gif_close(g);
    printf("final position (%.3f, %.3f), speed %.3f\n", s.x, s.y, swimmer_speed(&s));
    free(px);
    fluid_free(&f);
    return rc;
}

static int run_interactive(void)
{
    if (!isatty(STDIN_FILENO)) {
        fputs("interactive mode needs a terminal; try --snapshot or --headless\n", stderr);
        return 1;
    }
    tcgetattr(STDIN_FILENO, &saved_tty);
    tty_saved = 1;
    atexit(restore_tty);
    struct termios raw = saved_tty;
    raw.c_lflag &= (tcflag_t)~(ICANON | ECHO);
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 0;
    tcsetattr(STDIN_FILENO, TCSANOW, &raw);
    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);

    Fluid f;
    Swimmer s;
    if (fluid_init(&f, GRID, VISC)) return 1;
    swimmer_init(&s, 0.3f, 0.5f, 0.0f);
    size_t cap = (size_t)GRID * (size_t)GRID * 48u;
    char *buf = malloc(cap);
    if (!buf) return 1;

    float stroke_left = 0.0f, turn_left = 0.0f, turn_dir = 0.0f, t = 0.0f;
    int paused = 0;
    fputs("\x1b[2J\x1b[?25l", stdout);
    while (!stop_flag) {
        char key;
        while (read(STDIN_FILENO, &key, 1) == 1) {
            if (key == 'q') stop_flag = 1;
            else if (key == 'p') paused = !paused;
            else if (key == ' ' || key == 'w') stroke_left = 0.2f;
            else if (key == 'a') { turn_dir = 1.0f; turn_left = 0.2f; }
            else if (key == 'd') { turn_dir = -1.0f; turn_left = 0.2f; }
        }
        if (!paused) {
            Controls c = {turn_left > 0.0f ? turn_dir : 0.0f, stroke_left > 0.0f};
            swimmer_step(&s, &f, &c, DT);
            fluid_step(&f, DT);
            stroke_left -= DT;
            turn_left -= DT;
            t += DT;
        }
        size_t used = render_frame(&f, &s, buf, cap);
        fputs("\x1b[H", stdout);
        fwrite(buf, 1, used, stdout);
        printf("t=%5.1fs speed=%.2f Re=%4.0f  [space/w] stroke  [a/d] turn  [p] pause  [q] quit\x1b[K\n", t,
               swimmer_speed(&s), reynolds_number(swimmer_speed(&s), 2.0f * s.radius, f.visc));
        fflush(stdout);
        struct timespec nap = {0, 20 * 1000 * 1000};
        nanosleep(&nap, NULL);
    }
    free(buf);
    fluid_free(&f);
    return 0;
}

int main(int argc, char **argv)
{
    if (argc >= 2 && !strcmp(argv[1], "--headless")) return run_headless(argc >= 3 ? argv[2] : "docs");
    if (argc >= 2 && !strcmp(argv[1], "--snapshot")) return run_snapshot();
    if (argc >= 2 && !strcmp(argv[1], "--gif")) return run_gif(argc >= 3 ? argv[2] : "demo.gif");
    if (argc == 1) return run_interactive();
    fprintf(stderr, "usage: %s [--headless [outdir] | --snapshot | --gif [file]]\n", argv[0]);
    return 2;
}
