#include "fluid.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define IX(f, i, j) ((i) + ((f)->n + 2) * (j))

static void set_bnd(const Fluid *f, int b, float *x)
{
    int n = f->n;
    for (int i = 1; i <= n; i++) {
        x[IX(f, 0, i)] = b == 1 ? -x[IX(f, 1, i)] : x[IX(f, 1, i)];
        x[IX(f, n + 1, i)] = b == 1 ? -x[IX(f, n, i)] : x[IX(f, n, i)];
        x[IX(f, i, 0)] = b == 2 ? -x[IX(f, i, 1)] : x[IX(f, i, 1)];
        x[IX(f, i, n + 1)] = b == 2 ? -x[IX(f, i, n)] : x[IX(f, i, n)];
    }
    x[IX(f, 0, 0)] = 0.5f * (x[IX(f, 1, 0)] + x[IX(f, 0, 1)]);
    x[IX(f, 0, n + 1)] = 0.5f * (x[IX(f, 1, n + 1)] + x[IX(f, 0, n)]);
    x[IX(f, n + 1, 0)] = 0.5f * (x[IX(f, n, 0)] + x[IX(f, n + 1, 1)]);
    x[IX(f, n + 1, n + 1)] = 0.5f * (x[IX(f, n, n + 1)] + x[IX(f, n + 1, n)]);
}

static void lin_solve(const Fluid *f, int b, float *x, const float *x0, float a, float c)
{
    int n = f->n;
    for (int k = 0; k < 24; k++) {
        for (int j = 1; j <= n; j++)
            for (int i = 1; i <= n; i++)
                x[IX(f, i, j)] = (x0[IX(f, i, j)] +
                                  a * (x[IX(f, i - 1, j)] + x[IX(f, i + 1, j)] +
                                       x[IX(f, i, j - 1)] + x[IX(f, i, j + 1)])) / c;
        set_bnd(f, b, x);
    }
}

static void diffuse(const Fluid *f, int b, float *x, const float *x0, float dt)
{
    float a = dt * f->visc * (float)f->n * (float)f->n;
    lin_solve(f, b, x, x0, a, 1.0f + 4.0f * a);
}

static void advect(const Fluid *f, int b, float *d, const float *d0, const float *u, const float *v, float dt)
{
    int n = f->n;
    float dt0 = dt * (float)n;
    for (int j = 1; j <= n; j++) {
        for (int i = 1; i <= n; i++) {
            float x = (float)i - dt0 * u[IX(f, i, j)];
            float y = (float)j - dt0 * v[IX(f, i, j)];
            if (x < 0.5f) x = 0.5f;
            if (x > (float)n + 0.5f) x = (float)n + 0.5f;
            if (y < 0.5f) y = 0.5f;
            if (y > (float)n + 0.5f) y = (float)n + 0.5f;
            int i0 = (int)x, j0 = (int)y;
            float s1 = x - (float)i0, t1 = y - (float)j0;
            float s0 = 1.0f - s1, t0 = 1.0f - t1;
            d[IX(f, i, j)] = s0 * (t0 * d0[IX(f, i0, j0)] + t1 * d0[IX(f, i0, j0 + 1)]) +
                             s1 * (t0 * d0[IX(f, i0 + 1, j0)] + t1 * d0[IX(f, i0 + 1, j0 + 1)]);
        }
    }
    set_bnd(f, b, d);
}

static void project(Fluid *f)
{
    int n = f->n;
    float h = 1.0f / (float)n;
    for (int j = 1; j <= n; j++)
        for (int i = 1; i <= n; i++) {
            f->div[IX(f, i, j)] = -0.5f * h * (f->u[IX(f, i + 1, j)] - f->u[IX(f, i - 1, j)] +
                                                f->v[IX(f, i, j + 1)] - f->v[IX(f, i, j - 1)]);
            f->p[IX(f, i, j)] = 0.0f;
        }
    set_bnd(f, 0, f->div);
    set_bnd(f, 0, f->p);
    lin_solve(f, 0, f->p, f->div, 1.0f, 4.0f);
    for (int j = 1; j <= n; j++)
        for (int i = 1; i <= n; i++) {
            f->u[IX(f, i, j)] -= 0.5f * (f->p[IX(f, i + 1, j)] - f->p[IX(f, i - 1, j)]) / h;
            f->v[IX(f, i, j)] -= 0.5f * (f->p[IX(f, i, j + 1)] - f->p[IX(f, i, j - 1)]) / h;
        }
    set_bnd(f, 1, f->u);
    set_bnd(f, 2, f->v);
}

int fluid_init(Fluid *f, int n, float visc)
{
    size_t cells = (size_t)(n + 2) * (size_t)(n + 2);
    memset(f, 0, sizeof *f);
    f->n = n;
    f->visc = visc;
    f->u = calloc(cells, sizeof(float));
    f->v = calloc(cells, sizeof(float));
    f->u0 = calloc(cells, sizeof(float));
    f->v0 = calloc(cells, sizeof(float));
    f->p = calloc(cells, sizeof(float));
    f->div = calloc(cells, sizeof(float));
    if (!f->u || !f->v || !f->u0 || !f->v0 || !f->p || !f->div) {
        fluid_free(f);
        return -1;
    }
    return 0;
}

