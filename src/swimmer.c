#include "swimmer.h"

#include <math.h>

#define PI_F 3.14159265358979f
#define STROKE_THRUST 0.006f
#define TURN_RATE 2.5f
#define DRAG_QUAD 0.03f   /* 0.5 * Cd * rho * diameter for Cd = 1, diameter = 0.06 */
#define DRAG_LIN 0.002f

void swimmer_init(Swimmer *s, float x, float y, float heading)
{
    s->x = x;
    s->y = y;
    s->vx = s->vy = 0.0f;
    s->heading = heading;
    s->radius = 0.03f;
    s->mass = PI_F * s->radius * s->radius;
    s->thrust = s->drag = 0.0f;
}

float swimmer_speed(const Swimmer *s)
{
    return hypotf(s->vx, s->vy);
}

static void push_fluid(Fluid *f, float x, float y, float fx, float fy, float sigma, float dt)
{
    float area = 2.0f * PI_F * sigma * sigma;
    fluid_add_force(f, x, y, fx / area, fy / area, sigma, dt);
}

void swimmer_step(Swimmer *s, Fluid *f, const Controls *c, float dt)
{
    float uf, vf;
    fluid_sample(f, s->x, s->y, &uf, &vf);
    float wx = s->vx - uf, wy = s->vy - vf;
    float w = hypotf(wx, wy);
    float k = DRAG_LIN + DRAG_QUAD * w;
    float dragx = -k * wx, dragy = -k * wy;
    float ch = cosf(s->heading), sh = sinf(s->heading);
    float thrust = c->stroke ? STROKE_THRUST : 0.0f;
    float fx = thrust * ch + dragx, fy = thrust * sh + dragy;
    float meff = 2.0f * s->mass;

    s->vx += fx / meff * dt;
    s->vy += fy / meff * dt;
    s->x += s->vx * dt;
    s->y += s->vy * dt;
    s->heading += c->turn * TURN_RATE * dt;
    s->thrust = thrust;
    s->drag = k * w;

    float lo = s->radius, hi = 1.0f - s->radius;
    if (s->x < lo) { s->x = lo; if (s->vx < 0) s->vx = 0; }
    if (s->x > hi) { s->x = hi; if (s->vx > 0) s->vx = 0; }
    if (s->y < lo) { s->y = lo; if (s->vy < 0) s->vy = 0; }
    if (s->y > hi) { s->y = hi; if (s->vy > 0) s->vy = 0; }

    /* Newton's third law: the tail jet leaves behind the body, drag drags the water along. */
    push_fluid(f, s->x - ch * s->radius * 1.5f, s->y - sh * s->radius * 1.5f,
               -thrust * ch, -thrust * sh, 0.04f, dt);
    push_fluid(f, s->x, s->y, -dragx, -dragy, s->radius, dt);
}
