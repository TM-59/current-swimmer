/* Story bench: plays "Upstream" with a human-like swimmer and writes
 *   docs/narrative/play-order.json  scenes in the order they opened, per seed
 *   docs/narrative/ablation.json    story numbers on/off, one beat off, each choice path
 *   docs/narrative/tension.json     per-reach crises and sweeps, one row per run
 * Usage: story_bench <dir>          write all three files (make story)
 *        story_bench --check <dir>  re-run a few rows and require them verbatim in the files (make test)
 * Runs are spread over STORY_JOBS processes (default 8); every run is deterministic. */
#define _POSIX_C_SOURCE 200809L
#include "story_run.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

typedef struct { uint64_t key; unsigned effects; int route, stay; } Job;

static void run_all(const Job *jobs, int n, RunResult *out)
{
    const char *env = getenv("STORY_JOBS");
    int workers = env ? atoi(env) : 8;
    if (workers < 1) workers = 1;
    if (workers > n) workers = n;
    int fds[64][2];
    if (workers > 64) workers = 64;
    for (int w = 0; w < workers; w++) {
        if (pipe(fds[w])) { perror("pipe"); exit(1); }
        pid_t pid = fork();
        if (pid < 0) { perror("fork"); exit(1); }
        if (pid == 0) {
            close(fds[w][0]);
            for (int i = w; i < n; i += workers) {
                RunResult r;
                run_journey(jobs[i].key, jobs[i].effects, jobs[i].route, jobs[i].stay, &r);
                if (write(fds[w][1], &i, sizeof i) != (ssize_t)sizeof i ||
                    write(fds[w][1], &r, sizeof r) != (ssize_t)sizeof r) _exit(1);
            }
            _exit(0);
        }
        close(fds[w][1]);
    }
    for (int w = 0; w < workers; w++) {
        FILE *in = fdopen(fds[w][0], "rb");
        int i;
        while (fread(&i, sizeof i, 1, in) == 1) {
            if (i < 0 || i >= n || fread(&out[i], sizeof out[i], 1, in) != 1) { fputs("bad worker output\n", stderr); exit(1); }
        }
        fclose(in);
    }
    int status, failed = 0;
    while (wait(&status) > 0) failed |= !WIFEXITED(status) || WEXITSTATUS(status) != 0;
    if (failed) { fputs("a story worker failed\n", stderr); exit(1); }
}

/* ------------------------------------------------------------------ rows */
static const char *ending_id(int e) { return e == 0 ? "end-stay" : e == 1 ? "end-left" : "none"; }

static void play_row(char *buf, size_t cap, int i, const RunResult *r)
{
    int route, stay;
    play_choices(i, &route, &stay);
    size_t n = (size_t)snprintf(buf, cap, "{\"seed\": \"%d\", \"choices\": {\"culvert\": %d, \"reeds\": %d}, \"order\": [", i, route, stay);
    for (int k = 0; k < r->order_len; k++) n += (size_t)snprintf(buf + n, cap - n, "%s\"%s\"", k ? ", " : "", SCENE_ID[r->order[k]]);
    n += (size_t)snprintf(buf + n, cap - n, "], \"won\": %s, \"ending\": \"%s\", \"quiet\": [", r->completed ? "true" : "false", ending_id(r->ending));
    int first = 1;
    for (int k = 0; k < r->order_len; k++) {
        int s = r->order[k];
        if (journey_heavy(s) && r->quiet[s]) { n += (size_t)snprintf(buf + n, cap - n, "%s\"%s\"", first ? "" : ", ", SCENE_ID[s]); first = 0; }
    }
    snprintf(buf + n, cap - n, "]}");
}

static void ablation_row(char *buf, size_t cap, int i, const RunResult *r)
{
    snprintf(buf, cap, "{\"seed\": \"%d\", \"seconds\": %.1f, \"strain\": %.2f}", i, r->seconds, r->strain);
}

static void tension_row(char *buf, size_t cap, int i, int run, const RunResult *r)
{
    int route, stay;
    tension_choices(run, &route, &stay);
    size_t n = (size_t)snprintf(buf, cap, "{\"seed\": \"%d\", \"run\": %d, \"path\": {\"culvert\": \"%d\", \"reeds\": \"%d\"}, \"completed\": %s, \"crisis\": [",
                                i, run, route, stay, r->completed ? "true" : "false");
    for (int k = 0; k < J_REACHES; k++) n += (size_t)snprintf(buf + n, cap - n, "%s%d", k ? ", " : "", r->failure[k] > 0 ? r->failure[k] : r->crisis[k]);
    n += (size_t)snprintf(buf + n, cap - n, "], \"failure\": [");
    for (int k = 0; k < J_REACHES; k++) n += (size_t)snprintf(buf + n, cap - n, "%s%d", k ? ", " : "", r->failure[k]);
    snprintf(buf + n, cap - n, "]}");
}

