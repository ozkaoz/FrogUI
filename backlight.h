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

/* Read cubevol's persistentmem-stored raw backlight (slot 30), -1 on failure. */
int cube_pmem_backlight_read(void);

/* Sync cubevol's persistentmem-stored backlight to `level` (0..100) so its
 * delayed startup apply shows the right brightness. Writes EEPROM only on real
 * change — call on brightness change / boot, NOT every frame. */
void cube_pmem_backlight_sync(int level);

/* Show or hide the cubevol OSD overlay (/dev/fb1: battery, volume). Frog should
 * always set this to visible on init — picoarch/standalone game code blanks it
 * during gameplay and restores on exit, but if those crash, FrogUI's explicit
 * unblank ensures the user sees the battery icon. */
void fb1_set_visible(int visible);

#endif
