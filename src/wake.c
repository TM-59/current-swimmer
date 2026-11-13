#include "wake.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

/* Staggered (MAC) layout, stored with one ghost layer where a stencil needs it:
 *   u(i, j) at (i h, (j + 1/2) h),  i = 0..nx, j = -1..ny   -> U_(w, i, j)
 *   v(i, j) at ((i + 1/2) h, j h),  i = -1..nx, j = 0..ny   -> V_(w, i, j)
 *   p(i, j) at ((i + 1/2) h, (j + 1/2) h), i, j = -1..n     -> P_(w, i, j) */
#define U_(w, i, j) ((size_t)(i) + (size_t)((w)->nx + 1) * (size_t)((j) + 1))
#define V_(w, i, j) ((size_t)((i) + 1) + (size_t)((w)->nx + 2) * (size_t)(j))
#define P_(w, i, j) ((size_t)((i) + 1) + (size_t)((w)->nx + 2) * (size_t)((j) + 1))

#define INFLOW 1.0f
#define DIFFUSION_SWEEPS 12
#define PRESSURE_TOL 1e-4f   /* largest |div u| left by the projection [U / D] */
#define PRESSURE_MAX 4000
#define SOR 1.85f

static size_t u_count(const Wake *w) { return (size_t)(w->nx + 1) * (size_t)(w->ny + 2); }
static size_t v_count(const Wake *w) { return (size_t)(w->nx + 2) * (size_t)(w->ny + 1); }
static size_t p_count(const Wake *w) { return (size_t)(w->nx + 2) * (size_t)(w->ny + 2); }

/* Ghost values: inflow u = U, v = 0 at the left; zero gradient at the right; free slip (no flow through,
 * no shear) at the top and bottom. The outflow face u(nx, j) is left to the caller: it is copied from the
 * last interior face before a projection, and set by the projection itself. */
static void bc_u(const Wake *w, float *u)
{
    for (int j = 0; j < w->ny; j++) u[U_(w, 0, j)] = INFLOW;
    for (int i = 0; i <= w->nx; i++) {
        u[U_(w, i, -1)] = u[U_(w, i, 0)];
        u[U_(w, i, w->ny)] = u[U_(w, i, w->ny - 1)];
    }
}

static void bc_v(const Wake *w, float *v)
{
    for (int i = -1; i <= w->nx; i++) {
        v[V_(w, i, 0)] = 0.0f;
        v[V_(w, i, w->ny)] = 0.0f;
    }
    for (int j = 0; j <= w->ny; j++) {
        v[V_(w, -1, j)] = -v[V_(w, 0, j)];
        v[V_(w, w->nx, j)] = v[V_(w, w->nx - 1, j)];
    }
}

static float clampf(float x, float lo, float hi) { return x < lo ? lo : x > hi ? hi : x; }

/* Bilinear interpolation of a staggered component. (gx, gy) are grid coordinates of that component,
 * clamped to its stored range [x0, x1] x [y0, y1]; lo / hi receive the four corner extremes when not NULL. */
static float interp(const float *a, int stride, int ox, int oy, float gx, float gy,
                    int x0, int x1, int y0, int y1, float *lo, float *hi)
{
    gx = clampf(gx, (float)x0, (float)x1);
    gy = clampf(gy, (float)y0, (float)y1);
    int i = (int)floorf(gx), j = (int)floorf(gy);
    if (i >= x1) i = x1 - 1;
    if (j >= y1) j = y1 - 1;
    float s = gx - (float)i, t = gy - (float)j;
    size_t k = (size_t)(i + ox) + (size_t)stride * (size_t)(j + oy);
    float a00 = a[k], a10 = a[k + 1], a01 = a[k + (size_t)stride], a11 = a[k + (size_t)stride + 1];
    if (lo) {
        *lo = fminf(fminf(a00, a10), fminf(a01, a11));
        *hi = fmaxf(fmaxf(a00, a10), fmaxf(a01, a11));
    }
    return (1.0f - t) * ((1.0f - s) * a00 + s * a10) + t * ((1.0f - s) * a01 + s * a11);
}

static float sample_u(const Wake *w, const float *u, float x, float y, float *lo, float *hi)
{
    return interp(u, w->nx + 1, 0, 1, x / w->h, y / w->h - 0.5f, 0, w->nx, -1, w->ny, lo, hi);
}

static float sample_v(const Wake *w, const float *v, float x, float y, float *lo, float *hi)
{
    return interp(v, w->nx + 2, 1, 0, x / w->h - 0.5f, y / w->h, -1, w->nx, 0, w->ny, lo, hi);
}

