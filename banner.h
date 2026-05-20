#pragma once
#include <stdint.h>

void banner_load(const char *path);
void banner_clear(void);
void banner_render(uint16_t *framebuffer);
int  banner_is_loaded(void);
