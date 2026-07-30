#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"
#include "font.h"
#include "settings.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static stbtt_fontinfo font_info;
static unsigned char *font_buffer = NULL;
static float font_scale;
static int font_loaded = 0;

#ifndef UI_SCALE
#define UI_SCALE 100
#endif
#define FONT_SIZE (26.0f * UI_SCALE / 100.0f)   /* NextUI-style larger text */

// Internal function to load a font file
static int load_font_file(const char *font_filename) {
    // Free previous font if loaded
    if (font_buffer) {
        free(font_buffer);
        font_buffer = NULL;
        font_loaded = 0;
    }

    // Build search paths for the font (SF3000 paths first)
    char font_paths[4][256];
    snprintf(font_paths[0], sizeof(font_paths[0]), "/mnt/sdcard/cubegm/fonts/%s", font_filename);
    snprintf(font_paths[1], sizeof(font_paths[1]), "/mnt/sdcard/frogui/fonts/%s", font_filename);
    snprintf(font_paths[2], sizeof(font_paths[2]), "/mnt/sdcard/frogui/fonts/%s", font_filename);
    snprintf(font_paths[3], sizeof(font_paths[3]), "fonts/%s", font_filename);

    FILE *fp = NULL;
    for (int i = 0; i < 4; i++) {
        fp = fopen(font_paths[i], "rb");
        if (fp) break;
    }

    if (!fp) {
        return 0;
    }

    // Get file size
    fseek(fp, 0, SEEK_END);
    long font_size = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    // Allocate buffer and read font
    font_buffer = (unsigned char*)malloc(font_size);
    if (!font_buffer) {
        fclose(fp);
        return 0;
    }

    fread(font_buffer, 1, font_size, fp);
    fclose(fp);

    // Initialize font
    if (!stbtt_InitFont(&font_info, font_buffer, stbtt_GetFontOffsetForIndex(font_buffer, 0))) {
        free(font_buffer);
        font_buffer = NULL;
        return 0;
    }

    // Calculate scale for desired pixel height
    font_scale = stbtt_ScaleForPixelHeight(&font_info, FONT_SIZE);
    font_loaded = 1;
    return 1;
}

void font_load_file(const char *font_filename) {
    if (!font_filename || !font_filename[0]) return;
    load_font_file(font_filename);
    if (font_loaded)
        font_scale = stbtt_ScaleForPixelHeight(&font_info, FONT_SIZE);
}

void font_load_from_settings(const char *font_name) {
    const char *font_filename = NULL;
    float custom_size = FONT_SIZE;

    // Map font names to font files — always render at FONT_SIZE (scaled by UI_SCALE)
    if (strcmp(font_name, "Monogram") == 0) {
        font_filename = "monogram.ttf";
    } else if (strcmp(font_name, "GamePocket") == 0) {
        font_filename = "GamePocket-Regular-ZeroKern.ttf";
    } else {
        font_filename = "BPreplayBold.otf";   /* NextUI-style default */
    }
    custom_size = FONT_SIZE;  // use compile-time size, not hardcoded per-font px

    load_font_file(font_filename);

    if (font_loaded) {
        font_scale = stbtt_ScaleForPixelHeight(&font_info, custom_size);
    }
}

void font_init(void) {
    // Default to the NextUI-style bold font; fall back if the file is missing.
    if (load_font_file("BPreplayBold.otf"))
        font_scale = stbtt_ScaleForPixelHeight(&font_info, FONT_SIZE);
    else
        font_load_from_settings("GamePocket");
}

/* Rasterize glyphs into a static buffer instead of stbtt_GetGlyphBitmap (which
 * mallocs per glyph per frame). On memory-pressured devices those allocs can
 * transiently fail -> draw bails -> glyphs vanish for a frame. Ported from the
 * same fix in picoarch/menu_font.c. No per-frame allocation now. */
#define GLYPH_MAX 128
/* Per-glyph cache: rasterize each character ONCE at the current font scale and
 * reuse it. The old code ran stbtt_MakeGlyphBitmap + GetFontVMetrics for every
 * character every frame, which made scrolling crawl with the larger font. */
struct gcache_ent { int valid, w, h, xoff, yoff; unsigned char *bmp; };
static struct gcache_ent gcache[128];
static float gcache_scale = -1.0f;
static int   gcache_baseline = 0;
static void gcache_reset(void) {
    for (int i = 0; i < 128; i++) { free(gcache[i].bmp); gcache[i].bmp = NULL; gcache[i].valid = 0; }
}