void wake_sample(const Wake *w, float x, float y, float *u, float *v)
{
    *u = sample_u(w, w->u, x, y, NULL, NULL);
    *v = sample_v(w, w->v, x, y, NULL, NULL);
}

/* Departure point of the particle now at (x, y), traced dir * dt through the carrier field (cu, cv)
 * with a midpoint (RK2) step. */
static void trace(const Wake *w, const float *cu, const float *cv, float x, float y, float dt, float *px, float *py)
{
    float u = sample_u(w, cu, x, y, NULL, NULL), v = sample_v(w, cv, x, y, NULL, NULL);
    float mx = x + 0.5f * dt * u, my = y + 0.5f * dt * v;
    *px = x + dt * sample_u(w, cu, mx, my, NULL, NULL);
    *py = y + dt * sample_v(w, cv, mx, my, NULL, NULL);
}

/* MacCormack advection of both components (Selle et al. 2008): a semi-Lagrangian step back, one forward
 * from its result, half the round-trip error added, and the value clamped to the four old values around
 * the departure point so that no new extremum can appear. */
static void advect(Wake *w, float dt)
{
    int nx = w->nx, ny = w->ny;
    float h = w->h;
    float *u0 = w->u0, *v0 = w->v0, *uh = w->u1, *vh = w->v1, *ut = w->u2, *vt = w->v2;
    memcpy(u0, w->u, u_count(w) * sizeof(float));
    memcpy(v0, w->v, v_count(w) * sizeof(float));
    memcpy(uh, u0, u_count(w) * sizeof(float));
    memcpy(vh, v0, v_count(w) * sizeof(float));
    for (int j = 0; j < ny; j++)
        for (int i = 1; i < nx; i++) {
            float px, py;
            trace(w, u0, v0, (float)i * h, ((float)j + 0.5f) * h, -dt, &px, &py);
            uh[U_(w, i, j)] = sample_u(w, u0, px, py, NULL, NULL);
        }
    for (int j = 1; j < ny; j++)
        for (int i = 0; i < nx; i++) {
            float px, py;
            trace(w, u0, v0, ((float)i + 0.5f) * h, (float)j * h, -dt, &px, &py);
            vh[V_(w, i, j)] = sample_v(w, v0, px, py, NULL, NULL);
        }
    for (int j = 0; j < ny; j++) uh[U_(w, nx, j)] = uh[U_(w, nx - 1, j)];
    bc_u(w, uh);
    bc_v(w, vh);
    if (w->first_order) {
        memcpy(w->u, uh, u_count(w) * sizeof(float));
        memcpy(w->v, vh, v_count(w) * sizeof(float));
        return;
    }
    memcpy(ut, uh, u_count(w) * sizeof(float));
    memcpy(vt, vh, v_count(w) * sizeof(float));
    for (int j = 0; j < ny; j++)
        for (int i = 1; i < nx; i++) {
            float x = (float)i * h, y = ((float)j + 0.5f) * h, px, py, qx, qy, lo, hi;
            trace(w, u0, v0, x, y, -dt, &px, &py);
            sample_u(w, u0, px, py, &lo, &hi);
            trace(w, u0, v0, x, y, dt, &qx, &qy);
            float back = sample_u(w, uh, qx, qy, NULL, NULL);
            size_t k = U_(w, i, j);
            ut[k] = clampf(uh[k] + 0.5f * (u0[k] - back), lo, hi);
        }
    for (int j = 1; j < ny; j++)
        for (int i = 0; i < nx; i++) {
            float x = ((float)i + 0.5f) * h, y = (float)j * h, px, py, qx, qy, lo, hi;
            trace(w, u0, v0, x, y, -dt, &px, &py);
            sample_v(w, v0, px, py, &lo, &hi);
            trace(w, u0, v0, x, y, dt, &qx, &qy);
            float back = sample_v(w, vh, qx, qy, NULL, NULL);
            size_t k = V_(w, i, j);
            vt[k] = clampf(vh[k] + 0.5f * (v0[k] - back), lo, hi);
        }
    memcpy(w->u, ut, u_count(w) * sizeof(float));
    memcpy(w->v, vt, v_count(w) * sizeof(float));
    for (int j = 0; j < ny; j++) w->u[U_(w, nx, j)] = w->u[U_(w, nx - 1, j)];
    bc_u(w, w->u);
    bc_v(w, w->v);
}

