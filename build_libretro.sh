#!/bin/sh
set -e
MIPS="$HOME/sf3000-work/sf3000toolchain/mipsel-buildroot-linux-gnu_sdk-buildroot/opt/ext-toolchain/bin/mips-mti-linux-gnu-"
SYSROOT="$HOME/sf3000-work/sf3000toolchain/mipsel-buildroot-linux-gnu_sdk-buildroot/mipsel-buildroot-linux-gnu/sysroot"
CC="${MIPS}gcc"
cd /home/tomaszz/sf3000-work/FrogUI

CFLAGS="-mips32r2 -march=mips32r2 -mtune=24kc -mfp32 -mhard-float -mlong-calls -EL --sysroot=$SYSROOT -fPIC -G0 -Wall -I./ -I$SYSROOT/usr/include -Ofast -DPLATFORM_SF3000 -DNDEBUG -D__LIBRETRO__ -DSCREEN_WIDTH=640 -DSCREEN_HEIGHT=480 -DUI_SCALE=133"

for src in frogui_libretro.c render.c font.c recent_games.c settings.c theme.c favorites.c banner.c backlight.c input.c; do
    obj="${src%.c}.lo"
    echo "CC $src"
    $CC $CFLAGS -c -o "$obj" "$src"
done

echo "Linking..."
$CC -mips32r2 -mhard-float -mfp32 -EL -fPIC -shared -nostdlib -Wl,--no-undefined \
    frogui_libretro.lo render.lo font.lo recent_games.lo settings.lo theme.lo favorites.lo banner.lo backlight.lo input.lo \
    -L"$SYSROOT/usr/lib" --sysroot="$SYSROOT" -lm -lc -ldl -lpthread \
    -o frogui_libretro.so

ls -la frogui_libretro.so
echo "Done."