void font_draw_char(uint16_t *framebuffer, int screen_width, int screen_height,
                   int x, int y, char c, uint16_t color) {
    if (!font_loaded || !framebuffer) return;

    // Convert to uppercase
    if (c >= 'a' && c <= 'z') {
        c = c - 'a' + 'A';
    }
    unsigned char idx = (unsigned char)c;
    if (idx >= 128) return;

    // Rebuild cache if the font/scale changed
    if (gcache_scale != font_scale) {
        gcache_reset();
        gcache_scale = font_scale;
        int ascent, descent, line_gap;
        stbtt_GetFontVMetrics(&font_info, &ascent, &descent, &line_gap);
        gcache_baseline = (int)(ascent * font_scale);
    }

    struct gcache_ent *g = &gcache[idx];
    if (!g->valid) {
        int glyph_index = stbtt_FindGlyphIndex(&font_info, c);
        if (glyph_index == 0) { g->valid = 1; g->w = g->h = 0; }   // no glyph: cache empty
        else {
            int xoff, yoff, x1, y1;
            stbtt_GetGlyphBitmapBox(&font_info, glyph_index, font_scale, font_scale, &xoff, &yoff, &x1, &y1);
            int width = x1 - xoff, height = y1 - yoff;
            if (width <= 0 || height <= 0 || width > GLYPH_MAX || height > GLYPH_MAX) {
                g->valid = 1; g->w = g->h = 0;
            } else {
                g->bmp = (unsigned char*)malloc((size_t)width * height);
                if (!g->bmp) return;   // alloc fail: try again next frame
                stbtt_MakeGlyphBitmap(&font_info, g->bmp, width, height, width, font_scale, font_scale, glyph_index);
                g->w = width; g->h = height; g->xoff = xoff; g->yoff = yoff; g->valid = 1;
            }
        }
    }
    if (g->w <= 0 || g->h <= 0) return;   // space / empty

    int width = g->w, height = g->h, xoff = g->xoff, yoff = g->yoff, baseline = gcache_baseline;
    const unsigned char *bitmap = g->bmp;
    // Draw the glyph
    for (int row = 0; row < height; row++) {
        for (int col = 0; col < width; col++) {
            unsigned char alpha = bitmap[row * width + col];
            if (alpha > 0) {
                int px = x + xoff + col;
                int py = y + baseline + yoff + row;

                if (px >= 0 && px < screen_width && py >= 0 && py < screen_height) {
                    uint16_t *dst = &framebuffer[py * screen_width + px];
                    if (alpha >= 255) {
                        *dst = color;
                    } else {
                        uint16_t bg = *dst;
                        int fr = (color >> 11) & 0x1F, fg = (color >> 5) & 0x3F, fb = color & 0x1F;
                        int br = (bg >> 11) & 0x1F, bgc = (bg >> 5) & 0x3F, bb = bg & 0x1F;
                        int ia = 255 - alpha;
                        int rr = (fr * alpha + br * ia) / 255;
                        int rg = (fg * alpha + bgc * ia) / 255;
                        int rb = (fb * alpha + bb * ia) / 255;
                        *dst = (uint16_t)((rr << 11) | (rg << 5) | rb);
                    }
                }
            }
        }
    }
}

/* Vertical metrics for centering: baseline = pixels from glyph-cell top down to
 * the baseline; cap_height = pixel height of capital letters (all text is
 * uppercased, so the visible ink is the cap band [baseline-cap_height, baseline]). */
void font_cap_metrics(int *baseline_out, int *cap_height_out) {
    int baseline = 0, cap = 0;
    if (font_loaded) {
        int ascent, descent, line_gap;
        stbtt_GetFontVMetrics(&font_info, &ascent, &descent, &line_gap);
        baseline = (int)(ascent * font_scale);
        int gi = stbtt_FindGlyphIndex(&font_info, 'H');
        int x0, y0, x1, y1;
        if (gi && stbtt_GetGlyphBox(&font_info, gi, &x0, &y0, &x1, &y1))
            cap = (int)((y1 - y0) * font_scale);
        else
            cap = baseline;
    }
    if (baseline_out)   *baseline_out = baseline;
    if (cap_height_out) *cap_height_out = cap;
}

void font_draw_text(uint16_t *framebuffer, int screen_width, int screen_height,
                   int x, int y, const char *text, uint16_t color) {
    if (!font_loaded || !framebuffer || !text) return;

    int start_x = x;
    int prev_codepoint = 0;

    while (*text) {
        if (*text == '\n') {
            y += FONT_SIZE + 4;  // Line spacing
            x = start_x;
            text++;
            prev_codepoint = 0;
            continue;
        }

        char c = *text;

        // Convert to uppercase
        if (c >= 'a' && c <= 'z') {
            c = c - 'a' + 'A';
        }

        // Get glyph index
        int glyph_index = stbtt_FindGlyphIndex(&font_info, c);

        if (glyph_index != 0) {
            // Get advance width and left side bearing
            int advance_width, left_side_bearing;
            stbtt_GetGlyphHMetrics(&font_info, glyph_index, &advance_width, &left_side_bearing);

            // Apply kerning if we have a previous character
            if (prev_codepoint != 0) {
                int kern = stbtt_GetGlyphKernAdvance(&font_info, prev_codepoint, glyph_index);
                x += (int)(kern * font_scale);
            }

            // Draw the character
            font_draw_char(framebuffer, screen_width, screen_height, x, y, c, color);

            // Advance cursor
            x += (int)(advance_width * font_scale);
            prev_codepoint = glyph_index;
        } else {
            // Space or unknown character
            x += FONT_CHAR_SPACING;
            prev_codepoint = 0;
        }

        text++;
    }
}

int font_measure_text(const char *text) {
    if (!text || !font_loaded) return 0;

    int width = 0;
    int prev_codepoint = 0;

    while (*text) {
        // Skip newlines
        if (*text == '\n') {
            text++;
            prev_codepoint = 0;
            continue;
        }

        char c = *text;

        // Convert to uppercase
        if (c >= 'a' && c <= 'z') {
            c = c - 'a' + 'A';
        }

        // Get glyph index
        int glyph_index = stbtt_FindGlyphIndex(&font_info, c);

        if (glyph_index != 0) {
            // Get advance width
            int advance_width, left_side_bearing;
            stbtt_GetGlyphHMetrics(&font_info, glyph_index, &advance_width, &left_side_bearing);

            // Apply kerning if we have a previous character
            if (prev_codepoint != 0) {
                int kern = stbtt_GetGlyphKernAdvance(&font_info, prev_codepoint, glyph_index);
                width += (int)(kern * font_scale);
            }

            // Add character width
            width += (int)(advance_width * font_scale);
            prev_codepoint = glyph_index;
        } else {
            // Space or unknown character
            width += FONT_CHAR_SPACING;
            prev_codepoint = 0;
        }

        text++;
    }

    return width;
}