/* Backward Euler for du/dt = nu lap(u) on the faces, Gauss-Seidel from the current field. */
static void diffuse(Wake *w, float dt)
{
    int nx = w->nx, ny = w->ny;
    float a = w->visc * dt / (w->h * w->h), c = 1.0f + 4.0f * a;
    memcpy(w->u0, w->u, u_count(w) * sizeof(float));
    memcpy(w->v0, w->v, v_count(w) * sizeof(float));
    for (int k = 0; k < DIFFUSION_SWEEPS; k++) {
        for (int j = 0; j < ny; j++)
            for (int i = 1; i < nx; i++)
                w->u[U_(w, i, j)] = (w->u0[U_(w, i, j)] + a * (w->u[U_(w, i - 1, j)] + w->u[U_(w, i + 1, j)] +
                                                                w->u[U_(w, i, j - 1)] + w->u[U_(w, i, j + 1)])) / c;
        for (int j = 0; j < ny; j++) w->u[U_(w, nx, j)] = w->u[U_(w, nx - 1, j)];
        bc_u(w, w->u);
        for (int j = 1; j < ny; j++)
            for (int i = 0; i < nx; i++)
                w->v[V_(w, i, j)] = (w->v0[V_(w, i, j)] + a * (w->v[V_(w, i - 1, j)] + w->v[V_(w, i + 1, j)] +
                                                                w->v[V_(w, i, j - 1)] + w->v[V_(w, i, j + 1)])) / c;
        bc_v(w, w->v);
    }
}

/* Removes the solid fraction of every face velocity; the momentum removed is an impulse the water gives
 * to the cylinder (density 1, per unit span), added to (*ix, *iy). */
static void penalize(Wake *w, double *ix, double *iy)
{
    double fx = 0.0, fy = 0.0, area = (double)w->h * w->h;
    for (size_t k = 0; k < u_count(w); k++)
        if (w->chiu[k] > 0.0f) {
            fx += (double)w->chiu[k] * w->u[k];
            w->u[k] *= 1.0f - w->chiu[k];
        }
    for (size_t k = 0; k < v_count(w); k++)
        if (w->chiv[k] > 0.0f) {
            fy += (double)w->chiv[k] * w->v[k];
            w->v[k] *= 1.0f - w->chiv[k];
        }
    *ix += fx * area;
    *iy += fy * area;
}

static float cell_div(const Wake *w, int i, int j)
{
    return (w->u[U_(w, i + 1, j)] - w->u[U_(w, i, j)] + w->v[V_(w, i, j + 1)] - w->v[V_(w, i, j)]) / w->h;
}

/* Exact discrete projection: solve div(beta grad phi) = div u with phi = 0 on the outflow face and no
 * correction through the inflow face or the walls (SOR to a residual tolerance, warm-started from the
 * last step), then u -= beta grad phi. Afterwards the discrete divergence of every cell is below
 * PRESSURE_TOL. beta = 1 - chi is the open fraction of a face (1 everywhere when aware = 0, the M2 scheme):
 * a solid face receives no pressure correction, so no water is pushed through the cylinder, and the
 * impulse chi grad phi it withheld is the pressure impulse on the cylinder, added to (*ix, *iy). */
