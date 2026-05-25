#ifndef BACKLIGHT_H
#define BACKLIGHT_H

/* Set screen brightness via /dev/backlight. level: 0..100 logical.
 * Mirrors rkgame's cube_set_backlight_brightness curve:
 *   level <  43: out = level + 23      (linear, 23..65)
 *   level 43..100: out = level*level/43 + 23 (quadratic, 65..255)
 *   level >100: clamped to 255
 * The 4-byte int is written to /dev/backlight (O_RDWR). Silent if device
 * unavailable. */
void cube_set_backlight(int level);

#endif
