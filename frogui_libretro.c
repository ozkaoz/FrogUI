/*
 * frogui_libretro.c - FrogUI as a libretro core for SF3000
 *
 * Picoarch loads this core and handles ALL display/input/SDL init.
 * FrogUI renders its menu into a RGB565 buffer and passes it to video_cb.
 * When user picks a game, write /tmp/frogui_launch.txt and signal SHUTDOWN.
 * icube reads that file and launches picoarch with the real game core.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <unistd.h>
#include <dirent.h>
#include <stdbool.h>

#include "libretro.h"
#include "font.h"
#include "render.h"
#include "theme.h"
#include "recent_games.h"
#include "favorites.h"
#include "settings.h"
#include "banner.h"
#include "backlight.h"

#define SDCARD_BASE  "/mnt/sdcard"
#define CORES_PATH   SDCARD_BASE "/cubegm/cores"
#define ROMS_PATH    SDCARD_BASE "/roms"
#define LAUNCH_FILE  "/tmp/frogui_launch.txt"
#define PCSX4ALL_BIN SDCARD_BASE "/cubegm/pcsx4all"

/* Console → core mapping (folder name → libretro .so)
 * Folder names match /mnt/sdcard/roms/ subdirectories (gb300_multicore convention). */
typedef struct { const char *console_name; const char *core_path; } ConsoleMapping;
static const ConsoleMapping console_mappings[] = {
    /* NES */
    {"nes",    CORES_PATH "/fceumm_libretro.so"},
    {"nesq",   CORES_PATH "/quicknes_libretro.so"},
    {"nest",   CORES_PATH "/nestopia_libretro.so"},
    {"FC",     CORES_PATH "/fceumm_libretro.so"},
    {"NES",    CORES_PATH "/quicknes_libretro.so"},
    /* SNES */
    {"snes",   CORES_PATH "/snes9x2005_plus_libretro.so"},
    {"snes02", CORES_PATH "/snes9x2002_libretro.so"},
    {"SFC",    CORES_PATH "/snes9x2005_plus_libretro.so"},
    /* Game Boy */
    {"gb",     CORES_PATH "/gambatte_libretro.so"},
    {"gbgb",   CORES_PATH "/gearboy_libretro.so"},
    {"gbb",    CORES_PATH "/tgbdual_libretro.so"},
    {"dblcherrygb", CORES_PATH "/gambatte_libretro.so"},
    /* GBA */
    {"gba",    CORES_PATH "/gpsp_libretro.so"},
    {"GBA",    CORES_PATH "/gpsp_libretro.so"},
    {"gbav",   CORES_PATH "/vba_next_libretro.so"},
    {"mgba",   CORES_PATH "/mgba_libretro.so"},
    {"gbaf",   CORES_PATH "/mgba_libretro.so"},
    {"gbaf",   CORES_PATH "/mgba_libretro.so"},
    {"GBA",    CORES_PATH "/gpsp_libretro.so"},
    /* Sega */
    {"sega",   CORES_PATH "/picodrive_libretro.so"},
    {"gg",     CORES_PATH "/gearsystem_libretro.so"},
    {"gpgx",   CORES_PATH "/genesis_plus_gx_libretro.so"},
    {"MD",     CORES_PATH "/picodrive_libretro.so"},
    {"SMS",    CORES_PATH "/picodrive_libretro.so"},
    {"GG",     CORES_PATH "/gearsystem_libretro.so"},
    /* Atari */
    {"a26",    CORES_PATH "/stella2014_libretro.so"},
    {"a5200",  CORES_PATH "/a5200_libretro.so"},
    {"a78",    CORES_PATH "/prosystem_libretro.so"},
    {"a800",   CORES_PATH "/atari800_libretro.so"},
    /* Lynx */
    {"lnx",    CORES_PATH "/handy_libretro.so"},
    /* PC Engine */
    {"pce",    CORES_PATH "/mednafen_pce_fast_libretro.so"},
    {"pcesgx", CORES_PATH "/mednafen_supergrafx_libretro.so"},
    /* Neo Geo Pocket */
    {"ngpc",   CORES_PATH "/race_libretro.so"},
    /* WonderSwan */
    {"wswan",  CORES_PATH "/mednafen_wswan_libretro.so"},
    {"wsv",    CORES_PATH "/potator_libretro.so"},
    /* Virtual Boy */
    {"vb",     CORES_PATH "/mednafen_vb_libretro.so"},
    /* PC-FX */
    {"pcfx",   CORES_PATH "/mednafen_pcfx_libretro.so"},
    /* PC-8800 */
    {"pc8800", CORES_PATH "/quasi88_libretro.so"},
    /* MSX */
    {"msx",    CORES_PATH "/bluemsx_libretro.so"},
    /* C64 */
    {"c64",    CORES_PATH "/vice_x64_libretro.so"},
    {"c64sc",  CORES_PATH "/vice_x64sc_libretro.so"},
    {"c64f",   CORES_PATH "/frodo_libretro.so"},
    {"c64fc",  CORES_PATH "/frodo_libretro.so"},
    {"vic20",  CORES_PATH "/vice_xvic_libretro.so"},
    /* Amstrad */
    {"amstrad",  CORES_PATH "/crocods_libretro.so"},
    {"amstradb", CORES_PATH "/cap32_libretro.so"},
    /* ZX Spectrum */
    {"spec",   CORES_PATH "/fuse_libretro.so"},
    {"zx81",   CORES_PATH "/81_libretro.so"},
    /* Coleco */
    {"col",    CORES_PATH "/gearcoleco_libretro.so"},
    /* Ports / games */
    {"Quake",  CORES_PATH "/tyrquake_libretro.so"},
    {"outrun", CORES_PATH "/cannonball_libretro.so"},
    {"wolf3d", CORES_PATH "/ecwolf_libretro.so"},
    {"prboom", CORES_PATH "/prboom_libretro.so"},
    {"cavestory", CORES_PATH "/nxengine_libretro.so"},
    {"flashback", CORES_PATH "/reminiscence_libretro.so"},
    {"xrick",  CORES_PATH "/xrick_libretro.so"},
    {"gw",     CORES_PATH "/gw_libretro.so"},
    {"jnb",    CORES_PATH "/jumpnbump_libretro.so"},
    /* Misc */
    {"fake08", CORES_PATH "/fake08_libretro.so"},
    {"lowres-nx", CORES_PATH "/lowresnx_libretro.so"},
    {"gme",    CORES_PATH "/gme_libretro.so"},
    {"m2k",    CORES_PATH "/mame2000_libretro.so"},
    {"pokem",  CORES_PATH "/pokemini_libretro.so"},
    {"int",    CORES_PATH "/freeintv_libretro.so"},
    {"fcf",    CORES_PATH "/freechaf_libretro.so"},
    {"cdg",    CORES_PATH "/pocketcdg_libretro.so"},
    {"chip8",  CORES_PATH "/jaxe_libretro.so"},
    {"retro8", CORES_PATH "/retro8_libretro.so"},
    {"arduboy",CORES_PATH "/arduous_libretro.so"},
    {"vec",    CORES_PATH "/vecx_libretro.so"},
    {"thom",   CORES_PATH "/theodore_libretro.so"},
    {"o2em",   CORES_PATH "/o2em_libretro.so"},
    {"xmil",   CORES_PATH "/x68k_libretro.so"},
    {"geolith",CORES_PATH "/geolith_libretro.so"},
    {"gong",   CORES_PATH "/gong_libretro.so"},
    {"vapor",  CORES_PATH "/vaporspec_libretro.so"},
    {"amiga",  CORES_PATH "/uae_libretro.so"},
    {"atari-st", CORES_PATH "/castaway_libretro.so"},
    /* PlayStation */
    {"ps1",    CORES_PATH "/pcsx_rearmed_libretro.so"},
    {"psx",    CORES_PATH "/pcsx_rearmed_libretro.so"},
    {"PS",     CORES_PATH "/pcsx_rearmed_libretro.so"},
    {NULL, NULL}
};