static void project(Wake *w, int aware, double *ix, double *iy)
{
    int nx = w->nx, ny = w->ny;
    float h2 = w->h * w->h, s = aware ? 1.0f : 0.0f;
    const float *cu = w->chiu, *cv = w->chiv;
    for (int j = 0; j < ny; j++)
        for (int i = 0; i < nx; i++) w->div[P_(w, i, j)] = cell_div(w, i, j) * h2;
    float *p = w->p;
    int sweep = 0;
    for (; sweep < PRESSURE_MAX; sweep++) {
        for (int j = 0; j < ny; j++)
            for (int i = 0; i < nx; i++) {
                float sum = 0.0f, k = 0.0f, b;
                if (i > 0) { b = 1.0f - s * cu[U_(w, i, j)]; sum += b * p[P_(w, i - 1, j)]; k += b; }
                if (i < nx - 1) { b = 1.0f - s * cu[U_(w, i + 1, j)]; sum += b * p[P_(w, i + 1, j)]; k += b; } else k += 2.0f;
                if (j > 0) { b = 1.0f - s * cv[V_(w, i, j)]; sum += b * p[P_(w, i, j - 1)]; k += b; }
                if (j < ny - 1) { b = 1.0f - s * cv[V_(w, i, j + 1)]; sum += b * p[P_(w, i, j + 1)]; k += b; }
                size_t c = P_(w, i, j);
                if (k < 1e-6f) { p[c] = 0.0f; continue; } /* a cell sealed inside the solid */
                float target = (sum - w->div[c]) / k;
                p[c] += SOR * (target - p[c]);
            }
        if (sweep % 8 == 7) {
            float worst = 0.0f;
            for (int j = 0; j < ny; j++)
                for (int i = 0; i < nx; i++) {
                    float r = -w->div[P_(w, i, j)];
                    float pc = p[P_(w, i, j)];
                    if (i > 0) r += (1.0f - s * cu[U_(w, i, j)]) * (p[P_(w, i - 1, j)] - pc);
                    if (i < nx - 1) r += (1.0f - s * cu[U_(w, i + 1, j)]) * (p[P_(w, i + 1, j)] - pc); else r += -2.0f * pc;
                    if (j > 0) r += (1.0f - s * cv[V_(w, i, j)]) * (p[P_(w, i, j - 1)] - pc);
                    if (j < ny - 1) r += (1.0f - s * cv[V_(w, i, j + 1)]) * (p[P_(w, i, j + 1)] - pc);
                    if (fabsf(r) > worst) worst = fabsf(r);
                }
            if (worst / h2 < PRESSURE_TOL) { sweep++; break; }
        }
    }
    w->sweeps = sweep;
    double fx = 0.0, fy = 0.0;
    for (int j = 0; j < ny; j++) {
        for (int i = 1; i < nx; i++) {
            float g = (p[P_(w, i, j)] - p[P_(w, i - 1, j)]) / w->h, chi = s * cu[U_(w, i, j)];
            w->u[U_(w, i, j)] -= (1.0f - chi) * g;
            fx -= (double)chi * g;
        }
        w->u[U_(w, nx, j)] -= (-2.0f * p[P_(w, nx - 1, j)]) / w->h;
    }
    for (int j = 1; j < ny; j++)
        for (int i = 0; i < nx; i++) {
            float g = (p[P_(w, i, j)] - p[P_(w, i, j - 1)]) / w->h, chi = s * cv[V_(w, i, j)];
            w->v[V_(w, i, j)] -= (1.0f - chi) * g;
            fy -= (double)chi * g;
        }
    if (ix) *ix += fx * h2;
    if (iy) *iy += fy * h2;
    bc_u(w, w->u);
    bc_v(w, w->v);
}

/* Solid fraction of a face at distance d from the centre: 1 deep inside, 0 outside, linear over one cell
 * centred on the radius plus `shift` cells. */
static float solid(const Wake *w, float shift, float x, float y)
{
    float d = sqrtf((x - w->cx) * (x - w->cx) + (y - w->cy) * (y - w->cy));
    return clampf((w->r - d) / w->h + 0.5f + shift, 0.0f, 1.0f);
}

int wake_init(Wake *w, int cells_per_diameter, float reynolds)
{
    return wake_init_channel(w, cells_per_diameter, reynolds, WAKE_LY);
}

int wake_init_channel(Wake *w, int cells_per_diameter, float reynolds, int height)
{
    WakeSetup setup = {cells_per_diameter, reynolds, WAKE_LX, height, WAKE_CX, 0.0f};
    return wake_init_setup(w, &setup);
}

