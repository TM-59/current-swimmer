#ifndef FLUID_H
#define FLUID_H

/* 2D incompressible flow on the unit square, solved with Stam's "Stable Fluids".
 * Grid cells are 1..n in each direction; index 0 and n+1 are wall ghost cells.
 * Velocities are in domain-lengths per second. */
typedef struct {
    int n;
    float visc;      /* kinematic viscosity [domain^2 / s] */
    float *u, *v;    /* velocity components */
    float *u0, *v0;  /* scratch */
    float *p, *div;  /* pressure and divergence scratch */
} Fluid;

int fluid_init(Fluid *f, int n, float visc);
void fluid_free(Fluid *f);
void fluid_step(Fluid *f, float dt);

/* Add momentum (fx, fy) per unit time as a Gaussian blob centred at (x, y). */
void fluid_add_force(Fluid *f, float x, float y, float fx, float fy, float radius, float dt);
/* Bilinear velocity sample at (x, y) in [0, 1]. */
void fluid_sample(const Fluid *f, float x, float y, float *u, float *v);

float fluid_kinetic_energy(const Fluid *f);
float fluid_max_divergence(const Fluid *f);
float fluid_vorticity_at(const Fluid *f, int i, int j);
float fluid_max_abs_vorticity(const Fluid *f);
float fluid_total_circulation(const Fluid *f);

/* Reynolds number U * L / nu for body length L. */
float reynolds_number(float speed, float length, float visc);

#endif
