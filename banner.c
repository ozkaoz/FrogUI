#include "banner.h"
#include "render.h"

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

static uint16_t banner_buf[SCREEN_WIDTH * SCREEN_HEIGHT];
static int banner_loaded = 0;

static inline uint16_t rgb_to_565(unsigned char r, unsigned char g, unsigned char b) {
    return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
}

void banner_load(const char *path) {
    int w, h, ch;
    unsigned char *img = stbi_load(path, &w, &h, &ch, 3);
    if (!img) {
        banner_loaded = 0;
        return;
    }

    /* Nearest-neighbor scale to full screen */
    int sw = SCREEN_WIDTH, sh = SCREEN_HEIGHT;
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
}

void banner_render(uint16_t *framebuffer) {
    if (!banner_loaded || !framebuffer) return;
    int n = SCREEN_WIDTH * SCREEN_HEIGHT;
    for (int i = 0; i < n; i++)
        framebuffer[i] = banner_buf[i];
}

int banner_is_loaded(void) {
    return banner_loaded;
}