static const char* get_core_for_folder(const char *folder) {
    if (!folder) return NULL;
    for (int i = 0; console_mappings[i].console_name; i++)
        if (strcasecmp(console_mappings[i].console_name, folder) == 0)
            return console_mappings[i].core_path;
    return NULL;
}

/* Extension fallback: when folder name doesn't match a console mapping,
 * pick a core based on the ROM file extension. */
typedef struct { const char *ext; const char *core_path; } ExtensionMapping;
static const ExtensionMapping ext_mappings[] = {
    {".nes",  CORES_PATH "/fceumm_libretro.so"},
    {".fds",  CORES_PATH "/fceumm_libretro.so"},
    {".unf",  CORES_PATH "/fceumm_libretro.so"},
    {".sfc",  CORES_PATH "/snes9x2005_plus_libretro.so"},
    {".smc",  CORES_PATH "/snes9x2005_plus_libretro.so"},
    {".gba",  CORES_PATH "/gpsp_libretro.so"},
    {".gb",   CORES_PATH "/gambatte_libretro.so"},
    {".gbc",  CORES_PATH "/gambatte_libretro.so"},
    {".md",   CORES_PATH "/picodrive_libretro.so"},
    {".smd",  CORES_PATH "/picodrive_libretro.so"},
    {".gen",  CORES_PATH "/picodrive_libretro.so"},
    {".sms",  CORES_PATH "/picodrive_libretro.so"},
    {".gg",   CORES_PATH "/gearsystem_libretro.so"},
    {".pce",  CORES_PATH "/mednafen_pce_fast_libretro.so"},
    {".sgx",  CORES_PATH "/mednafen_supergrafx_libretro.so"},
    {".lnx",  CORES_PATH "/handy_libretro.so"},
    {".lyx",  CORES_PATH "/handy_libretro.so"},
    {".ngp",  CORES_PATH "/race_libretro.so"},
    {".ngc",  CORES_PATH "/race_libretro.so"},
    {".ws",   CORES_PATH "/mednafen_wswan_libretro.so"},
    {".wsc",  CORES_PATH "/mednafen_wswan_libretro.so"},
    {".vb",   CORES_PATH "/mednafen_vb_libretro.so"},
    {".a26",  CORES_PATH "/stella2014_libretro.so"},
    {".a52",  CORES_PATH "/a5200_libretro.so"},
    {".a78",  CORES_PATH "/prosystem_libretro.so"},
    {".min",  CORES_PATH "/pokemini_libretro.so"},
    {".col",  CORES_PATH "/gearcoleco_libretro.so"},
    {".int",  CORES_PATH "/freeintv_libretro.so"},
    {".bin",  CORES_PATH "/freechaf_libretro.so"},
    {".sv",   CORES_PATH "/potator_libretro.so"},
    {".d64",  CORES_PATH "/vice_x64_libretro.so"},
    {".tap",  CORES_PATH "/fuse_libretro.so"},
    {".tzx",  CORES_PATH "/fuse_libretro.so"},
    {".dsk",  CORES_PATH "/cap32_libretro.so"},
    {".cdt",  CORES_PATH "/cap32_libretro.so"},
    {".cas",  CORES_PATH "/atari800_libretro.so"},
    {".xex",  CORES_PATH "/atari800_libretro.so"},
    {".atr",  CORES_PATH "/atari800_libretro.so"},
    {".vec",  CORES_PATH "/vecx_libretro.so"},
    {".rom",  CORES_PATH "/o2em_libretro.so"},
    {".adf",  CORES_PATH "/uae_libretro.so"},
    {".st",   CORES_PATH "/castaway_libretro.so"},
    {".msa",  CORES_PATH "/castaway_libretro.so"},
    {".cue",  CORES_PATH "/pcsx_rearmed_libretro.so"},
    {".iso",  CORES_PATH "/pcsx_rearmed_libretro.so"},
    {NULL, NULL}
};

