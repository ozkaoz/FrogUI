#include "backlight.h"

#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>

void cube_set_backlight(int level) {
    if (level < 0)   level = 0;
    if (level > 100) level = 100;
    int out = (level < 43) ? (level + 23)
                           : (level * level / 43 + 23);
    if (out > 255) out = 255;
    int fd = open("/dev/backlight", O_RDWR);
    if (fd < 0) return;
    (void)!write(fd, &out, sizeof(int));
    close(fd);
}

#include <stdlib.h>

void fb1_set_visible(int visible) {
    if (!visible) return;
    /* Restart cubevol — fresh process re-draws battery + volume on /dev/fb1.
     * Shmem at /tmp/joy_key survives the restart (kernel-owned). */
    system("killall cubevol 2>/dev/null; /usr/bin/cubevol &");
    usleep(50000);
}
