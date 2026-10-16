#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../src/gif.h"

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

static unsigned char palette[GIF_COLOURS][3];

/* Independent LZW decoder for the first frame of a file written by gif.c. Returns pixels decoded. */
static int decode_first(const char *path, int *w, int *h, unsigned char *out, int cap)
{
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    static unsigned char file[1 << 22];
    size_t len = fread(file, 1, sizeof file, f);
    fclose(f);
    if (len < 13 || memcmp(file, "GIF89a", 6) != 0) return -1;
    *w = file[6] | file[7] << 8;
    *h = file[8] | file[9] << 8;
    size_t p = 13 + 48;
    while (p < len && file[p] != 0x2C) {
        if (file[p] != 0x21) return -1;
        p += 2;
        while (file[p]) p += file[p] + 1;
        p++;
    }
    p += 10;
    int mcs = file[p++];
    static unsigned char data[1 << 22];
    size_t dl = 0;
    while (file[p]) { memcpy(data + dl, file + p + 1, file[p]); dl += file[p]; p += file[p] + 1; }

    int clear = 1 << mcs, eoi = clear + 1, size = mcs + 1, next = eoi + 1, prev = -1, n = 0;
    static int prefix[4096];
    static unsigned char suffix[4096], stack[4096];
    unsigned long acc = 0;
    int nbits = 0;
    size_t pos = 0;
    for (;;) {
        while (nbits < size && pos < dl) { acc |= (unsigned long)data[pos++] << nbits; nbits += 8; }
        if (nbits < size) return -2;
        int code = (int)(acc & ((1u << size) - 1));
        acc >>= size; nbits -= size;
        if (code == clear) { size = mcs + 1; next = eoi + 1; prev = -1; continue; }
        if (code == eoi) break;
        int cur = code, top = 0;
        if (prev == -1) {
            if (n < cap) out[n++] = (unsigned char)code;
            prev = code;
            continue;
        }
        if (code >= next) { stack[top++] = 0; cur = prev; }   /* first char patched below */
        while (cur >= clear) { stack[top++] = suffix[cur]; cur = prefix[cur]; }
        stack[top++] = (unsigned char)cur;
        unsigned char first = stack[top - 1];
        if (code >= next) stack[0] = first;
        while (top) if (n < cap) out[n++] = stack[--top]; else top--;
        if (next < 4096) { prefix[next] = prev; suffix[next] = first; next++; }
        if (next == (1 << size) && size < 12) size++;
        prev = code;
    }
    return n;
}

static void write_one(const char *path, int w, int h, const unsigned char *px)
{
    Gif *g = gif_open(path, w, h, palette, 5);
    CHECK(g != NULL, "cannot open %s", path);
    if (!g) return;
    CHECK(gif_frame(g, px, 0) == 0, "frame write failed");
    CHECK(gif_close(g) == 0, "close failed");
}

static void test_round_trip_of_noise_and_flat_images(void)
{
    static unsigned char px[300 * 200], back[300 * 200];
    srand(7);
    int sizes[][2] = {{1, 1}, {2, 3}, {17, 5}, {64, 64}, {100, 57}, {300, 200}};
    for (size_t s = 0; s < sizeof sizes / sizeof *sizes; s++) {
        int w = sizes[s][0], h = sizes[s][1];
        for (int mode = 0; mode < 3; mode++) {
            for (int i = 0; i < w * h; i++)
                px[i] = (unsigned char)(mode == 0 ? rand() & 15 : mode == 1 ? 3 : (i / 7 + (i % 5 == 0)) & 15);
            write_one("/tmp/gif_test.gif", w, h, px);
            int rw = 0, rh = 0, n = decode_first("/tmp/gif_test.gif", &rw, &rh, back, (int)sizeof back);
            CHECK(rw == w && rh == h, "size %dx%d read back as %dx%d", w, h, rw, rh);
            CHECK(n == w * h, "%dx%d mode %d decoded %d pixels", w, h, mode, n);
            CHECK(n == w * h && memcmp(px, back, (size_t)n) == 0, "%dx%d mode %d pixels differ", w, h, mode);
        }
    }
}

static void test_flat_frames_compress_well(void)
{
    static unsigned char px[400 * 200];
    memset(px, 2, sizeof px);
    write_one("/tmp/gif_flat.gif", 400, 200, px);
    FILE *f = fopen("/tmp/gif_flat.gif", "rb");
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fclose(f);
    CHECK(size < 2000, "a flat 400x200 frame should be tiny, got %ld bytes", size);
}

static void test_animation_structure(void)
{
    static unsigned char px[16 * 16];
    Gif *g = gif_open("/tmp/gif_anim.gif", 16, 16, palette, 4);
    CHECK(g != NULL, "cannot open animation");
    if (!g) return;
    for (int k = 0; k < 5; k++) {
        memset(px, k, sizeof px);
        CHECK(gif_frame(g, px, k == 4 ? 100 : 0) == 0, "frame %d failed", k);
    }
    CHECK(gif_close(g) == 0, "close failed");
    FILE *f = fopen("/tmp/gif_anim.gif", "rb");
    static unsigned char file[1 << 16];
    size_t len = fread(file, 1, sizeof file, f);
    fclose(f);
    CHECK(memcmp(file, "GIF89a", 6) == 0, "missing header");
    CHECK(file[len - 1] == 0x3B, "missing trailer");
    int frames = 0, loops = 0;
    for (size_t i = 0; i + 1 < len; i++) {
        if (file[i] == 0x21 && file[i + 1] == 0xF9 && file[i + 2] == 0x04) frames++;
        if (!memcmp(file + i, "NETSCAPE2.0", 11)) loops++;
    }
    CHECK(frames == 5, "expected 5 frames, found %d", frames);
    CHECK(loops == 1, "expected one looping extension");
}

static int white(char c) { (void)c; return 1; }

static void test_text_drawing(void)
{
    int w, h;
    gif_text_size("ab\ncd", 2, &w, &h);
    CHECK(w == 2 * 6 * 2 && h == 2 * 9 * 2, "text size %dx%d", w, h);
    static unsigned char px[24 * 36];
    gif_draw_text(px, w, h, "#=\n_|", 2, white);
    CHECK(px[0] == 1, "'#' should fill its cell");
    CHECK(px[(0 * 9 + 4) * 2 * w + 7] == 0 || px[4 * 2 * w + 7] == 1, "'=' draws a bar");
    int lit = 0;
    for (int i = 0; i < w * h; i++) lit += px[i];
    CHECK(lit > 100, "expected a drawn picture, lit pixels %d", lit);
    gif_draw_text(px, w, h, "  \n  ", 2, white);
    lit = 0;
    for (int i = 0; i < w * h; i++) lit += px[i];
    CHECK(lit == 0, "spaces draw nothing");
}

int main(void)
{
    for (int i = 0; i < GIF_COLOURS; i++) palette[i][0] = palette[i][1] = palette[i][2] = (unsigned char)(i * 17);
    test_round_trip_of_noise_and_flat_images();
    test_flat_frames_compress_well();
    test_animation_structure();
    test_text_drawing();
    if (failures) {
        printf("%d check(s) failed\n", failures);
        return 1;
    }
    puts("gif tests passed");
    return 0;
}