static const char* get_core_for_extension(const char *filename) {
    if (!filename) return NULL;
    const char *dot = strrchr(filename, '.');
    if (!dot) return NULL;
    for (int i = 0; ext_mappings[i].ext; i++)
        if (strcasecmp(ext_mappings[i].ext, dot) == 0)
            return ext_mappings[i].core_path;
    return NULL;
}

/* Libretro callbacks */
static retro_video_refresh_t     video_cb     = NULL;
static retro_environment_t       environ_cb   = NULL;
static retro_input_poll_t        input_poll_cb = NULL;
static retro_input_state_t       input_state_cb = NULL;

/* App state */
#define MAX_PATH_LEN 512
#define INITIAL_ENTRIES_CAPACITY 64
#ifndef min
#define min(a,b) ((a)<(b)?(a):(b))
#endif

typedef struct { char name[256]; int is_dir; } DirEntry;

static DirEntry *entries    = NULL;
static int entry_count      = 0;
static int entry_capacity   = 0;
static char current_path[MAX_PATH_LEN] = ROMS_PATH;
static int selected_index   = 0;
static int scroll_offset    = 0;
static uint16_t *framebuffer = NULL;
static bool shutdown_requested = false;
static bool viewing_recents = false;
static bool viewing_favourites = false;

static void dbg(const char *msg) {
    FILE *f = fopen("/tmp/frogui_crash.log", "a");
    if (f) { fputs(msg, f); fputs("\n", f); fclose(f); }
    fprintf(stderr, "FROGUI_DBG: %s\n", msg);
}

/* --- Cubevol direct input (picoarch input_state_cb returns nothing on SF3000) --- */
#define CV_UP     4
#define CV_DOWN   6
#define CV_LEFT   7
#define CV_RIGHT  5
#define CV_A     13
#define CV_B     14
#define CV_X     12
#define CV_Y     15
#define CV_L     10
#define CV_R     11
#define CV_SEL    0
#define CV_START  3

static volatile uint32_t *cv_keys = NULL;
static int cv_shmid = -1;

static void cv_init(void) {
    key_t key = ftok("/tmp/joy_key", 'a');
    if (key == (key_t)-1) { dbg("cv_init: ftok failed"); return; }
    cv_shmid = shmget(key, 4, 0666);
    if (cv_shmid < 0) { dbg("cv_init: shmget failed"); return; }
    cv_keys = (volatile uint32_t *)shmat(cv_shmid, NULL, 0);
    if (cv_keys == (void *)-1) { cv_keys = NULL; dbg("cv_init: shmat failed"); return; }
    dbg("cv_init: OK");
}

static uint32_t cv_read(void) {
    return cv_keys ? (*cv_keys & 0xFFFF) : 0;
}

static bool cv_btn(uint32_t state, int bit) { return (state >> bit) & 1; }

/* --- Settings ---
 * Stored at /mnt/sdcard/frogui/settings.txt, key=value format.
 * Two options: theme + font. */
#define SETTINGS_DIR  "/mnt/sdcard/frogui"
#define SETTINGS_FILE SETTINGS_DIR "/settings.txt"

static const char *font_names[] = {"GamePocket", "Monogram"};
#define FONT_COUNT 2

static bool settings_menu_active = false;
static int settings_menu_idx = 0;       /* which row: 0=theme, 1=font, 2=brightness */
static int settings_theme_idx = 0;
static int settings_font_idx = 0;
static int settings_brightness = 75;    /* 0..100, step 5 */
#define SETTINGS_BRIGHTNESS_STEP 5
#define SETTINGS_ROW_COUNT 3

static void mkdir_p(const char *path) {
    char cmd[300];
    snprintf(cmd, sizeof(cmd), "mkdir -p '%s'", path);
    system(cmd);
}

static void settings_apply(void) {
    extern const int theme_count;
    if (settings_theme_idx < 0 || settings_theme_idx >= theme_count) settings_theme_idx = 0;
    if (settings_font_idx < 0 || settings_font_idx >= FONT_COUNT) settings_font_idx = 0;
    if (settings_brightness < 0)   settings_brightness = 0;
    if (settings_brightness > 100) settings_brightness = 100;
    theme_apply(settings_theme_idx);
    font_load_from_settings(font_names[settings_font_idx]);
    cube_set_backlight(settings_brightness);
}

static void settings_load_file(void) {
    FILE *f = fopen(SETTINGS_FILE, "r");
    if (!f) return;
    char line[256];
    extern const int theme_count;
    extern const Theme themes[];
    while (fgets(line, sizeof(line), f)) {
        char *eq = strchr(line, '=');
        if (!eq) continue;
        *eq = '\0';
        char *val = eq + 1;
        char *nl = strchr(val, '\n'); if (nl) *nl = '\0';
        char *cr = strchr(val, '\r'); if (cr) *cr = '\0';
        if (strcmp(line, "theme") == 0) {
            for (int i = 0; i < theme_count; i++)
                if (strcmp(themes[i].name, val) == 0) { settings_theme_idx = i; break; }
        } else if (strcmp(line, "font") == 0) {
            for (int i = 0; i < FONT_COUNT; i++)
                if (strcmp(font_names[i], val) == 0) { settings_font_idx = i; break; }
        } else if (strcmp(line, "brightness") == 0) {
            settings_brightness = atoi(val);
        }
    }
    fclose(f);
}

