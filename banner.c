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
static int fade_alpha = 256;          /* 0..256: 256 = fully showing current */

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
    if (!banner_anim) fade_alpha = 256;
}

void banner_load(const char *path) {
    int w, h, ch;
    unsigned char *img = stbi_load(path, &w, &h, &ch, 3);
    if (!img) {
        banner_loaded = 0;
        return;
    }

    int sw = SCREEN_WIDTH, sh = SCREEN_HEIGHT;
    if (!banner_buf)
        banner_buf = malloc(sw * sh * sizeof(uint16_t));
    if (!banner_buf) {
        stbi_image_free(img);
        banner_loaded = 0;
        return;
    }

    /* snapshot the outgoing banner for crossfade */
    if (banner_anim && banner_loaded) {
        if (!banner_prev)
            banner_prev = malloc(sw * sh * sizeof(uint16_t));
        if (banner_prev) {
            memcpy(banner_prev, banner_buf, sw * sh * sizeof(uint16_t));
            fade_alpha = 0;
        } else {
            fade_alpha = 256;
        }
    } else {
        fade_alpha = 256;
    }

    for (int y = 0; y < sh; y++) {
        int sy = (y * h) / sh;
        for (int x = 0; x < sw; x++) {
            int sx = (x * w) / sw;
            unsigned char *p = img + (sy * w + sx) * 3;
            banner_buf[y * sw + x] = rgb_to_565(p[0], p[1], p[2]);
        }
    }

    stbi_image_free(img);
    banner_loaded = 1;
}

void banner_clear(void) {
    banner_loaded = 0;
    fade_alpha = 256;
}

void banner_render(uint16_t *framebuffer) {
    if (!banner_loaded || !banner_buf || !framebuffer) return;
    int n = SCREEN_WIDTH * SCREEN_HEIGHT;

    if (fade_alpha >= 256 || !banner_prev) {
        memcpy(framebuffer, banner_buf, n * sizeof(uint16_t));
        return;
    }

    int t = fade_alpha;
    for (int i = 0; i < n; i++)
        framebuffer[i] = blend565(banner_prev[i], banner_buf[i], t);

    /* ease-out: snappy start, soft finish (iPhone-like). ~3 frames. */
    int step = (256 - fade_alpha) / 2;
    if (step < 96) step = 96;
    fade_alpha += step;
    if (fade_alpha > 256) fade_alpha = 256;
}

int banner_is_loaded(void) {
    return banner_loaded;
}