int wake_init_setup(Wake *w, const WakeSetup *setup)
{
    memset(w, 0, sizeof *w);
    if (setup->cells < 2 || setup->reynolds <= 0.0f || setup->height < 2 || setup->upstream < 1 ||
        setup->length < setup->upstream + 2) return -1;
    w->n = setup->cells;
    w->length = setup->length;
    w->nx = setup->length * setup->cells;
    w->ny = setup->height * setup->cells;
    w->h = 1.0f / (float)setup->cells;
    w->visc = 1.0f / setup->reynolds;
    w->cx = (float)setup->upstream;
    w->cy = 0.5f * (float)setup->height;
    w->r = 0.5f;
    float **uu[] = {&w->u, &w->u0, &w->u1, &w->u2, &w->chiu};
    float **vv[] = {&w->v, &w->v0, &w->v1, &w->v2, &w->chiv};
    for (size_t k = 0; k < 5; k++) {
        *uu[k] = calloc(u_count(w), sizeof(float));
        *vv[k] = calloc(v_count(w), sizeof(float));
        if (!*uu[k] || !*vv[k]) { wake_free(w); return -1; }
    }
    w->p = calloc(p_count(w), sizeof(float));
    w->div = calloc(p_count(w), sizeof(float));
    if (!w->p || !w->div) { wake_free(w); return -1; }
    float h = w->h;
    for (int j = -1; j <= w->ny; j++)
        for (int i = 0; i <= w->nx; i++) {
            size_t k = U_(w, i, j);
            w->chiu[k] = solid(w, setup->wall_shift, (float)i * h, ((float)j + 0.5f) * h);
            w->u[k] = INFLOW * (1.0f - w->chiu[k]);
        }
    /* The nudge: a weak cross-flow blob behind the cylinder. Without it a mirror-symmetric start would
     * keep the two shear layers symmetric for longer before round-off breaks the symmetry. */
    for (int j = 0; j <= w->ny; j++)
        for (int i = -1; i <= w->nx; i++) {
            size_t k = V_(w, i, j);
            float x = ((float)i + 0.5f) * h - (w->cx + 1.5f), y = (float)j * h - w->cy;
            w->chiv[k] = solid(w, setup->wall_shift, ((float)i + 0.5f) * h, (float)j * h);
            w->v[k] = 0.3f * expf(-(x * x + y * y) / 0.5f) * (1.0f - w->chiv[k]);
        }
    bc_u(w, w->u);
    bc_v(w, w->v);
    project(w, 1, NULL, NULL);
    return 0;
}

void wake_free(Wake *w)
{
    free(w->u); free(w->v); free(w->u0); free(w->v0); free(w->u1); free(w->v1);
    free(w->u2); free(w->v2); free(w->chiu); free(w->chiv); free(w->p); free(w->div);
    memset(w, 0, sizeof *w);
}

float wake_dt(const Wake *w) { return 0.4f * w->h / INFLOW; }

void wake_step(Wake *w, float dt)
{
    advect(w, dt);
    diffuse(w, dt);
    for (int j = 0; j < w->ny; j++) w->u[U_(w, w->nx, j)] = w->u[U_(w, w->nx - 1, j)];
    double ix = 0.0, iy = 0.0;
    penalize(w, &ix, &iy);
    project(w, !w->split_penalty, &ix, &iy);
    w->fx = ix / dt;
    w->fy = iy / dt;
    w->dt = dt;
    w->t += dt;
}

float wake_drag_coefficient(const Wake *w) { return (float)(2.0 * w->fx / (INFLOW * INFLOW)); }
float wake_lift_coefficient(const Wake *w) { return (float)(2.0 * w->fy / (INFLOW * INFLOW)); }

static float corner_vorticity(const Wake *w, int i, int j)
{
    return ((w->v[V_(w, i, j)] - w->v[V_(w, i - 1, j)]) - (w->u[U_(w, i, j)] - w->u[U_(w, i, j - 1)])) / w->h;
}

float wake_vorticity_at(const Wake *w, int i, int j)
{
    return 0.25f * (corner_vorticity(w, i, j) + corner_vorticity(w, i + 1, j) +
                    corner_vorticity(w, i, j + 1) + corner_vorticity(w, i + 1, j + 1));
}

float wake_max_divergence(const Wake *w)
{
    float worst = 0.0f;
    for (int j = 0; j < w->ny; j++)
        for (int i = 0; i < w->nx; i++) worst = fmaxf(worst, fabsf(cell_div(w, i, j)));
    return worst;
}

float wake_control_volume_drag(const Wake *w, float half)
{
    int n = w->n, i0 = (int)lroundf((w->cx - half) * (float)n), i1 = (int)lroundf((w->cx + half) * (float)n);
    int j0 = (int)lroundf((w->cy - half) * (float)n), j1 = (int)lroundf((w->cy + half) * (float)n);
    if (i0 < 1 || i1 > w->nx - 1 || j0 < 1 || j1 > w->ny - 1 || w->dt <= 0.0f) return 0.0f;
    double h = w->h, nu = w->visc, f = 0.0;
    /* Left (+) and right (-) faces: u is stored there; momentum flux u u, pressure, normal viscous stress. */
    for (int j = j0; j < j1; j++)
        for (int side = 0; side < 2; side++) {
            int i = side ? i1 : i0;
            double u = w->u[U_(w, i, j)], p = 0.5 * (w->p[P_(w, i - 1, j)] + w->p[P_(w, i, j)]) / w->dt;
            double dudx = (w->u[U_(w, i + 1, j)] - w->u[U_(w, i - 1, j)]) / (2.0 * h);
            f += (side ? -1.0 : 1.0) * (u * u + p - nu * dudx) * h;
        }
    /* Bottom (+) and top (-) faces, at the cell corners (trapezoid rule): flux u v and the shear stress. */
    for (int i = i0; i <= i1; i++)
        for (int side = 0; side < 2; side++) {
            int j = side ? j1 : j0;
            double u = 0.5 * (w->u[U_(w, i, j - 1)] + w->u[U_(w, i, j)]);
            double v = 0.5 * (w->v[V_(w, i - 1, j)] + w->v[V_(w, i, j)]);
            double dudy = (w->u[U_(w, i, j)] - w->u[U_(w, i, j - 1)]) / h;
            f += (side ? -1.0 : 1.0) * ((i == i0 || i == i1) ? 0.5 : 1.0) * (u * v - nu * dudy) * h;
        }
    return (float)(2.0 * f / (INFLOW * INFLOW));
}