static void settings_save_file(void) {
    extern const Theme themes[];
    mkdir_p(SETTINGS_DIR);
    FILE *f = fopen(SETTINGS_FILE, "w");
    if (!f) { dbg("settings save: fopen failed"); return; }
    fprintf(f, "theme=%s\n", themes[settings_theme_idx].name);
    fprintf(f, "font=%s\n", font_names[settings_font_idx]);
    fprintf(f, "brightness=%d\n", settings_brightness);
    fflush(f);
    fsync(fileno(f));
    fclose(f);
}

static const char* get_basename(const char *path) {
    const char *b = strrchr(path, '/');
    return b ? b+1 : path;
}

static const char* get_console_folder(const char *path) {
    size_t roms_len = strlen(ROMS_PATH);
    if (strncmp(path, ROMS_PATH, roms_len) == 0) {
        const char *sub = path + roms_len;
        if (*sub == '/') {
            sub++;
        }
        static char console[64];
        int i = 0;
        while (sub[i] != '\0' && sub[i] != '/' && i < 63) {
            console[i] = sub[i];
            i++;
        }
        console[i] = '\0';
        return console;
    }
    return NULL;
}

#define SETTINGS_ENTRY_NAME    ">> Settings"
#define RECENTS_ENTRY_NAME     ">> Recents"
#define FAVOURITES_ENTRY_NAME  ">> Favourites"

#define BANNER_DIR SDCARD_BASE "/frogui"

static void load_banner_for_view(const char *path, bool is_recents, bool is_favourites) {
    char img[512];
    const char *exts[] = { "png", "jpg", "jpeg", "bmp", NULL };
    if (is_recents) {
        for (int i = 0; exts[i]; i++) {
            snprintf(img, sizeof(img), BANNER_DIR "/recents.%s", exts[i]);
            if (access(img, R_OK) == 0) { banner_load(img); return; }
        }
    } else if (is_favourites) {
        for (int i = 0; exts[i]; i++) {
            snprintf(img, sizeof(img), BANNER_DIR "/favourites.%s", exts[i]);
            if (access(img, R_OK) == 0) { banner_load(img); return; }
        }
    } else {
        const char *base = (path && *path) ? strrchr(path, '/') : NULL;
        const char *name = base ? base + 1 : (path ? path : "main");
        if (!name || !*name) name = "main";
        for (int i = 0; exts[i]; i++) {
            snprintf(img, sizeof(img), BANNER_DIR "/%s.%s", name, exts[i]);
            if (access(img, R_OK) == 0) { banner_load(img); return; }
        }
        /* Fallback: try main.png for any view that has no folder-specific image */
        for (int i = 0; exts[i]; i++) {
            snprintf(img, sizeof(img), BANNER_DIR "/main.%s", exts[i]);
            if (access(img, R_OK) == 0) { banner_load(img); return; }
        }
    }
    banner_clear();
}

static void scan_directory(const char *path) {
    DIR *dir = opendir(path);
    if (!dir) return;
    entry_count = 0;
    struct dirent *e;
    while ((e = readdir(dir)) != NULL) {
        if (e->d_name[0] == '.') continue;
        struct stat st;
        char full[MAX_PATH_LEN];
        snprintf(full, sizeof(full), "%s/%s", path, e->d_name);
        if (stat(full, &st) != 0) continue;
        if (!S_ISDIR(st.st_mode)) {
            const char *ext = strrchr(e->d_name, '.');
            if (ext && (strcasecmp(ext,".csv")==0 || strcasecmp(ext,".txt")==0 ||
                        strcasecmp(ext,".xml")==0 || strcasecmp(ext,".jpg")==0 ||
                        strcasecmp(ext,".png")==0)) continue;
        }
        if (entry_count >= entry_capacity) {
            entry_capacity = entry_capacity ? entry_capacity*2 : INITIAL_ENTRIES_CAPACITY;
            entries = realloc(entries, entry_capacity * sizeof(DirEntry));
            if (!entries) { closedir(dir); return; }
        }
        strncpy(entries[entry_count].name, e->d_name, 255);
        entries[entry_count].name[255] = '\0';
        entries[entry_count].is_dir = S_ISDIR(st.st_mode);
        entry_count++;
    }
    closedir(dir);
    /* Sort: dirs first, then alpha */
    for (int i = 0; i < entry_count-1; i++)
        for (int j = i+1; j < entry_count; j++)
            if (entries[i].is_dir < entries[j].is_dir ||
                (entries[i].is_dir == entries[j].is_dir &&
                 strcasecmp(entries[i].name, entries[j].name) > 0)) {
                DirEntry tmp = entries[i]; entries[i] = entries[j]; entries[j] = tmp;
            }
    /* Append Settings at end, prepend Recents+Favourites at top */
    if (strcmp(path, ROMS_PATH) == 0) {
        int has_recents = recent_games_get_count() > 0 ? 1 : 0;
        int has_favs    = favorites_get_count()    > 0 ? 1 : 0;
        int extras = 1 + has_recents + has_favs;
        while (entry_count + extras > entry_capacity) {
            entry_capacity = entry_capacity ? entry_capacity*2 : INITIAL_ENTRIES_CAPACITY;
            entries = realloc(entries, entry_capacity * sizeof(DirEntry));
            if (!entries) goto done;
        }
        /* Append >> Settings last */
        strncpy(entries[entry_count].name, SETTINGS_ENTRY_NAME, 255);
        entries[entry_count].name[255] = '\0';
        entries[entry_count].is_dir = 0;
        entry_count++;
        /* Prepend >> Favourites then >> Recents (Recents ends up at index 0) */
        if (has_favs) {
            memmove(&entries[1], &entries[0], entry_count * sizeof(DirEntry));
            strncpy(entries[0].name, FAVOURITES_ENTRY_NAME, 255);
            entries[0].name[255] = '\0';
            entries[0].is_dir = 0;
            entry_count++;
        }
        if (has_recents) {
            memmove(&entries[1], &entries[0], entry_count * sizeof(DirEntry));
            strncpy(entries[0].name, RECENTS_ENTRY_NAME, 255);
            entries[0].name[255] = '\0';
            entries[0].is_dir = 0;
            entry_count++;
        }
    }
done:
    viewing_recents = false;
    viewing_favourites = false;
    selected_index = 0;
    scroll_offset  = 0;
}