/* ------------------------------------------------------------------ arms */
enum { ARM_ON, ARM_OFF, ARM_OUTLET, ARM_EDDY, ARM_TASTE, ARM_REEDS_LOSS, ARM_SURGE, ARM_DITCH, ARM_STAY, ARMS };
static const unsigned ARM_EFFECTS[ARMS] = {
    JE_ALL, 0u, JE_ALL & ~JE_PACE, JE_ALL & ~JE_EDDY, JE_ALL & ~JE_WARM, JE_ALL & ~JE_PARTING, JE_ALL & ~JE_SURGE, JE_ALL, JE_ALL};
static const int ARM_ROUTE[ARMS] = {DEFAULT_ROUTE, DEFAULT_ROUTE, DEFAULT_ROUTE, DEFAULT_ROUTE, DEFAULT_ROUTE, DEFAULT_ROUTE, DEFAULT_ROUTE, 1, DEFAULT_ROUTE};
static const int ARM_STAYS[ARMS] = {DEFAULT_STAY, DEFAULT_STAY, DEFAULT_STAY, DEFAULT_STAY, DEFAULT_STAY, DEFAULT_STAY, DEFAULT_STAY, DEFAULT_STAY, 0};

static Job ablation_job(int arm, int i) { return (Job){ablation_key(i), ARM_EFFECTS[arm], ARM_ROUTE[arm], ARM_STAYS[arm]}; }
static Job play_job(int i) { int r, s; play_choices(i, &r, &s); return (Job){play_key(i), JE_ALL, r, s}; }
static Job tension_job(int i, int run) { int r, s; tension_choices(run, &r, &s); return (Job){tension_key(i, run), JE_ALL, r, s}; }

/* ------------------------------------------------------------------ full output */
static FILE *open_out(const char *dir, const char *name)
{
    char path[512];
    snprintf(path, sizeof path, "%s/%s", dir, name);
    FILE *f = fopen(path, "w");
    if (!f) { fprintf(stderr, "cannot write %s\n", path); exit(1); }
    return f;
}

static void write_rows(FILE *f, const char *indent, const RunResult *res, int arm)
{
    char row[512];
    for (int i = 0; i < ABLATION_SEEDS; i++) {
        ablation_row(row, sizeof row, i, &res[arm * ABLATION_SEEDS + i]);
        fprintf(f, "%s%s%s\n", indent, row, i + 1 < ABLATION_SEEDS ? "," : "");
    }
}

static void write_arm_pair(FILE *f, const char *name, const RunResult *res, int off, int last)
{
    fprintf(f, "    \"%s\": {\n      \"on\": [\n", name);
    write_rows(f, "        ", res, ARM_ON);
    fprintf(f, "      ],\n      \"off\": [\n");
    write_rows(f, "        ", res, off);
    fprintf(f, "      ]\n    }%s\n", last ? "" : ",");
}

static const char *GEN = "tests/story_bench.c (make story)";
static const char *POLICY =
    "A swimmer who does not know the numbers: steers at the next mark with some wobble, beats the tail in bursts "
    "(0.45-0.9 s on, 0.1-0.4 s glide), rests when the stamina bar drops below its own threshold (2-20 %) and starts "
    "again at 40-80 %, glides while inside a hold ring. Thresholds come from the run's own stirred seed; wobble and rhythm "
    "from a stream stirred per reach. Choices follow the path field.";