void fluid_free(Fluid *f)
{
    free(f->u); free(f->v); free(f->u0); free(f->v0); free(f->p); free(f->div);
    memset(f, 0, sizeof *f);
}

void fluid_step(Fluid *f, float dt)
{
    size_t bytes = (size_t)(f->n + 2) * (size_t)(f->n + 2) * sizeof(float);
    memcpy(f->u0, f->u, bytes);
    memcpy(f->v0, f->v, bytes);
    diffuse(f, 1, f->u, f->u0, dt);
    diffuse(f, 2, f->v, f->v0, dt);
    project(f);
    memcpy(f->u0, f->u, bytes);
    memcpy(f->v0, f->v, bytes);
    advect(f, 1, f->u, f->u0, f->u0, f->v0, dt);
    advect(f, 2, f->v, f->v0, f->u0, f->v0, dt);
    project(f);
}

void fluid_add_force(Fluid *f, float x, float y, float fx, float fy, float radius, float dt)
{
    int n = f->n;
    float h = 1.0f / (float)n;
    float inv = 1.0f / (2.0f * radius * radius);
    for (int j = 1; j <= n; j++)
        for (int i = 1; i <= n; i++) {
            float dx = ((float)i - 0.5f) * h - x, dy = ((float)j - 0.5f) * h - y;
            float w = expf(-(dx * dx + dy * dy) * inv);
            f->u[IX(f, i, j)] += fx * w * dt;
            f->v[IX(f, i, j)] += fy * w * dt;
        }
}

void fluid_sample(const Fluid *f, float x, float y, float *u, float *v)
{
    int n = f->n;
    float gx = x * (float)n + 0.5f, gy = y * (float)n + 0.5f;
    if (gx < 0.5f) gx = 0.5f;
    if (gx > (float)n + 0.5f) gx = (float)n + 0.5f;
    if (gy < 0.5f) gy = 0.5f;
    if (gy > (float)n + 0.5f) gy = (float)n + 0.5f;
    int i0 = (int)gx, j0 = (int)gy;
    float s1 = gx - (float)i0, t1 = gy - (float)j0;
    float s0 = 1.0f - s1, t0 = 1.0f - t1;
    *u = s0 * (t0 * f->u[IX(f, i0, j0)] + t1 * f->u[IX(f, i0, j0 + 1)]) +
         s1 * (t0 * f->u[IX(f, i0 + 1, j0)] + t1 * f->u[IX(f, i0 + 1, j0 + 1)]);
    *v = s0 * (t0 * f->v[IX(f, i0, j0)] + t1 * f->v[IX(f, i0, j0 + 1)]) +
         s1 * (t0 * f->v[IX(f, i0 + 1, j0)] + t1 * f->v[IX(f, i0 + 1, j0 + 1)]);
}

float fluid_kinetic_energy(const Fluid *f)
{
    double sum = 0.0;
    for (int j = 1; j <= f->n; j++)
        for (int i = 1; i <= f->n; i++) {
            double u = f->u[IX(f, i, j)], v = f->v[IX(f, i, j)];
            sum += 0.5 * (u * u + v * v);
        }
    return (float)(sum / ((double)f->n * (double)f->n));
}

float fluid_max_divergence(const Fluid *f)
{
    int n = f->n;
    float h = 1.0f / (float)n, worst = 0.0f;
    for (int j = 1; j <= n; j++)
        for (int i = 1; i <= n; i++) {
            float d = 0.5f / h * (f->u[IX(f, i + 1, j)] - f->u[IX(f, i - 1, j)] +
                                  f->v[IX(f, i, j + 1)] - f->v[IX(f, i, j - 1)]);
            if (fabsf(d) > worst) worst = fabsf(d);
        }
    return worst;
}

float fluid_vorticity_at(const Fluid *f, int i, int j)
{
    float h = 1.0f / (float)f->n;
    return 0.5f / h * ((f->v[IX(f, i + 1, j)] - f->v[IX(f, i - 1, j)]) -
                       (f->u[IX(f, i, j + 1)] - f->u[IX(f, i, j - 1)]));
}

float fluid_max_abs_vorticity(const Fluid *f)
{
    float worst = 0.0f;
    for (int j = 1; j <= f->n; j++)
        for (int i = 1; i <= f->n; i++) {
            float w = fabsf(fluid_vorticity_at(f, i, j));
            if (w > worst) worst = w;
        }
    return worst;
}

float fluid_total_circulation(const Fluid *f)
{
    double sum = 0.0;
    for (int j = 1; j <= f->n; j++)
        for (int i = 1; i <= f->n; i++) sum += fluid_vorticity_at(f, i, j);
    return (float)(sum / ((double)f->n * (double)f->n));
}

float reynolds_number(float speed, float length, float visc)
{
    return visc > 0.0f ? speed * length / visc : INFINITY;
}
