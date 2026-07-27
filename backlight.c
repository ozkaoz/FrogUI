#include "backlight.h"

#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <stdio.h>

/* cubevol's persistentmem backlight slot (reverse-engineered from cubevol's
 * sysdata_get/set_backlight_value). Struct + ioctl cmds must match exactly. */
struct pmem_req { unsigned short flag, id, len, pad; void *buf; };
#define PMEM_GET_BACKLIGHT 0x400c2602u
#define PMEM_SET_BACKLIGHT 0x800c2603u

/* Level (0..100) → raw backlight byte (23..255). Matches cubevol's own curve so
 * the value we store in persistentmem is what cubevol would store itself. */
static int backlight_raw(int level) {
    if (level < 0)   level = 0;
    if (level > 100) level = 100;
    int out = (level < 43) ? (level + 23) : (level * level / 43 + 23);
    if (out > 255) out = 255;
    return out;
}

/* Read cubevol's stored raw backlight from persistentmem (slot 30), or -1. */
int cube_pmem_backlight_read(void) {
    int fd = open("/dev/persistentmem", O_RDWR);
    if (fd < 0) return -1;
    int value = 0;
    struct pmem_req req = { 1, 30, 1, 0, &value };
    int rv = ioctl(fd, PMEM_GET_BACKLIGHT, &req);
    close(fd);
    if (rv < 0) return -1;
    return value & 0xFF;
}

/* Sync cubevol's stored raw backlight so its DELAYED startup apply already shows
 * the right brightness (no flash to our re-assert). persistentmem is EEPROM-like:
 * write ONLY on real change, never per frame. No-op if already correct. */
void cube_pmem_backlight_sync(int level) {
    int raw = backlight_raw(level);
    if (cube_pmem_backlight_read() == raw) return;   /* already in sync */
    int fd = open("/dev/persistentmem", O_RDWR);
    if (fd < 0) return;
    int value = raw;
    struct pmem_req req = { 1, 30, 1, 0, &value };
    (void)!ioctl(fd, PMEM_SET_BACKLIGHT, &req);
    close(fd);
}

void cube_set_backlight(int level) {
    int out = backlight_raw(level);
    int fd = open("/dev/backlight", O_RDWR);
    if (fd < 0) return;
    (void)!write(fd, &out, sizeof(int));
    close(fd);
}

#include <stdlib.h>

void fb1_set_visible(int visible) {
    if (!visible) return;
    /* Do NOT kill/restart cubevol: it owns the volume-button gpio, and killing it
     * (even with a respawn) drops volume control. cubevol stays alive from boot
     * and repaints the battery/volume OSD on its own next poll (charge-% tick or
     * a volume press), so the OSD returns without a restart. Only respawn if it
     * somehow died, so volume always works. */
    if (system("pidof cubevol >/dev/null 2>&1") != 0)
        system("/usr/bin/cubevol &");
}
