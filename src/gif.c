#include "gif.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MIN_CODE 4
#define CLEAR (1 << MIN_CODE)
#define EOI (CLEAR + 1)
#define HASH 8209

struct Gif {
    FILE *f;
    int w, h, delay;
    unsigned char block[255];
    int blen;
    unsigned long acc;
    int nbits;
    int hkey[HASH];
    int hval[HASH];
};

static void put16(FILE *f, int v)
{
    fputc(v & 255, f);
    fputc((v >> 8) & 255, f);
}

Gif *gif_open(const char *path, int width, int height, unsigned char palette[GIF_COLOURS][3], int delay_cs)
{
    Gif *g = calloc(1, sizeof *g);
    if (!g) return NULL;
    g->f = fopen(path, "wb");
    if (!g->f) { free(g); return NULL; }
    g->w = width; g->h = height; g->delay = delay_cs;
    fwrite("GIF89a", 1, 6, g->f);
    put16(g->f, width);
    put16(g->f, height);
    fputc(0xB3, g->f);   /* global table, 16 colours */
    fputc(0, g->f);
    fputc(0, g->f);
    for (int i = 0; i < GIF_COLOURS; i++) fwrite(palette[i], 1, 3, g->f);
    fwrite("\x21\xFF\x0BNETSCAPE2.0\x03\x01\x00\x00\x00", 1, 19, g->f);
    return g;
}

static void emit_byte(Gif *g, int b)
{
    g->block[g->blen++] = (unsigned char)b;
    if (g->blen == 255) {
        fputc(255, g->f);
        fwrite(g->block, 1, 255, g->f);
        g->blen = 0;
    }
}

static void put_code(Gif *g, int code, int size)
{
    g->acc |= (unsigned long)code << g->nbits;
    g->nbits += size;
    while (g->nbits >= 8) {
        emit_byte(g, (int)(g->acc & 255));
        g->acc >>= 8;
        g->nbits -= 8;
    }
}

static void reset_table(Gif *g)
{
    for (int i = 0; i < HASH; i++) g->hkey[i] = -1;
}

static int *lookup(Gif *g, int key)
{
    int slot = (int)(((unsigned)key * 2654435761u) % HASH);
    while (g->hkey[slot] != -1 && g->hkey[slot] != key) slot = (slot + 1) % HASH;
    g->hkey[slot] = key;
    return &g->hval[slot];
}

static int find(Gif *g, int key)
{
    int slot = (int)(((unsigned)key * 2654435761u) % HASH);
    while (g->hkey[slot] != -1) {
        if (g->hkey[slot] == key) return g->hval[slot];
        slot = (slot + 1) % HASH;
    }
    return -1;
}

int gif_frame(Gif *g, const unsigned char *pixels, int hold_cs)
{
    int delay = hold_cs > 0 ? hold_cs : g->delay;
    fwrite("\x21\xF9\x04\x04", 1, 4, g->f);
    put16(g->f, delay);
    fputc(0, g->f);
    fputc(0, g->f);
    fputc(0x2C, g->f);
    put16(g->f, 0); put16(g->f, 0); put16(g->f, g->w); put16(g->f, g->h);
    fputc(0, g->f);
    fputc(MIN_CODE, g->f);

    int n = g->w * g->h, size = MIN_CODE + 1, next = EOI + 1;
    g->acc = 0; g->nbits = 0; g->blen = 0;
    reset_table(g);
    put_code(g, CLEAR, size);
    int prefix = pixels[0] & 15;
    for (int i = 1; i < n; i++) {
        int c = pixels[i] & 15, key = (prefix << 8) | c, hit = find(g, key);
        if (hit >= 0) { prefix = hit; continue; }
        put_code(g, prefix, size);
        *lookup(g, key) = next++;
        if (next == 4096) {
            put_code(g, CLEAR, size);
            reset_table(g);
            size = MIN_CODE + 1;
            next = EOI + 1;
        } else if (next > (1 << size)) {
            size++;
        }
        prefix = c;
    }
    put_code(g, prefix, size);
    /* the decoder adds one more entry after the last code, which may widen the next code */
    if (++next > (1 << size) && size < 12) size++;
    put_code(g, EOI, size);
    if (g->nbits > 0) emit_byte(g, (int)(g->acc & 255));
    if (g->blen > 0) {
        fputc(g->blen, g->f);
        fwrite(g->block, 1, (size_t)g->blen, g->f);
    }
    fputc(0, g->f);
    return ferror(g->f) ? 1 : 0;
}

