/*
 * Unit tests for the platform independent core (font formats, glyph
 * operations, undo history, CP437 mapping). Build and run with "make test".
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cp437.h"
#include "font.h"
#include "undo.h"

static int failures, checks;

#define CHECK(cond) do { \
    checks++; \
    if (!(cond)) { \
        failures++; \
        printf("%s:%d: check failed: %s\n", __FILE__, __LINE__, #cond); \
    } \
} while (0)

static void fill_pattern(VgaFont *f, int height)
{
    int ch, y;

    font_init(f, height);
    for (ch = 0; ch < FONT_GLYPHS; ch++)
        for (y = 0; y < height; y++)
            f->rows[ch][y] = (unsigned char)(ch * 31 + y * 7 + 1);
}

static int fonts_equal(const VgaFont *a, const VgaFont *b)
{
    return memcmp(a, b, sizeof(*a)) == 0;
}

static void test_pixels(void)
{
    VgaFont f;

    font_init(&f, 16);
    font_set_pixel(&f, 'A', 0, 3, 1);
    font_set_pixel(&f, 'A', 7, 3, 1);
    CHECK(f.rows['A'][3] == 0x81);
    CHECK(font_get_pixel(&f, 'A', 0, 3) == 1);
    CHECK(font_get_pixel(&f, 'A', 1, 3) == 0);
    font_set_pixel(&f, 'A', 0, 3, 0);
    CHECK(f.rows['A'][3] == 0x01);
    font_set_pixel(&f, 'A', 0, 16, 1);  /* out of range: ignored */
    CHECK(f.rows['A'][16] == 0);

    /* Ninth column repeats column 7 only for 0xC0-0xDF. */
    font_set_pixel(&f, 0xC4, 7, 5, 1);
    font_set_pixel(&f, 0xB3, 7, 5, 1);
    CHECK(font_display_pixel(&f, 0xC4, 8, 5, 1) == 1);
    CHECK(font_display_pixel(&f, 0xC4, 8, 5, 0) == 0);
    CHECK(font_display_pixel(&f, 0xB3, 8, 5, 1) == 0);
}

static void test_round_trip(FontFormat format, int height, size_t expected_size)
{
    VgaFont a, b;
    unsigned char *data;
    size_t size = 0;
    FontFormat detected;
    unsigned warnings = 99;

    fill_pattern(&a, height);
    data = font_serialize(&a, format, NULL, &size);
    CHECK(data != NULL);
    CHECK(size == expected_size);
    CHECK(font_parse(&b, data, size, 0, &detected, &warnings) == FONT_OK);
    CHECK(detected == format);
    CHECK(warnings == 0);
    CHECK(fonts_equal(&a, &b));
    free(data);
}

static void test_formats(void)
{
    VgaFont f;
    unsigned char buf[4 + 512 * 16];
    unsigned char psf2[32 + 100 * 8];
    unsigned warnings;
    FontFormat format;

    test_round_trip(FONT_FORMAT_RAW, 16, 4096);
    test_round_trip(FONT_FORMAT_RAW, 8, 2048);
    test_round_trip(FONT_FORMAT_PSF1, 14, 4 + 256 * 14);
    test_round_trip(FONT_FORMAT_PSF2, 16, 32 + 256 * 16);
    test_round_trip(FONT_FORMAT_PSF2, 32, 32 + 256 * 32);

    /* PSF1 with 512 glyphs and a Unicode table. */
    memset(buf, 0, sizeof(buf));
    buf[0] = 0x36; buf[1] = 0x04; buf[2] = 0x03; buf[3] = 16;
    buf[4 + 'A' * 16 + 2] = 0x18;
    CHECK(font_parse(&f, buf, sizeof(buf), 0, &format, &warnings) == FONT_OK);
    CHECK(format == FONT_FORMAT_PSF1 && f.height == 16 && f.rows['A'][2] == 0x18);
    CHECK(warnings == (FONT_WARN_EXTRA_GLYPHS | FONT_WARN_UNICODE_TABLE));
    CHECK(font_parse(&f, buf, 100, 0, NULL, NULL) == FONT_ERR_TRUNCATED);

    /* PSF2, 6 pixels wide, 100 glyphs of height 8. */
    memset(psf2, 0, sizeof(psf2));
    psf2[0] = 0x72; psf2[1] = 0xB5; psf2[2] = 0x4A; psf2[3] = 0x86;
    psf2[8] = 32; psf2[16] = 100; psf2[20] = 8; psf2[24] = 8; psf2[28] = 6;
    psf2[32 + 'B' * 8] = 0xFC;
    CHECK(font_parse(&f, psf2, sizeof(psf2), 0, &format, &warnings) == FONT_OK);
    CHECK(format == FONT_FORMAT_PSF2 && f.height == 8 && f.rows['B'][0] == 0xFC);
    CHECK(warnings == FONT_WARN_MISSING_GLYPHS);
    psf2[28] = 9;
    CHECK(font_parse(&f, psf2, sizeof(psf2), 0, NULL, NULL) == FONT_ERR_BAD_WIDTH);
    psf2[28] = 8;
    psf2[24] = 40;
    CHECK(font_parse(&f, psf2, sizeof(psf2), 0, NULL, NULL) == FONT_ERR_BAD_HEIGHT);

    /* Raw: the extension hint decides between 512x16 and 256x32. */
    {
        static unsigned char raw[8192];
        CHECK(font_parse(&f, raw, sizeof(raw), 0, &format, &warnings) == FONT_OK);
        CHECK(format == FONT_FORMAT_RAW && f.height == 32 && warnings == 0);
        CHECK(font_parse(&f, raw, sizeof(raw), 16, NULL, &warnings) == FONT_OK);
        CHECK(f.height == 16 && warnings == FONT_WARN_EXTRA_GLYPHS);
        CHECK(font_parse(&f, raw, 1000, 0, NULL, NULL) == FONT_ERR_UNKNOWN_FORMAT);
        CHECK(font_parse(&f, raw, 0, 0, NULL, NULL) == FONT_ERR_UNKNOWN_FORMAT);
    }
}

