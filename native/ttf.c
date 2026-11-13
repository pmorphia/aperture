/* TrueType and OpenType fonts for Aperture, through FreeType. aperture/ttf.fw imports this
   file; it only bakes glyphs into an RGBA atlas in memory, which Aperture turns into a
   texture and a Font. Programs that do not import aperture.ttf do not need FreeType. */

// flywheel: pkg freetype2

#include <ft2build.h>
#include FT_FREETYPE_H
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void fw_foreign_fail(const char *message);

typedef struct {
    float x, y, w, h, xoff, yoff, advance;
    int present;
} BakedGlyph;
typedef struct {
    int width, height;
    unsigned char *pixels; /* width * height RGBA bytes: white, with coverage as alpha */
    float line_height;
    BakedGlyph glyphs[256];
} Baked;

static void fail(const char *what, const char *path) {
    char message[512];
    snprintf(message, sizeof message, "%s '%s'", what, path);
    fw_foreign_fail(message);
}
/* Renders the Latin-1 characters of a font file at a pixel size into one atlas. Returns the
   address of the result (free it with ttf_free), or fails. */
Baked *ttf_bake(const char *path, double pixel_size) {
    if (pixel_size < 4 || pixel_size > 512) {
        fw_foreign_fail("font size must be between 4 and 512 pixels");
        return NULL;
    }
    FT_Library library;
    if (FT_Init_FreeType(&library)) {
        fw_foreign_fail("cannot start FreeType");
        return NULL;
    }
    FT_Face face;
    if (FT_New_Face(library, path, 0, &face)) {
        FT_Done_FreeType(library);
        fail("cannot load font", path);
        return NULL;
    }
    FT_Set_Pixel_Sizes(face, 0, (FT_UInt)pixel_size);
    Baked *baked = calloc(1, sizeof *baked);
    float ascent = face->size->metrics.ascender / 64.0f;
    baked->line_height = face->size->metrics.height / 64.0f;
    /* Rows of glyphs with a pixel between them, in an atlas wide enough for about 16 a row. */
    int width = 64;
    while (width < (int)pixel_size * 16 && width < 4096)
        width *= 2;
    int capacity = width, x = 1, y = 1, row = 0;
    baked->pixels = calloc((size_t)width * capacity, 4);
    for (int code = 32; code < 256; code++) {
        if (code >= 127 && code < 160)
            continue;
        if (FT_Load_Char(face, (FT_ULong)code, FT_LOAD_RENDER))
            continue;
        FT_GlyphSlot slot = face->glyph;
        int w = (int)slot->bitmap.width, h = (int)slot->bitmap.rows;
        if (x + w + 1 > width) {
            x = 1;
            y += row + 1;
            row = 0;
        }
        if (y + h + 1 > capacity) {
            int grown = capacity * 2;
            unsigned char *more = calloc((size_t)width * grown, 4);
            memcpy(more, baked->pixels, (size_t)width * capacity * 4);
            free(baked->pixels);
            baked->pixels = more;
            capacity = grown;
        }
        for (int r = 0; r < h; r++)
            for (int c = 0; c < w; c++) {
                unsigned char *p = baked->pixels + ((size_t)(y + r) * width + x + c) * 4;
                p[0] = p[1] = p[2] = 255;
                p[3] = slot->bitmap.buffer[r * slot->bitmap.pitch + c];
            }
        BakedGlyph *glyph = &baked->glyphs[code];
        glyph->x = (float)x, glyph->y = (float)y, glyph->w = (float)w, glyph->h = (float)h;
        glyph->xoff = (float)slot->bitmap_left;
        glyph->yoff = ascent - slot->bitmap_top;
        glyph->advance = slot->advance.x / 64.0f;
        glyph->present = 1;
        x += w + 1;
        if (h > row)
            row = h;
    }
    baked->width = width;
    baked->height = y + row + 1;
    FT_Done_Face(face);
    FT_Done_FreeType(library);
    return baked;
}
long ttf_width(const Baked *baked) {
    return baked ? baked->width : 0;
}
long ttf_height(const Baked *baked) {
    return baked ? baked->height : 0;
}
/* The address of the atlas pixels. */
const unsigned char *ttf_pixels(const Baked *baked) {
    return baked ? baked->pixels : NULL;
}
double ttf_line_height(const Baked *baked) {
    return baked ? baked->line_height : 0;
}
bool ttf_has(const Baked *baked, long code) {
    return baked && code >= 0 && code < 256 && baked->glyphs[code].present;
}
/* A measure of a glyph: 0 x, 1 y, 2 width, 3 height, 4 x offset, 5 y offset, 6 advance. */
double ttf_glyph(const Baked *baked, long code, long field) {
    if (!ttf_has(baked, code) || field < 0 || field > 6)
        return 0;
    const BakedGlyph *glyph = &baked->glyphs[code];
    const float values[7] = {glyph->x, glyph->y,    glyph->w,      glyph->h,
                             glyph->xoff, glyph->yoff, glyph->advance};
    return values[field];
}
void ttf_free(Baked *baked) {
    if (!baked)
        return;
    free(baked->pixels);
    free(baked);
}