int gif_close(Gif *g)
{
    fputc(0x3B, g->f);
    int bad = ferror(g->f) != 0;
    if (fclose(g->f)) bad = 1;
    free(g);
    return bad;
}

/* 5x7 glyphs, one byte per row, bit 4 is the leftmost pixel. Characters not listed here are drawn
 * by dedicated code in draw_cell. */
static const unsigned char *glyph(char ch)
{
    static const unsigned char gt[7] = {16, 24, 28, 30, 28, 24, 16};
    static const unsigned char lt[7] = {1, 3, 7, 15, 7, 3, 1};
    static const unsigned char up[7] = {4, 14, 31, 14, 14, 14, 0};
    static const unsigned char dn[7] = {0, 14, 14, 14, 31, 14, 4};
    static const unsigned char sl[7] = {1, 2, 2, 4, 8, 8, 16};
    static const unsigned char bs[7] = {16, 8, 8, 4, 2, 2, 1};
    static const unsigned char dot[7] = {14, 31, 31, 31, 31, 31, 14};
    static const unsigned char star[7] = {4, 21, 14, 31, 14, 21, 4};
    static const unsigned char dash[7] = {0, 0, 0, 31, 0, 0, 0};
    switch (ch) {
    case '>': return gt;
    case '<': return lt;
    case '^': return up;
    case 'v': return dn;
    case '/': return sl;
    case '\\': return bs;
    case 'o': case '@': return dot;
    case '*': return star;
    case '-': return dash;
    default: return NULL;
    }
}

void gif_text_size(const char *text, int scale, int *width, int *height)
{
    int cols = 0, rows = 0, cur = 0;
    for (const char *p = text; *p; p++) {
        if (*p == '\n') { rows++; if (cur > cols) cols = cur; cur = 0; }
        else cur++;
    }
    if (cur > 0) { rows++; if (cur > cols) cols = cur; }
    *width = cols * 6 * scale;
    *height = rows * 9 * scale;
}

static void fill(unsigned char *px, int w, int h, int x0, int y0, int x1, int y1, int colour)
{
    for (int y = y0; y < y1 && y < h; y++)
        for (int x = x0; x < x1 && x < w; x++)
            if (x >= 0 && y >= 0) px[y * w + x] = (unsigned char)colour;
}

static void draw_cell(unsigned char *px, int w, int h, int cx, int cy, int s, char ch, int colour)
{
    int x0 = cx * 6 * s, y0 = cy * 9 * s;
    switch (ch) {
    case ' ': return;
    case '#': fill(px, w, h, x0, y0, x0 + 6 * s, y0 + 9 * s, colour); return;
    case '=': fill(px, w, h, x0, y0 + 3 * s, x0 + 6 * s, y0 + 6 * s, colour); return;
    case '_': fill(px, w, h, x0, y0 + 8 * s, x0 + 6 * s, y0 + 9 * s, colour); return;
    case '|': fill(px, w, h, x0 + 2 * s, y0, x0 + 4 * s, y0 + 9 * s, colour); return;
    case '.': fill(px, w, h, x0 + 2 * s, y0 + 4 * s, x0 + 4 * s, y0 + 6 * s, colour); return;
    default: break;
    }
    const unsigned char *rows = glyph(ch);
    if (!rows) { fill(px, w, h, x0 + 2 * s, y0 + 3 * s, x0 + 4 * s, y0 + 5 * s, colour); return; }
    for (int r = 0; r < 7; r++)
        for (int b = 0; b < 5; b++)
            if (rows[r] & (16 >> b))
                fill(px, w, h, x0 + b * s, y0 + (r + 1) * s, x0 + (b + 1) * s, y0 + (r + 2) * s, colour);
}

void gif_draw_text(unsigned char *pixels, int width, int height, const char *text, int scale,
                   int (*colour_of)(char))
{
    memset(pixels, 0, (size_t)width * (size_t)height);
    int cx = 0, cy = 0;
    for (const char *p = text; *p; p++) {
        if (*p == '\n') { cy++; cx = 0; continue; }
        draw_cell(pixels, width, height, cx, cy, scale, *p, colour_of(*p));
        cx++;
    }
}