static void test_c_export(void)
{
    VgaFont f;
    size_t size = 0;
    char *text;

    fill_pattern(&f, 16);
    text = (char *)font_serialize(&f, FONT_FORMAT_C_SOURCE, "8x16-font", &size);
    CHECK(text != NULL);
    CHECK(size == strlen(text));
    CHECK(strstr(text, "static const unsigned char font_8x16_font[FONT_8X16_FONT_GLYPHS * FONT_8X16_FONT_HEIGHT]") != NULL);
    CHECK(strstr(text, "#define FONT_8X16_FONT_HEIGHT 16") != NULL);
    CHECK(strstr(text, "/* 0x41 'A' */") != NULL);
    free(text);

    text = (char *)font_serialize(&f, FONT_FORMAT_C_SOURCE, "", &size);
    CHECK(text != NULL && strstr(text, "vga_font[") != NULL);
    free(text);
}

static void test_resize(void)
{
    VgaFont f;
    int y;

    font_init(&f, 4);
    for (y = 0; y < 4; y++)
        f.rows[1][y] = (unsigned char)(y + 1);   /* 1 2 3 4 */

    {
        VgaFont g = f;
        font_resize(&g, 6, FONT_RESIZE_TOP);
        CHECK(g.height == 6 && g.rows[1][0] == 1 && g.rows[1][3] == 4 && g.rows[1][4] == 0);
        font_resize(&g, 2, FONT_RESIZE_TOP);
        CHECK(g.height == 2 && g.rows[1][1] == 2 && g.rows[1][2] == 0 && g.rows[1][3] == 0);
    }
    {
        VgaFont g = f;
        font_resize(&g, 6, FONT_RESIZE_CENTER);
        CHECK(g.rows[1][0] == 0 && g.rows[1][1] == 1 && g.rows[1][4] == 4 && g.rows[1][5] == 0);
    }
    {
        VgaFont g = f;
        font_resize(&g, 6, FONT_RESIZE_BOTTOM);
        CHECK(g.rows[1][1] == 0 && g.rows[1][2] == 1 && g.rows[1][5] == 4);
        font_resize(&g, 3, FONT_RESIZE_BOTTOM);
        CHECK(g.rows[1][0] == 2 && g.rows[1][2] == 4 && g.rows[1][3] == 0);
    }
    {
        VgaFont g = f;
        font_resize(&g, 8, FONT_RESIZE_SCALE);
        CHECK(g.rows[1][0] == 1 && g.rows[1][1] == 1 && g.rows[1][6] == 4 && g.rows[1][7] == 4);
        font_resize(&g, 4, FONT_RESIZE_SCALE);
        CHECK(fonts_equal(&f, &g));
    }
}