static int write_all(const char *dir)
{
    int np = PLAY_SEEDS, na = ARMS * ABLATION_SEEDS, nt = TENSION_SEEDS * TENSION_RUNS;
    int n = np + na + nt;
    Job *jobs = calloc((size_t)n, sizeof *jobs);
    RunResult *res = calloc((size_t)n, sizeof *res);
    if (!jobs || !res) return 1;
    for (int i = 0; i < np; i++) jobs[i] = play_job(i);
    for (int a = 0; a < ARMS; a++)
        for (int i = 0; i < ABLATION_SEEDS; i++) jobs[np + a * ABLATION_SEEDS + i] = ablation_job(a, i);
    for (int i = 0; i < TENSION_SEEDS; i++)
        for (int k = 0; k < TENSION_RUNS; k++) jobs[np + na + i * TENSION_RUNS + k] = tension_job(i, k);
    run_all(jobs, n, res);
    char row[1024];

    FILE *f = open_out(dir, "play-order.json");
    fprintf(f, "{\n  \"version\": 1,\n  \"generator\": \"%s\",\n  \"note\": \"Scenes open at fixed progress points: a reach starts, a reach is cleared, "
               "the halfway mark of the last reach. culvert 0 = pipe, 1 = ditch; reeds 0 = stay with Silt, 1 = leave. quiet lists the heavy scenes "
               "shown with no cleared banner raised before the next stroke. Do not edit by hand.\",\n  \"seeds\": [\n", GEN);
    for (int i = 0; i < np; i++) { play_row(row, sizeof row, i, &res[i]); fprintf(f, "    %s%s\n", row, i + 1 < np ? "," : ""); }
    fprintf(f, "  ]\n}\n");
    fclose(f);

    const RunResult *ab = res + np;
    f = open_out(dir, "ablation.json");
    fprintf(f, "{\n  \"version\": 1,\n  \"generator\": \"%s\",\n  \"note\": \"Same %d seeds in every arm. on = all story numbers, pipe and leave Silt; "
               "off = scenes still open but every story number is neutral; beats = only that scene's numbers removed; choices = one choice fixed per path, "
               "the other at its default. Do not edit by hand.\",\n", GEN, ABLATION_SEEDS);
    fprintf(f, "  \"metrics\": {\n"
               "    \"seconds\": {\"better\": \"lower\", \"kind\": \"outcome\", \"felt\": \"screen\", \"label\": \"seconds swum, shown on the clock; every one is a played step (holds at the weir and in the reeds included, no flat waits)\"},\n"
               "    \"strain\": {\"better\": \"lower\", \"kind\": \"outcome\", \"felt\": \"screen\", \"label\": \"seconds with the stamina bar in the red (below 20 %%); strokes do nothing once it is empty\"}\n"
               "  },\n  \"effects\": {\n    \"on\": [\n");
    write_rows(f, "      ", ab, ARM_ON);
    fprintf(f, "    ],\n    \"off\": [\n");
    write_rows(f, "      ", ab, ARM_OFF);
    fprintf(f, "    ]\n  },\n  \"beats\": {\n");
    write_arm_pair(f, "outlet", ab, ARM_OUTLET, 0);
    write_arm_pair(f, "eddy", ab, ARM_EDDY, 0);
    write_arm_pair(f, "taste", ab, ARM_TASTE, 0);
    write_arm_pair(f, "reeds", ab, ARM_REEDS_LOSS, 0);
    write_arm_pair(f, "surge", ab, ARM_SURGE, 1);
    fprintf(f, "  },\n  \"choices\": {\n    \"culvert\": {\"paths\": {\n      \"0\": [\n");
    write_rows(f, "        ", ab, ARM_ON);
    fprintf(f, "      ],\n      \"1\": [\n");
    write_rows(f, "        ", ab, ARM_DITCH);
    fprintf(f, "      ]\n    }},\n    \"reeds\": {\"paths\": {\n      \"0\": [\n");
    write_rows(f, "        ", ab, ARM_STAY);
    fprintf(f, "      ],\n      \"1\": [\n");
    write_rows(f, "        ", ab, ARM_ON);
    fprintf(f, "      ]\n    }}\n  }\n}\n");
    fclose(f);

    const RunResult *tn = res + np + na;
    f = open_out(dir, "tension.json");
    fprintf(f, "{\n  \"version\": 1,\n  \"generator\": \"%s\",\n  \"policy\": \"%s\",\n", GEN, POLICY);
    fprintf(f, "  \"crisis\": \"the stamina bar below 12 %% at any moment in the reach, or swept back (a failure counts as a crisis)\",\n"
               "  \"failure\": \"swept back: the current takes more than 0.2 of the ground won in the reach\",\n"
               "  \"segments\": [\"outlet pool\", \"culvert\", \"eddy stone to the drain\", \"warm water\", \"last reach\"],\n"
               "  \"note\": \"%d seeds x %d runs; every run has its own stirred seed; the path comes from the run number.\",\n  \"seeds\": [\n",
            TENSION_SEEDS, TENSION_RUNS);
    for (int i = 0; i < nt; i++) {
        tension_row(row, sizeof row, i / TENSION_RUNS, i % TENSION_RUNS, &tn[i]);
        fprintf(f, "    %s%s\n", row, i + 1 < nt ? "," : "");
    }
    fprintf(f, "  ]\n}\n");
    fclose(f);

    double sec = 0;
    for (int i = 0; i < ABLATION_SEEDS; i++) sec += ab[i].seconds;
    printf("story bench: %d runs; mean journey %.1f s (default path)\n", n, sec / ABLATION_SEEDS);
    free(jobs);
    free(res);
    return 0;
}