float wake_recirculation_length(const Wake *w)
{
    float rear = w->cx + w->r, step = 0.25f * w->h, end = (float)w->length;
    int reversed = 0;
    for (float x = rear + w->h; x < end; x += step) {
        float u = sample_u(w, w->u, x, w->cy, NULL, NULL);
        if (u < 0.0f) reversed = 1;
        else if (reversed) return x - rear;
        else if (x > rear + 2.0f * w->h) return 0.0f;
    }
    return 0.0f;
}

float strouhal_number(float frequency, float diameter, float speed)
{
    return speed > 0.0f ? frequency * diameter / speed : 0.0f;
}

WakeStats wake_stats(const float *cl, const float *cd, int count, int from, float dt)
{
    WakeStats s = {0};
    if (from < 0) from = 0;
    if (count - from < 2) return s;
    double mean = 0.0;
    float lo = cl[from], hi = cl[from], dlo = cd[from], dhi = cd[from];
    for (int k = from; k < count; k++) {
        mean += cl[k];
        lo = fminf(lo, cl[k]); hi = fmaxf(hi, cl[k]);
        dlo = fminf(dlo, cd[k]); dhi = fmaxf(dhi, cd[k]);
    }
    mean /= (double)(count - from);
    s.cl_amp = 0.5f * (hi - lo);
    s.cd_amp = 0.5f * (dhi - dlo);
    double first = -1.0, last = -1.0;
    int up = 0, kfirst = from, klast = count;
    for (int k = from + 1; k < count; k++) {
        double a = cl[k - 1] - mean, b = cl[k] - mean;
        if (a < 0.0 && b >= 0.0) {
            double t = ((double)(k - 1) + a / (a - b)) * dt;
            if (up == 0) { first = t; kfirst = k; }
            last = t; klast = k;
            up++;
        }
    }
    double sum = 0.0;
    int n = 0;
    if (up >= 3 && s.cl_amp >= 0.01f) {
        s.periods = up - 1;
        s.frequency = (float)((double)s.periods / (last - first));
        s.strouhal = strouhal_number(s.frequency, 1.0f, INFLOW);
        for (int k = kfirst; k < klast; k++, n++) sum += cd[k];
    } else {
        for (int k = from; k < count; k++, n++) sum += cd[k];
    }
    s.cd_mean = (float)(sum / n);
    return s;
}

int wake_run(int cells_per_diameter, float reynolds, float t_end, float dt, WakeStats *stats, float *lr,
             float *cl, float *cd, int cap, int *count)
{
    Wake w;
    if (wake_init(&w, cells_per_diameter, reynolds)) return -1;
    if (dt <= 0.0f) dt = wake_dt(&w);
    int steps = (int)(t_end / dt + 0.5f);
    float *l = malloc((size_t)steps * sizeof(float)), *d = malloc((size_t)steps * sizeof(float));
    if (!l || !d) { free(l); free(d); wake_free(&w); return -1; }
    for (int k = 0; k < steps; k++) {
        wake_step(&w, dt);
        l[k] = wake_lift_coefficient(&w);
        d[k] = wake_drag_coefficient(&w);
        if (cl && k < cap) cl[k] = l[k];
        if (cd && k < cap) cd[k] = d[k];
    }
    if (stats) *stats = wake_stats(l, d, steps, steps / 2, dt);
    if (lr) *lr = wake_recirculation_length(&w);
    if (count) *count = steps < cap ? steps : cap;
    free(l); free(d);
    wake_free(&w);
    return 0;
}