static void test_glyph_ops(void)
{
    VgaFont f, orig;

    fill_pattern(&f, 16);
    orig = f;
    glyph_flip_horizontal(&f, 10);
    CHECK(!fonts_equal(&f, &orig));
    glyph_flip_horizontal(&f, 10);
    CHECK(fonts_equal(&f, &orig));
    glyph_flip_vertical(&f, 10);
    CHECK(f.rows[10][0] == orig.rows[10][15]);
    glyph_flip_vertical(&f, 10);
    glyph_invert(&f, 10);
    CHECK(f.rows[10][0] == (unsigned char)~orig.rows[10][0]);
    CHECK(f.rows[10][16] == 0);  /* rows past the height stay clear */
    glyph_invert(&f, 10);
    CHECK(fonts_equal(&f, &orig));

    font_init(&f, 8);
    f.rows[0][0] = 0x81;
    glyph_shift(&f, 0, 1, 0);
    CHECK(f.rows[0][0] == 0x40);
    glyph_shift(&f, 0, -2, 0);
    CHECK(f.rows[0][0] == 0x00);
    f.rows[0][0] = 0x18;
    glyph_shift(&f, 0, 0, 1);
    CHECK(f.rows[0][0] == 0 && f.rows[0][1] == 0x18);
    glyph_shift(&f, 0, 0, -1);
    CHECK(f.rows[0][0] == 0x18 && f.rows[0][1] == 0);
    glyph_bold(&f, 0);
    CHECK(f.rows[0][0] == 0x1C);
    glyph_clear(&f, 0);
    CHECK(f.rows[0][0] == 0);
}

static void test_art(void)
{
    VgaFont f;
    wchar_t buf[400];
    unsigned char rows[FONT_MAX_HEIGHT];
    int count = 0;

    fill_pattern(&f, 16);
    glyph_to_art(&f, 'Q', buf, 400);
    CHECK(glyph_from_art(buf, rows, &count));
    CHECK(count == 16);
    CHECK(memcmp(rows, f.rows['Q'], 16) == 0);

    CHECK(glyph_from_art(L"..##..\n.#..#.\r\n", rows, &count) && count == 2);
    CHECK(rows[0] == 0x30 && rows[1] == 0x48);
    CHECK(!glyph_from_art(L"hello", rows, &count));
    CHECK(!glyph_from_art(L"   \n  ", rows, &count));
}

static void test_cp437(void)
{
    int i;

    for (i = 0; i < 256; i++)
        CHECK(unicode_to_cp437(cp437_to_unicode[i]) == i);
    CHECK(unicode_to_cp437('A') == 'A');
    CHECK(unicode_to_cp437(0x2588) == 0xDB);  /* full block */
    CHECK(unicode_to_cp437(0x20AC) == -1);    /* euro sign */
}

static void test_undo(void)
{
    UndoHistory h;
    VgaFont f;
    int ch = 5, i;

    undo_init(&h, 3);
    font_init(&f, 16);
    CHECK(!undo_can_undo(&h) && !undo_can_redo(&h));

    undo_push(&h, &f, ch);
    f.rows[5][0] = 0xFF;
    ch = 9;
    CHECK(undo_undo(&h, &f, &ch));
    CHECK(f.rows[5][0] == 0 && ch == 5);
    CHECK(undo_can_redo(&h));
    CHECK(undo_redo(&h, &f, &ch));
    CHECK(f.rows[5][0] == 0xFF && ch == 9);
    CHECK(!undo_redo(&h, &f, &ch));

    /* A new change clears redo; the limit drops the oldest state. */
    undo_clear(&h);
    for (i = 0; i < 5; i++) {
        undo_push(&h, &f, ch);
        f.rows[0][0] = (unsigned char)(i + 1);
    }
    for (i = 0; i < 3; i++)
        CHECK(undo_undo(&h, &f, &ch));
    CHECK(!undo_undo(&h, &f, &ch));
    CHECK(f.rows[0][0] == 2);

    /* Height changes are undone too. */
    undo_clear(&h);
    undo_push(&h, &f, ch);
    font_resize(&f, 8, FONT_RESIZE_TOP);
    CHECK(undo_undo(&h, &f, &ch) && f.height == 16);
    undo_free(&h);
}

int main(void)
{
    test_pixels();
    test_formats();
    test_c_export();
    test_resize();
    test_glyph_ops();
    test_art();
    test_cp437();
    test_undo();
    printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
