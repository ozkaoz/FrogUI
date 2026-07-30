#include "banner.h"
#include "render.h"
#include <stdlib.h>
#include <string.h>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

static uint16_t *banner_buf = NULL;   /* current (target) banner */
static uint16_t *banner_prev = NULL;  /* outgoing banner during crossfade */
static int banner_loaded = 0;
static int banner_anim = 1;           /* crossfade enabled */
#define FADE_FRAMES 20                /* ~330ms @ 60fps — slow, iPhone-like */
static int fade_frame = FADE_FRAMES;  /* 0..FADE_FRAMES; FADE_FRAMES = done */

static inline uint16_t rgb_to_565(unsigned char r, unsigned char g, unsigned char b) {
    return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
}

/* blend a->b by t (0..256): result = a*(256-t) + b*t */
static inline uint16_t blend565(uint16_t a, uint16_t b, int t) {
    int it = 256 - t;
    int ar = (a >> 11) & 0x1f, ag = (a >> 5) & 0x3f, ab = a & 0x1f;
    int br = (b >> 11) & 0x1f, bg = (b >> 5) & 0x3f, bb = b & 0x1f;
    int r = (ar * it + br * t) >> 8;
    int g = (ag * it + bg * t) >> 8;
    int bl = (ab * it + bb * t) >> 8;
    return (uint16_t)((r << 11) | (g << 5) | bl);
}

void banner_set_anim(int enabled) {
    banner_anim = enabled ? 1 : 0;
    if (!banner_anim) fade_frame = FADE_FRAMES;
}

/* LRU cache of fully-decoded + transformed panel-res backgrounds, keyed by
 * "path|mode". Root scrolling flips between folder backgrounds constantly; without
 * this each step re-decoded a PNG. A cache hit is just a memcpy. */
#define BCACHE_N 6
static struct { char key[600]; uint16_t *buf; int valid; } bcache[BCACHE_N];
static int bcache_next;
static int bcache_find(const char *key) {
    for (int i = 0; i < BCACHE_N; i++)
        if (bcache[i].valid && strcmp(bcache[i].key, key) == 0) return i;
    return -1;
}
static void bcache_store(const char *key, const uint16_t *src, int npix) {
    int i = bcache_next; bcache_next = (bcache_next + 1) % BCACHE_N;
    if (!bcache[i].buf) bcache[i].buf = malloc((size_t)npix * 2);
    if (!bcache[i].buf) { bcache[i].valid = 0; return; }
    memcpy(bcache[i].buf, src, (size_t)npix * 2);
    strncpy(bcache[i].key, key, sizeof bcache[i].key - 1);
    bcache[i].key[sizeof bcache[i].key - 1] = 0;
    bcache[i].valid = 1;
}

static void banner_snapshot_for_fade(int sw, int sh) {
    if (banner_anim && banner_loaded && banner_buf) {
        if (!banner_prev) banner_prev = malloc(sw * sh * sizeof(uint16_t));
        if (banner_prev) { memcpy(banner_prev, banner_buf, sw * sh * sizeof(uint16_t)); fade_frame = 0; }
        else fade_frame = FADE_FRAMES;
    } else {
        fade_frame = FADE_FRAMES;
    }
}