static void request_game_launch(const char *core_path, const char *rom_path) {
    FILE *f = fopen(LAUNCH_FILE, "w");
    if (!f) { dbg("failed to write launch file"); return; }
    fprintf(f, "%s\n%s\n", core_path, rom_path);
    fclose(f);
    /* Record in recent games history */
    const char *rom_base = strrchr(rom_path, '/');
    rom_base = rom_base ? rom_base + 1 : rom_path;
    char game_name[256];
    strncpy(game_name, rom_base, sizeof(game_name) - 1);
    game_name[sizeof(game_name) - 1] = '\0';
    char *dot = strrchr(game_name, '.');
    if (dot) *dot = '\0';
    recent_games_add(core_path, game_name, rom_path);
    sync(); /* flush FAT32 before exec */
    dbg("launch file written, requesting shutdown");
    if (environ_cb) environ_cb(RETRO_ENVIRONMENT_SHUTDOWN, NULL);
}

static void request_standalone_launch(const char *bin_path, const char *rom_path) {
    dbg("standalone_launch: start");
    FILE *f = fopen(LAUNCH_FILE, "w");
    if (!f) { dbg("standalone_launch: fopen failed"); return; }
    fprintf(f, "standalone\n%s\n%s\n", bin_path, rom_path);
    fclose(f);
    dbg("standalone_launch: file written");
    const char *rom_base = strrchr(rom_path, '/');
    rom_base = rom_base ? rom_base + 1 : rom_path;
    char game_name[256];
    strncpy(game_name, rom_base, sizeof(game_name) - 1);
    game_name[sizeof(game_name) - 1] = '\0';
    char *dot = strrchr(game_name, '.');
    if (dot) *dot = '\0';
    dbg("standalone_launch: calling recent_games_add");
    recent_games_add(bin_path, game_name, rom_path);
    dbg("standalone_launch: calling environ_cb SHUTDOWN");
    if (environ_cb) environ_cb(RETRO_ENVIRONMENT_SHUTDOWN, NULL);
    dbg("standalone_launch: after environ_cb");
}

static bool is_ps1_folder(const char *folder) {
    if (!folder) return false;
    return strcasecmp(folder, "ps1") == 0 ||
           strcasecmp(folder, "psx") == 0 ||
           strcasecmp(folder, "PS")  == 0;
}

