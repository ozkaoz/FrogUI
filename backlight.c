#include "backlight.h"

#include <fcntl.h>
#include <unistd.h>

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