static void banner_load_common(const char *path, int mode, uint16_t bg) {
    int sw = SCREEN_WIDTH, sh = SCREEN_HEIGHT, npix = sw * sh;
    char key[600];
    snprintf(key, sizeof key, "%s|%d", path, mode);

    if (!banner_buf) banner_buf = malloc(npix * sizeof(uint16_t));
    if (!banner_buf) { banner_loaded = 0; return; }

    /* Cache hit: no decode, just fade-snapshot + copy the cached result. */
    int ci = bcache_find(key);
    if (ci >= 0) {
        banner_snapshot_for_fade(sw, sh);
        memcpy(banner_buf, bcache[ci].buf, (size_t)npix * 2);
        banner_loaded = 1;
        return;
    }

    int w, h, ch;
    unsigned char *img = stbi_load(path, &w, &h, &ch, 3);
    if (!img || w <= 0 || h <= 0) {
        if (img) stbi_image_free(img);
        banner_loaded = 0;
        return;
    }

    banner_snapshot_for_fade(sw, sh);

    /* Placement: dst rect [dx0,dx0+dw) x [dy0,dy0+dh) that the (scaled) image
     * covers; pixels outside it get bg. For FILL the rect exceeds the screen
     * (crop); for FIT/CENTER it's inset (letterbox). */
    int dx0 = 0, dy0 = 0, dw = sw, dh = sh;
    if (mode == BANNER_FIT_FILL) {
        /* cover: scale by the larger ratio, center, crop */
        if ((long)w * sh > (long)h * sw) { dh = sh; dw = (int)((long)w * sh / h); }
        else                             { dw = sw; dh = (int)((long)h * sw / w); }
        dx0 = (sw - dw) / 2; dy0 = (sh - dh) / 2;
    } else if (mode == BANNER_FIT_FIT) {
        /* contain: scale by the smaller ratio, center, letterbox */
        if ((long)w * sh > (long)h * sw) { dw = sw; dh = (int)((long)h * sw / w); }
        else                             { dh = sh; dw = (int)((long)w * sh / h); }
        dx0 = (sw - dw) / 2; dy0 = (sh - dh) / 2;
    } else if (mode == BANNER_FIT_CENTER) {
        dw = w; dh = h; dx0 = (sw - w) / 2; dy0 = (sh - h) / 2;
    }
    /* STRETCH: dst == screen. TILE handled inline below. */

    for (int y = 0; y < sh; y++) {
        for (int x = 0; x < sw; x++) {
            int sx, sy;
            if (mode == BANNER_FIT_TILE) {
                sx = x % w; sy = y % h;
            } else if (x < dx0 || x >= dx0 + dw || y < dy0 || y >= dy0 + dh) {
                banner_buf[y * sw + x] = bg;      /* outside image (letterbox/center) */
                continue;
            } else {
                sx = (int)((long)(x - dx0) * w / dw);
                sy = (int)((long)(y - dy0) * h / dh);
                if (sx < 0) sx = 0; if (sx >= w) sx = w - 1;
                if (sy < 0) sy = 0; if (sy >= h) sy = h - 1;
            }
            unsigned char *p = img + (sy * w + sx) * 3;
            banner_buf[y * sw + x] = rgb_to_565(p[0], p[1], p[2]);
        }
    }

    stbi_image_free(img);
    banner_loaded = 1;
    bcache_store(key, banner_buf, npix);   /* cache the transformed result */
}

void banner_load(const char *path) {
    banner_load_common(path, BANNER_FIT_STRETCH, 0);   /* system art: stretch, as before */
}
void banner_load_fit(const char *path, int mode, uint16_t bg) {
    banner_load_common(path, mode, bg);
}

void banner_clear(void) {
    banner_loaded = 0;
    fade_frame = FADE_FRAMES;
}

void banner_render(uint16_t *framebuffer) {
    if (!banner_loaded || !banner_buf || !framebuffer) return;
    int n = SCREEN_WIDTH * SCREEN_HEIGHT;

    if (fade_frame >= FADE_FRAMES || !banner_prev) {
        memcpy(framebuffer, banner_buf, n * sizeof(uint16_t));
        return;
    }

    /* Advance one frame, ease-in-out (smoothstep) for a slow, soft iPhone-like
     * crossfade. p: 0..1 progress; e = 3p²−2p³. Only the background animates —
     * the selection/text is drawn separately and updates immediately. */
    fade_frame++;
    float p = (float)fade_frame / (float)FADE_FRAMES;
    if (p > 1.0f) p = 1.0f;
    float e = p * p * (3.0f - 2.0f * p);
    int t = (int)(e * 256.0f + 0.5f);
    if (t > 256) t = 256;

    for (int i = 0; i < n; i++)
        framebuffer[i] = blend565(banner_prev[i], banner_buf[i], t);
}

int banner_is_loaded(void) {
    return banner_loaded;
}

/* True while a crossfade is in progress (background still changing each frame). */
int banner_is_animating(void) {
    return banner_anim && banner_prev && fade_frame < FADE_FRAMES;
}

/* Repaint a rect with the current background: the banner image slice when one
 * is loaded, otherwise the given fallback color. Lets overlays (box-art panel)
 * cover list-text overflow without drawing an opaque card. */
void banner_fill_region(uint16_t *fb, int x, int y, int w, int h, uint16_t fallback) {
    if (!fb) return;
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > SCREEN_WIDTH)  w = SCREEN_WIDTH - x;
    if (y + h > SCREEN_HEIGHT) h = SCREEN_HEIGHT - y;
    if (w <= 0 || h <= 0) return;
    if (banner_loaded && banner_buf) {
        for (int yy = 0; yy < h; yy++)
            memcpy(fb + (size_t)(y + yy) * SCREEN_WIDTH + x,
                   banner_buf + (size_t)(y + yy) * SCREEN_WIDTH + x,
                   (size_t)w * sizeof(uint16_t));
    } else {
        for (int yy = 0; yy < h; yy++) {
            uint16_t *row = fb + (size_t)(y + yy) * SCREEN_WIDTH + x;
            for (int xx = 0; xx < w; xx++) row[xx] = fallback;
        }
    }
}