static void handle_input(void) {
    static bool a_last=false, b_last=false, up_last=false, dn_last=false;
    static bool l_last=false, r_last=false, y_last=false;

    uint32_t keys = cv_read();
    bool a  = cv_btn(keys, CV_A);
    bool b  = cv_btn(keys, CV_B);
    bool y  = cv_btn(keys, CV_Y);
    bool up = cv_btn(keys, CV_UP);
    bool dn = cv_btn(keys, CV_DOWN);
    bool lt = cv_btn(keys, CV_LEFT);
    bool rt = cv_btn(keys, CV_RIGHT);

    if (settings_menu_active) {
        extern const int theme_count;
        if (up && !up_last) settings_menu_idx = (settings_menu_idx - 1 + SETTINGS_ROW_COUNT) % SETTINGS_ROW_COUNT;
        if (dn && !dn_last) settings_menu_idx = (settings_menu_idx + 1) % SETTINGS_ROW_COUNT;
        if ((lt && !l_last) || (rt && !r_last)) {
            int delta = (rt && !r_last) ? 1 : -1;
            if (settings_menu_idx == 0) {
                settings_theme_idx = (settings_theme_idx + delta + theme_count) % theme_count;
            } else if (settings_menu_idx == 1) {
                settings_font_idx = (settings_font_idx + delta + FONT_COUNT) % FONT_COUNT;
            } else {
                settings_brightness += delta * SETTINGS_BRIGHTNESS_STEP;
                if (settings_brightness < 0)   settings_brightness = 0;
                if (settings_brightness > 100) settings_brightness = 100;
            }
            settings_apply();
        }
        if ((a && !a_last) || (b && !b_last)) {
            settings_save_file();
            settings_menu_active = false;
        }
        a_last=a; b_last=b; up_last=up; dn_last=dn; l_last=lt; r_last=rt; y_last=y;
        return;
    }

    /* Y: toggle favourite for current ROM, or remove from favourites view */
    if (y && !y_last && selected_index < entry_count) {
        if (viewing_favourites) {
            /* Remove selected favourite */
            favorites_remove_by_index(selected_index);
            /* Refresh favourites view */
            const FavoriteGame *fl = favorites_get_list();
            int fc = favorites_get_count();
            entry_count = 0;
            for (int i = 0; i < fc; i++) {
                strncpy(entries[entry_count].name, fl[i].game_name, 255);
                entries[entry_count].name[255] = '\0';
                entries[entry_count].is_dir = 0;
                entry_count++;
            }
            if (selected_index >= entry_count && selected_index > 0)
                selected_index = entry_count - 1;
            if (entry_count == 0) {
                /* No more favourites — go back to root */
                viewing_favourites = false;
                scan_directory(ROMS_PATH);
                strncpy(current_path, ROMS_PATH, MAX_PATH_LEN-1);
            }
        } else if (!viewing_recents && !entries[selected_index].is_dir &&
                   strcmp(entries[selected_index].name, SETTINGS_ENTRY_NAME) != 0 &&
                   strcmp(entries[selected_index].name, RECENTS_ENTRY_NAME) != 0 &&
                   strcmp(entries[selected_index].name, FAVOURITES_ENTRY_NAME) != 0) {
            /* Toggle favourite for current ROM */
            const char *folder = get_console_folder(current_path);
            const char *core   = get_core_for_folder(folder);
            if (!core) core = get_core_for_extension(entries[selected_index].name);
            char rom_path[MAX_PATH_LEN];
            snprintf(rom_path, sizeof(rom_path), "%s/%s", current_path, entries[selected_index].name);
            char game_name[256];
            strncpy(game_name, entries[selected_index].name, sizeof(game_name)-1);
            game_name[sizeof(game_name)-1] = '\0';
            char *dot = strrchr(game_name, '.');
            if (dot) *dot = '\0';
            if (core) favorites_toggle(core, game_name, rom_path);
        }
    }

    if (a && !a_last && selected_index < entry_count) {
        if (viewing_favourites) {
            /* Launch from favourites list */
            const FavoriteGame *fl = favorites_get_list();
            int fc = favorites_get_count();
            if (selected_index < fc) {
                if (strcmp(fl[selected_index].core_name, PCSX4ALL_BIN) == 0)
                    request_standalone_launch(fl[selected_index].core_name,
                                              fl[selected_index].full_path);
                else
                    request_game_launch(fl[selected_index].core_name,
                                        fl[selected_index].full_path);
            }
        } else if (viewing_recents) {
            /* Launch from recent games list */
            const RecentGame *rg = recent_games_get_list();
            int rc = recent_games_get_count();
            if (selected_index < rc) {
                if (strcmp(rg[selected_index].core_name, PCSX4ALL_BIN) == 0)
                    request_standalone_launch(rg[selected_index].core_name,
                                              rg[selected_index].full_path);
                else
                    request_game_launch(rg[selected_index].core_name,
                                        rg[selected_index].full_path);
            }
        } else if (strcmp(entries[selected_index].name, FAVOURITES_ENTRY_NAME) == 0) {
            /* Enter favourites view */
            const FavoriteGame *fl = favorites_get_list();
            int fc = favorites_get_count();
            while (fc > entry_capacity) {
                entry_capacity = entry_capacity ? entry_capacity*2 : INITIAL_ENTRIES_CAPACITY;
                entries = realloc(entries, entry_capacity * sizeof(DirEntry));
                if (!entries) goto input_done;
            }
            entry_count = 0;
            for (int i = 0; i < fc; i++) {
                strncpy(entries[entry_count].name, fl[i].game_name, 255);
                entries[entry_count].name[255] = '\0';
                entries[entry_count].is_dir = 0;
                entry_count++;
            }
            viewing_favourites = true;
            selected_index = 0; scroll_offset = 0;
        } else if (strcmp(entries[selected_index].name, RECENTS_ENTRY_NAME) == 0) {
            /* Enter recents view */
            const RecentGame *rg = recent_games_get_list();
            int rc = recent_games_get_count();
            while (rc > entry_capacity) {
                entry_capacity = entry_capacity ? entry_capacity*2 : INITIAL_ENTRIES_CAPACITY;
                entries = realloc(entries, entry_capacity * sizeof(DirEntry));
                if (!entries) goto input_done;
            }
            entry_count = 0;
            for (int i = 0; i < rc; i++) {
                strncpy(entries[entry_count].name, rg[i].game_name, 255);
                entries[entry_count].name[255] = '\0';
                entries[entry_count].is_dir = 0;
                entry_count++;
            }
            viewing_recents = true;
            selected_index = 0; scroll_offset = 0;
        } else if (entries[selected_index].is_dir) {
            char new_path[MAX_PATH_LEN];
            snprintf(new_path, sizeof(new_path), "%s/%s", current_path, entries[selected_index].name);
            strncpy(current_path, new_path, MAX_PATH_LEN-1);
            current_path[MAX_PATH_LEN-1] = '\0';
            scan_directory(current_path);
        } else {
            if (strcmp(entries[selected_index].name, SETTINGS_ENTRY_NAME) == 0) {
                settings_menu_active = true;
                settings_menu_idx = 0;
            } else {
                const char *folder = get_console_folder(current_path);
                const char *core   = get_core_for_folder(folder);
                if (!core)
                    core = get_core_for_extension(entries[selected_index].name);
                if (core) {
                    char rom_path[MAX_PATH_LEN];
                    snprintf(rom_path, sizeof(rom_path), "%s/%s", current_path, entries[selected_index].name);
                    {
                        int _ps1 = is_ps1_folder(folder);
                        int _bin = access(PCSX4ALL_BIN, F_OK);
                        char dbgbuf[512];
                        snprintf(dbgbuf, sizeof(dbgbuf),
                                 "A-press: folder=[%s] ps1=%d bin_ok=%d rom=[%s]",
                                 folder ? folder : "NULL", _ps1, _bin, rom_path);
                        dbg(dbgbuf);
                        FILE *_sdlog = fopen("/mnt/sdcard/frogui_apress.log", "a");
                        if (_sdlog) { fputs(dbgbuf, _sdlog); fputs("\n", _sdlog); fclose(_sdlog); sync(); }
                        if (_ps1 && _bin == 0)
                            request_standalone_launch(PCSX4ALL_BIN, rom_path);
                        else
                            request_game_launch(core, rom_path);
                    }
                } else {
                    dbg("no core mapping for this folder or extension");
                }
            }
        }
    }

    if (b && !b_last) {
        if (viewing_recents || viewing_favourites) {
            viewing_recents = false;
            viewing_favourites = false;
            scan_directory(ROMS_PATH);
            strncpy(current_path, ROMS_PATH, MAX_PATH_LEN-1);
        } else if (strcmp(current_path, ROMS_PATH) != 0) {
            char *slash = strrchr(current_path, '/');
            if (slash) { *slash = '\0'; scan_directory(current_path); }
        }
    }

    if (up && !up_last && selected_index > 0) {
        selected_index--;
        if (selected_index < scroll_offset) scroll_offset = selected_index;
    }
    if (dn && !dn_last && selected_index < entry_count-1) {
        selected_index++;
        if (selected_index >= scroll_offset + VISIBLE_ENTRIES)
            scroll_offset = selected_index - VISIBLE_ENTRIES + 1;
    }
    if (lt && !l_last) {
        selected_index = (selected_index >= VISIBLE_ENTRIES) ? selected_index - VISIBLE_ENTRIES : 0;
        if (selected_index < scroll_offset) scroll_offset = selected_index;
    }
    if (rt && !r_last) {
        selected_index = (selected_index + VISIBLE_ENTRIES < entry_count) ? selected_index + VISIBLE_ENTRIES : entry_count - 1;
        if (selected_index >= scroll_offset + VISIBLE_ENTRIES)
            scroll_offset = selected_index - VISIBLE_ENTRIES + 1;
    }

input_done:
    a_last=a; b_last=b; up_last=up; dn_last=dn; l_last=lt; r_last=rt; y_last=y;
}

