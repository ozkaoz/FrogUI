#pragma once
#include <stdint.h>

void banner_load(const char *path);
void banner_clear(void);
void banner_render(uint16_t *framebuffer);
int  banner_is_loaded(void);
void banner_set_anim(int enabled);
void banner_fill_region(uint16_t *fb, int x, int y, int w, int h, uint16_t fallback);
