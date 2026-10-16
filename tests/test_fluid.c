#include <math.h>
#include <stdio.h>
#include <string.h>

#include "../src/fluid.h"
#include "../src/render.h"
#include "../src/swimmer.h"

static int failures;

#define CHECK(cond, ...)                                      \
    do {                                                      \
        if (!(cond)) {                                        \
            failures++;                                       \
            printf("FAIL %s:%d: ", __FILE__, __LINE__);       \
            printf(__VA_ARGS__);                              \
            printf("\n");                                     \
        }                                                     \
    } while (0)

/* Taylor-Green-like vortex that is divergence-free and satisfies free-slip walls. */
static void seed_vortex(Fluid *f)
{
    const float pi = 3.14159265f;
    for (int j = 1; j <= f->n; j++)
        for (int i = 1; i <= f->n; i++) {
            float x = ((float)i - 0.5f) / (float)f->n, y = ((float)j - 0.5f) / (float)f->n;
            f->u[i + (f->n + 2) * j] = sinf(pi * x) * cosf(pi * y);
            f->v[i + (f->n + 2) * j] = -cosf(pi * x) * sinf(pi * y);
        }
}

static void test_still_water_stays_still(void)
{
    Fluid f;
    fluid_init(&f, 32, 0.001f);
    for (int k = 0; k < 20; k++) fluid_step(&f, 0.02f);
    CHECK(fluid_kinetic_energy(&f) == 0.0f, "still water gained energy");
    fluid_free(&f);
}

static void test_projection_keeps_flow_divergence_free(void)
{
    Fluid f;
    fluid_init(&f, 32, 0.001f);
    fluid_add_force(&f, 0.5f, 0.5f, 40.0f, 10.0f, 0.06f, 0.1f);
    float before = fluid_max_divergence(&f);
    fluid_step(&f, 0.02f);
    float after = fluid_max_divergence(&f);
    CHECK(after < 0.1f * before, "divergence %.4f -> %.4f", before, after);
    fluid_free(&f);
}

static void test_unforced_flow_only_loses_energy(void)
{
    Fluid f;
    fluid_init(&f, 32, 0.001f);
    seed_vortex(&f);
    float e = fluid_kinetic_energy(&f);
    CHECK(e > 0.01f, "seed vortex is empty");
    for (int k = 0; k < 50; k++) {
        fluid_step(&f, 0.02f);
        float now = fluid_kinetic_energy(&f);
        CHECK(now <= e * 1.001f, "energy grew at step %d: %.6f -> %.6f", k, e, now);
        e = now;
    }
    fluid_free(&f);
}

static void test_symmetry_of_mirrored_forcing(void)
{
    Fluid f;
    fluid_init(&f, 32, 0.001f);
    fluid_add_force(&f, 0.5f, 0.5f, 30.0f, 0.0f, 0.06f, 0.1f);
    for (int k = 0; k < 10; k++) fluid_step(&f, 0.02f);
    float a, b, c, d;
    fluid_sample(&f, 0.5f, 0.3f, &a, &b);
    fluid_sample(&f, 0.5f, 0.7f, &c, &d);
    CHECK(fabsf(a - c) < 1e-2f && fabsf(b + d) < 1e-2f, "mirror symmetry broken (%.4f vs %.4f, %.4f vs %.4f)", a, c, b, d);
    fluid_free(&f);
}

static void test_reynolds_number(void)
{
    CHECK(fabsf(reynolds_number(0.5f, 0.06f, 0.0004f) - 75.0f) < 1e-3f, "Re of the default swimmer should be 75");
    CHECK(isinf(reynolds_number(1.0f, 1.0f, 0.0f)), "inviscid Re should be infinite");
}

static void test_stroke_pushes_swimmer_forward_and_water_backward(void)
{
    Fluid f;
    Swimmer s;
    Controls c = {0.0f, 1};
    fluid_init(&f, 48, 0.0004f);
    swimmer_init(&s, 0.4f, 0.5f, 0.0f);
    for (int k = 0; k < 15; k++) {
        swimmer_step(&s, &f, &c, 0.02f);
        fluid_step(&f, 0.02f);
    }
    CHECK(s.x > 0.4f && s.vx > 0.0f, "swimmer did not move along its heading (x=%.4f vx=%.4f)", s.x, s.vx);
    CHECK(fabsf(s.y - 0.5f) < 1e-3f, "swimmer drifted sideways (y=%.5f)", s.y);
    float u, v;
    fluid_sample(&f, s.x - 0.1f, s.y, &u, &v);
    CHECK(u < 0.0f, "jet behind the body should flow backwards (u=%.4f)", u);
    fluid_free(&f);
}

static void test_swimmer_stays_inside_the_pool(void)
{
    Fluid f;
    Swimmer s;
    Controls c = {0.0f, 1};
    fluid_init(&f, 32, 0.0004f);
    swimmer_init(&s, 0.5f, 0.5f, 0.0f);
    for (int k = 0; k < 400; k++) {
        swimmer_step(&s, &f, &c, 0.02f);
        fluid_step(&f, 0.02f);
    }
    CHECK(s.x <= 1.0f - s.radius + 1e-5f, "swimmer left the pool (x=%.4f)", s.x);
    fluid_free(&f);
}

static void test_render_draws_every_cell(void)
{
    Fluid f;
    Swimmer s;
    fluid_init(&f, 48, 0.0004f);
    swimmer_init(&s, 0.5f, 0.5f, 0.0f);
    static char buf[48 * 48 * 48];
    size_t used = render_frame(&f, &s, buf, sizeof buf);
    int blocks = 0;
    for (char *p = buf; (p = strstr(p, "\xe2\x96\x80")) != NULL; p += 3) blocks++;
    CHECK(used > 0 && blocks == 48 * 24, "expected %d half blocks, got %d", 48 * 24, blocks);
    CHECK(render_frame(&f, &s, buf, 16) == 0, "tiny buffer must be refused");
    fluid_free(&f);
}

int main(void)
{
    test_still_water_stays_still();
    test_projection_keeps_flow_divergence_free();
    test_unforced_flow_only_loses_energy();
    test_symmetry_of_mirrored_forcing();
    test_reynolds_number();
    test_stroke_pushes_swimmer_forward_and_water_backward();
    test_swimmer_stays_inside_the_pool();
    test_render_draws_every_cell();
    if (failures) {
        printf("%d check(s) failed\n", failures);
        return 1;
    }
    puts("all tests passed");
    return 0;
}