/* ---- libretro API ---- */

unsigned retro_api_version(void) { return RETRO_API_VERSION; }

void retro_get_system_info(struct retro_system_info *info) {
    memset(info, 0, sizeof(*info));
    info->library_name     = "TreeFrogUI";
    info->library_version  = "1.0-sf3000";
    info->valid_extensions = "";
    info->need_fullpath    = false;
    info->block_extract    = false;
}

void retro_get_system_av_info(struct retro_system_av_info *info) {
    memset(info, 0, sizeof(*info));
    info->geometry.base_width   = SCREEN_WIDTH;
    info->geometry.base_height  = SCREEN_HEIGHT;
    info->geometry.max_width    = SCREEN_WIDTH;
    info->geometry.max_height   = SCREEN_HEIGHT;
    info->geometry.aspect_ratio = (float)SCREEN_WIDTH / SCREEN_HEIGHT;
    info->timing.fps            = 60.0;
    info->timing.sample_rate    = 44100.0;
}

void retro_set_environment(retro_environment_t cb) {
    environ_cb = cb;
    bool no_game = true;
    cb(RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME, &no_game);
    enum retro_pixel_format fmt = RETRO_PIXEL_FORMAT_RGB565;
    cb(RETRO_ENVIRONMENT_SET_PIXEL_FORMAT, &fmt);
}

void retro_set_video_refresh(retro_video_refresh_t cb)          { video_cb      = cb; }
void retro_set_audio_sample(retro_audio_sample_t cb)            { (void)cb; }
void retro_set_audio_sample_batch(retro_audio_sample_batch_t cb){ (void)cb; }
void retro_set_input_poll(retro_input_poll_t cb)                { input_poll_cb = cb; }
void retro_set_input_state(retro_input_state_t cb)              { input_state_cb = cb; }


void retro_init(void) {
    dbg("retro_init start");
    cv_init();
    font_init();
    dbg("font_init done");
    theme_init();
    dbg("theme_init done");
    settings_load_file();
    settings_apply();
    dbg("settings loaded");
    recent_games_init();
    dbg("recent_games_init done");
    favorites_init();
    dbg("favorites_init done");

    framebuffer = calloc(SCREEN_WIDTH * SCREEN_HEIGHT, sizeof(uint16_t));
    dbg("calloc done");
    render_init(framebuffer);
    dbg("render_init done");
    scan_directory(current_path);
    dbg("scan_directory done");
    shutdown_requested = false;
    dbg("retro_init complete");
}

void retro_deinit(void) {
    free(framebuffer); framebuffer = NULL;
    free(entries);     entries = NULL;
    entry_count = entry_capacity = 0;
}

bool retro_load_game(const struct retro_game_info *info) {
    (void)info;
    return true;  /* supports no-game */
}

void retro_unload_game(void) {}