/* ------------------------------------------------------------------ check mode */
static char *slurp(const char *dir, const char *name)
{
    char path[512];
    snprintf(path, sizeof path, "%s/%s", dir, name);
    FILE *f = fopen(path, "rb");
    if (!f) { fprintf(stderr, "missing %s\n", path); exit(1); }
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = malloc((size_t)size + 1);
    if (!buf || fread(buf, 1, (size_t)size, f) != (size_t)size) exit(1);
    buf[size] = 0;
    fclose(f);
    return buf;
}

static int cmp_key(const void *a, const void *b)
{
    uint64_t x = *(const uint64_t *)a, y = *(const uint64_t *)b;
    return x < y ? -1 : x > y;
}

static int check(const char *dir)
{
    int failures = 0;
    /* Every policy key in the bench is distinct (paired arms share a seed by design). */
    int nk = PLAY_SEEDS + ABLATION_SEEDS + TENSION_SEEDS * TENSION_RUNS, k = 0;
    uint64_t *keys = malloc(sizeof *keys * (size_t)nk);
    for (int i = 0; i < PLAY_SEEDS; i++) keys[k++] = play_key(i);
    for (int i = 0; i < ABLATION_SEEDS; i++) keys[k++] = ablation_key(i);
    for (int i = 0; i < TENSION_SEEDS; i++)
        for (int r = 0; r < TENSION_RUNS; r++) keys[k++] = tension_key(i, r);
    qsort(keys, (size_t)nk, sizeof *keys, cmp_key);
    for (int i = 1; i < nk; i++) if (keys[i] == keys[i - 1]) { fputs("[seed] duplicate run key\n", stderr); failures++; }
    free(keys);

    enum { CHECKS = 8 };
    Job jobs[CHECKS] = {play_job(0), play_job(PLAY_SEEDS - 1), ablation_job(ARM_ON, 0), ablation_job(ARM_OFF, 0),
                        ablation_job(ARM_DITCH, ABLATION_SEEDS - 1), ablation_job(ARM_STAY, ABLATION_SEEDS - 1),
                        tension_job(0, 0), tension_job(TENSION_SEEDS - 1, TENSION_RUNS - 1)};
    RunResult res[CHECKS];
    run_all(jobs, CHECKS, res);
    char *play = slurp(dir, "play-order.json"), *abl = slurp(dir, "ablation.json"), *ten = slurp(dir, "tension.json");
    char row[1024];
    /* How many times each re-run row must appear: the default arm is reused by every beat and as one path of each choice. */
    static const int copies[CHECKS] = {1, 1, 8, 1, 1, 1, 1, 1};
    struct { const char *file; char *text; } rows[CHECKS];
    play_row(row, sizeof row, 0, &res[0]);                 rows[0].file = "play-order"; rows[0].text = strdup(row);
    play_row(row, sizeof row, PLAY_SEEDS - 1, &res[1]);    rows[1].file = "play-order"; rows[1].text = strdup(row);
    ablation_row(row, sizeof row, 0, &res[2]);             rows[2].file = "ablation";   rows[2].text = strdup(row);
    ablation_row(row, sizeof row, 0, &res[3]);             rows[3].file = "ablation";   rows[3].text = strdup(row);
    ablation_row(row, sizeof row, ABLATION_SEEDS - 1, &res[4]); rows[4].file = "ablation"; rows[4].text = strdup(row);
    ablation_row(row, sizeof row, ABLATION_SEEDS - 1, &res[5]); rows[5].file = "ablation"; rows[5].text = strdup(row);
    tension_row(row, sizeof row, 0, 0, &res[6]);           rows[6].file = "tension";    rows[6].text = strdup(row);
    tension_row(row, sizeof row, TENSION_SEEDS - 1, TENSION_RUNS - 1, &res[7]); rows[7].file = "tension"; rows[7].text = strdup(row);
    for (int i = 0; i < CHECKS; i++) {
        const char *hay = rows[i].file[0] == 'p' ? play : rows[i].file[0] == 'a' ? abl : ten;
        int found = 0;
        for (const char *c = hay; (c = strstr(c, rows[i].text)); c++) found++;
        if (found != copies[i]) {
            fprintf(stderr, "[bench] %s.json does not match the code (%d of %d copies): %s\n", rows[i].file, found, copies[i], rows[i].text);
            failures++;
        }
        free(rows[i].text);
    }
    free(play); free(abl); free(ten);
    if (failures) { fprintf(stderr, "story bench check: %d failure(s); run make story to regenerate\n", failures); return 1; }
    puts("story bench check: 8 re-run rows match docs/narrative; all run seeds distinct");
    return 0;
}

int main(int argc, char **argv)
{
    if (argc == 3 && strcmp(argv[1], "--check") == 0) return check(argv[2]);
    if (argc == 2) return write_all(argv[1]);
    fputs("usage: story_bench <dir> | story_bench --check <dir>\n", stderr);
    return 2;
}