static void render_settings_menu(void) {
    extern const Theme themes[];
    render_clear_screen(framebuffer);
    render_header(framebuffer, "SETTINGS");

    char line[128];
    int y0 = START_Y;
    /* Theme row */
    snprintf(line, sizeof(line), "Theme: < %s >", themes[settings_theme_idx].name);
    if (settings_menu_idx == 0) {
        render_text_pillbox(framebuffer, PADDING, y0, line, COLOR_SELECT_BG, COLOR_SELECT_TEXT, 7);
    } else {
        font_draw_text(framebuffer, SCREEN_WIDTH, SCREEN_HEIGHT, PADDING, y0, line, COLOR_TEXT);
    }
    /* Font row */
    snprintf(line, sizeof(line), "Font: < %s >", font_names[settings_font_idx]);
    int y1 = y0 + ITEM_HEIGHT;
    if (settings_menu_idx == 1) {
        render_text_pillbox(framebuffer, PADDING, y1, line, COLOR_SELECT_BG, COLOR_SELECT_TEXT, 7);
    } else {
        font_draw_text(framebuffer, SCREEN_WIDTH, SCREEN_HEIGHT, PADDING, y1, line, COLOR_TEXT);
    }
    /* Brightness row */
    snprintf(line, sizeof(line), "Brightness: < %d%% >", settings_brightness);
    int y2 = y1 + ITEM_HEIGHT;
    if (settings_menu_idx == 2) {
        render_text_pillbox(framebuffer, PADDING, y2, line, COLOR_SELECT_BG, COLOR_SELECT_TEXT, 7);
    } else {
        font_draw_text(framebuffer, SCREEN_WIDTH, SCREEN_HEIGHT, PADDING, y2, line, COLOR_TEXT);
    }
    render_legend(framebuffer, LEGEND_X_NONE);
}

void retro_run(void) {
    handle_input();

    /* Reload banner when view, path, or selection changes.
     * On the main SYSTEMS menu, preview the highlighted folder's banner. */
    static char banner_last_path[MAX_PATH_LEN] = "";
    static bool banner_last_recents = false;
    static bool banner_last_favourites = false;
    static int  banner_last_sel = -1;
    {
        const char *banner_path = current_path;
        char sel_path[MAX_PATH_LEN];
        if (!viewing_recents && !viewing_favourites &&
            strcmp(current_path, ROMS_PATH) == 0 &&
            selected_index >= 0 && selected_index < entry_count &&
            entries[selected_index].is_dir) {
            snprintf(sel_path, sizeof(sel_path), "%s/%s",
                     current_path, entries[selected_index].name);
            banner_path = sel_path;
        }
        if (viewing_recents != banner_last_recents ||
            viewing_favourites != banner_last_favourites ||
            strcmp(banner_path, banner_last_path) != 0 ||
            selected_index != banner_last_sel) {
            load_banner_for_view(banner_path, viewing_recents, viewing_favourites);
            banner_last_recents = viewing_recents;
            banner_last_favourites = viewing_favourites;
            banner_last_sel = selected_index;
            strncpy(banner_last_path, banner_path, MAX_PATH_LEN - 1);
            banner_last_path[MAX_PATH_LEN - 1] = '\0';
        }
    }

    if (settings_menu_active) {
        render_settings_menu();
    } else {
        if (banner_is_loaded())
            banner_render(framebuffer);
        else
            render_clear_screen(framebuffer);
        const char *title = viewing_recents    ? "RECENT GAMES" :
                            viewing_favourites ? "FAVOURITES" :
                            (strcmp(current_path, ROMS_PATH) == 0)
                            ? "TREEFROGUI: SYSTEMS" : get_basename(current_path);
        render_header(framebuffer, title);
        int visible = min(entry_count - scroll_offset, VISIBLE_ENTRIES);
        for (int i = 0; i < visible; i++) {
            int idx = scroll_offset + i;
            render_menu_item(framebuffer, idx, entries[idx].name, entries[idx].is_dir,
                             (idx == selected_index), scroll_offset, 0);
        }
        /* Compute Y-button legend mode for current selection */
        int legend_mode = LEGEND_X_NONE;
        if (viewing_favourites) {
            legend_mode = LEGEND_X_REMOVE;
        } else if (!viewing_recents && selected_index < entry_count &&
                   !entries[selected_index].is_dir &&
                   strcmp(entries[selected_index].name, SETTINGS_ENTRY_NAME) != 0 &&
                   strcmp(entries[selected_index].name, RECENTS_ENTRY_NAME) != 0 &&
                   strcmp(entries[selected_index].name, FAVOURITES_ENTRY_NAME) != 0) {
            const char *folder = get_console_folder(current_path);
            const char *core   = get_core_for_folder(folder);
            if (!core) core = get_core_for_extension(entries[selected_index].name);
            if (core) {
                char game_name[256];
                strncpy(game_name, entries[selected_index].name, sizeof(game_name)-1);
                game_name[sizeof(game_name)-1] = '\0';
                char *dot = strrchr(game_name, '.');
                if (dot) *dot = '\0';
                legend_mode = favorites_is_favorited(core, game_name)
                              ? LEGEND_X_REMOVE : LEGEND_X_FAVOURITE;
            }
        }
        render_legend(framebuffer, legend_mode);
    }

    if (video_cb)
        video_cb(framebuffer, SCREEN_WIDTH, SCREEN_HEIGHT, SCREEN_WIDTH * sizeof(uint16_t));
}

void retro_reset(void) { scan_directory(ROMS_PATH); strncpy(current_path, ROMS_PATH, MAX_PATH_LEN-1); }

unsigned retro_get_region(void)                                    { return RETRO_REGION_NTSC; }
size_t   retro_serialize_size(void)                                { return 0; }
bool     retro_serialize(void *d, size_t s)                        { (void)d;(void)s; return false; }
bool     retro_unserialize(const void *d, size_t s)                { (void)d;(void)s; return false; }
void     retro_cheat_reset(void)                                   {}
void     retro_cheat_set(unsigned i, bool e, const char *c)        { (void)i;(void)e;(void)c; }
void    *retro_get_memory_data(unsigned id)                        { (void)id; return NULL; }
size_t   retro_get_memory_size(unsigned id)                        { (void)id; return 0; }
bool     retro_load_game_special(unsigned t,
             const struct retro_game_info *i, size_t n)            { (void)t;(void)i;(void)n; return false; }
