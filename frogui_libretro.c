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
#include <strings.h>
#include <stdint.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <dirent.h>
#include <ctype.h>
#include <math.h>
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
#include "input.h"
#include "core_override.h"

#define SDCARD_BASE  "/mnt/sdcard"
#define CORES_PATH   SDCARD_BASE "/cubegm/cores"
#define ROMS_PATH    SDCARD_BASE "/roms"
#define LAUNCH_FILE  "/tmp/frogui_launch.txt"
#define PCSX4ALL_BIN SDCARD_BASE "/cubegm/pcsx4all"
#define PICO286_BIN  SDCARD_BASE "/cubegm/pico286"

/* Console → core mapping (folder name → libretro .so)
 * Folder names match /mnt/sdcard/roms/ subdirectories (gb300_multicore convention). */
typedef struct { const char *console_name; const char *core_path; } ConsoleMapping;
static const ConsoleMapping console_mappings[] = {
    /* NES */
    {"nes",    CORES_PATH "/fceumm_libretro.so"},
    {"nesq",   CORES_PATH "/quicknes_libretro.so"},
    {"nest",   CORES_PATH "/nestopia_libretro.so"},
    {"FC",     CORES_PATH "/fceumm_libretro.so"},
    {"fds",    CORES_PATH "/fceumm_libretro.so"},   /* Famicom Disk System (needs disksys.rom BIOS) */
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
    {"gba",    CORES_PATH "/gpsp_libretro.so"},            /* upstream libretro/gpsp */
    {"GBA",    CORES_PATH "/gpsp_libretro.so"},
    {"gbac",   CORES_PATH "/gpsp_multicore_libretro.so"},  /* tzubertowski gpsp_multicore */
    {"gbav",   CORES_PATH "/vba_next_libretro.so"},
    {"mgba",   CORES_PATH "/mgba_libretro.so"},
    {"gbaf",   CORES_PATH "/mgba_libretro.so"},
    {"gbaf",   CORES_PATH "/mgba_libretro.so"},
    {"GBA",    CORES_PATH "/gpsp_libretro.so"},
    /* Sega */
    {"sega",   CORES_PATH "/picodrive_libretro.so"},
    {"gg",     CORES_PATH "/gearsystem_libretro.so"},
    {"gpgx",   CORES_PATH "/genesis_plus_gx_libretro.so"},
    {"segacd", CORES_PATH "/genesis_plus_gx_libretro.so"},   /* Sega CD / Mega CD (needs BIOS) */
    {"MD",     CORES_PATH "/picodrive_libretro.so"},
    {"32x",    CORES_PATH "/picodrive_libretro.so"},   /* Sega 32X (heavy, may run slow) */
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
    {"pico8",  CORES_PATH "/fake08_libretro.so"},   /* PICO-8 (fake08 core) */
    {"pico286", PICO286_BIN},                        /* DOS PC (standalone, launched directly) */
    {"fake08", CORES_PATH "/fake08_libretro.so"},   /* legacy folder name */
    {"lowres-nx", CORES_PATH "/lowresnx_libretro.so"},
    {"tic80",  CORES_PATH "/tic80_libretro.so"},   /* TIC-80 fantasy console (.tic carts) */
    {"gme",    CORES_PATH "/gme_libretro.so"},
    {"m2k",    CORES_PATH "/mame2000_libretro.so"},
    {"cps1",   CORES_PATH "/fbalpha2012_cps1_libretro.so"},    /* Capcom CPS-1 (FBA 2012) */
    {"cps2",   CORES_PATH "/fbalpha2012_cps2_libretro.so"},    /* Capcom CPS-2 (FBA 2012) */
    {"neogeo", CORES_PATH "/fbalpha2012_neogeo_libretro.so"},  /* Neo Geo (FBA 2012) */
    {"pokem",  CORES_PATH "/pokemini_libretro.so"},
    {"int",    CORES_PATH "/freeintv_libretro.so"},
    {"fcf",    CORES_PATH "/freechaf_libretro.so"},
    {"cdg",    CORES_PATH "/pocketcdg_libretro.so"},
    {"chip8",  CORES_PATH "/jaxe_libretro.so"},
    {"retro8", CORES_PATH "/retro8_libretro.so"},
    {"arduboy",CORES_PATH "/ardens_libretro.so"},   /* default: Ardens (fast custom AVR core) */
    {"arduous",CORES_PATH "/arduous_libretro.so"},   /* alt: simavr-based arduous (cycle-accurate, slower) */
    {"vec",    CORES_PATH "/vecx_libretro.so"},
    {"thom",   CORES_PATH "/theodore_libretro.so"},
    {"o2em",   CORES_PATH "/o2em_libretro.so"},
    {"xmil",   CORES_PATH "/x68k_libretro.so"},
    {"geolith",CORES_PATH "/geolith_libretro.so"},
    {"gong",   CORES_PATH "/gong_libretro.so"},
    {"vapor",  CORES_PATH "/vaporspec_libretro.so"},
    {"amiga",  CORES_PATH "/uae_libretro.so"},
    {"atari-st", CORES_PATH "/castaway_libretro.so"},
    /* PlayStation: ps1/psx/PS run standalone PCSX4ALL (via is_ps1_folder);
     * these pcsx_rearmed entries are only the fallback if the pcsx4all binary
     * is missing.  ps1r runs the pcsx_rearmed libretro core directly. */
    {"ps1",    CORES_PATH "/pcsx_rearmed_libretro.so"},
    {"psx",    CORES_PATH "/pcsx_rearmed_libretro.so"},
    {"PS",     CORES_PATH "/pcsx_rearmed_libretro.so"},
    {"ps1r",   CORES_PATH "/pcsx_rearmed_libretro.so"},
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

/* --- Available cores for the per-game / per-folder core picker ---
 * Deduped list built from console_mappings. Index 0 = "Default (auto)"
 * which clears any override and falls back to folder/extension mapping. */
typedef struct { char name[64]; const char *path; } CoreChoice;
static CoreChoice core_choices[160];
static int core_choice_count = 0;

static void build_core_choices(void) {
    strcpy(core_choices[0].name, "Default (auto)");
    core_choices[0].path = NULL;
    core_choice_count = 1;
    for (int i = 0; console_mappings[i].console_name; i++) {
        const char *p = console_mappings[i].core_path;
        int dup = 0;
        for (int j = 1; j < core_choice_count; j++)
            if (strcmp(core_choices[j].path, p) == 0) { dup = 1; break; }
        if (dup || core_choice_count >= (int)(sizeof(core_choices)/sizeof(core_choices[0])))
            continue;
        const char *base = strrchr(p, '/'); base = base ? base + 1 : p;
        char nm[64]; strncpy(nm, base, sizeof(nm)-1); nm[sizeof(nm)-1] = '\0';
        char *suf = strstr(nm, "_libretro.so"); if (suf) *suf = '\0';
        strncpy(core_choices[core_choice_count].name, nm, 63);
        core_choices[core_choice_count].name[63] = '\0';
        core_choices[core_choice_count].path = p;
        core_choice_count++;
    }
}

static int core_choice_index_for_path(const char *path) {
    if (!path) return 0;
    for (int i = 1; i < core_choice_count; i++)
        if (strcmp(core_choices[i].path, path) == 0) return i;
    return 0;
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

/* Search (X button): on-screen keyboard → filtered results.
 * Scope = the folder you were in (ROMS_PATH root = search everything). */
static bool search_kbd_active = false;     /* typing the query */
static bool viewing_search    = false;     /* showing results list */
static char search_query[64]  = "";
static int  search_kbd_r = 0, search_kbd_c = 0;
static char search_scope[MAX_PATH_LEN] = "";
typedef struct { char name[256]; char path[MAX_PATH_LEN]; } SearchResult;
static SearchResult *search_results = NULL;
static int search_results_count = 0, search_results_cap = 0;

/* Per-game / per-folder core picker overlay (opened with SELECT) */
static bool core_picker_active = false;
static int  core_picker_idx = 0;
static int  core_picker_scroll = 0;
static char core_picker_key[MAX_PATH_LEN];   /* ROM path (per-game) or folder path */
static char core_picker_title[160];          /* shown under header */

static void dbg(const char *msg) {
    FILE *f = fopen("/tmp/frogui_crash.log", "a");
    if (f) { fputs(msg, f); fputs("\n", f); fclose(f); }
    fprintf(stderr, "FROGUI_DBG: %s\n", msg);
}

/* --- Input (via input.c / cubevol shmem) --- */

/* --- Settings ---
 * Stored at /mnt/sdcard/frogui/settings.txt, key=value format.
 * Two options: theme + font. */
#define SETTINGS_DIR  "/mnt/sdcard/frogui"
#define SETTINGS_FILE SETTINGS_DIR "/settings.txt"

/* Fonts are discovered at runtime by scanning the font directories for
 * .ttf/.otf files. font_files[] holds the on-disk filename (persisted in
 * settings + passed to the loader); font_disp[] is the extension-stripped
 * name shown in the menu. */
#define MAX_FONTS    32
#define FONT_STR_MAX 96
static char font_files[MAX_FONTS][FONT_STR_MAX];
static char font_disp[MAX_FONTS][FONT_STR_MAX];
static int  font_count = 0;

static int font_has_ext(const char *name) {
    const char *dot = strrchr(name, '.');
    return dot && (strcasecmp(dot, ".ttf") == 0 || strcasecmp(dot, ".otf") == 0);
}

static void font_add(const char *fname) {
    if (font_count >= MAX_FONTS) return;
    for (int i = 0; i < font_count; i++)
        if (strcasecmp(font_files[i], fname) == 0) return;  /* dedup */
    strncpy(font_files[font_count], fname, FONT_STR_MAX - 1);
    font_files[font_count][FONT_STR_MAX - 1] = '\0';
    font_count++;
}

static void font_scan(void) {
    static const char *dirs[] = {
        "/mnt/sdcard/cubegm/fonts",
        "/mnt/sdcard/frogui/fonts",
        "fonts",
    };
    font_count = 0;
    for (size_t d = 0; d < sizeof(dirs) / sizeof(dirs[0]); d++) {
        DIR *dp = opendir(dirs[d]);
        if (!dp) continue;
        struct dirent *e;
        while ((e = readdir(dp))) {
            if (e->d_name[0] == '.') continue;
            if (font_has_ext(e->d_name)) font_add(e->d_name);
        }
        closedir(dp);
    }
    /* No fonts on disk: keep the built-in defaults as a safety net. */
    if (font_count == 0) {
        font_add("GamePocket-Regular-ZeroKern.ttf");
        font_add("monogram.ttf");
    }
    /* Sort filenames alphabetically (case-insensitive) for a stable list. */
    for (int i = 1; i < font_count; i++) {
        char key[FONT_STR_MAX];
        strncpy(key, font_files[i], FONT_STR_MAX);
        int j = i - 1;
        while (j >= 0 && strcasecmp(font_files[j], key) > 0) {
            strncpy(font_files[j + 1], font_files[j], FONT_STR_MAX);
            j--;
        }
        strncpy(font_files[j + 1], key, FONT_STR_MAX);
    }
    /* Build display names = filename minus extension. */
    for (int i = 0; i < font_count; i++) {
        strncpy(font_disp[i], font_files[i], FONT_STR_MAX - 1);
        font_disp[i][FONT_STR_MAX - 1] = '\0';
        char *dot = strrchr(font_disp[i], '.');
        if (dot) *dot = '\0';
    }
}

static bool settings_menu_active = false;
static int settings_menu_idx = 0;       /* row: 0=theme, 1=font, 2=brightness, 3=filter, 4=auto-resume, 5=remap */
static int settings_theme_idx = 0;
static int settings_font_idx = 0;
static int settings_brightness = 75;    /* 0..100, step 5 */
static int settings_filter_idx = 1;     /* forced bilinear (option removed from menu) */
static int settings_filter_idx_on_enter = 0;  /* snapshot for restart-on-change */
static int settings_auto_resume = 0;    /* 0=off, 1=on */
static int settings_anim = 0;           /* UI animations: 0=off, 1=on */
static int settings_hide_empty = 0;     /* hide rom folders with no games: 0=off, 1=on */
static int settings_game_switcher = 1;  /* recents as box-art carousel: 0=off, 1=on */
static int settings_load_recents = 0;   /* start FrogUI in the recents view: 0=off, 1=on */
static const char *filter_names[] = {"nearest", "bilinear"};
static const char *onoff_names[] = {"off", "on"};
#define FILTER_COUNT 2
#define SETTINGS_BRIGHTNESS_STEP 5
/* Filter option removed from the menu — always bilinear (HW path). */
#define SETTINGS_ROW_COUNT 9
#define SETTINGS_ROW_AUTORESUME 3
#define SETTINGS_ROW_ANIM 4
#define SETTINGS_ROW_HIDEEMPTY 5
#define SETTINGS_ROW_SWITCHER 6
#define SETTINGS_ROW_LOADRECENTS 7
#define SETTINGS_ROW_REMAP 8

static bool remap_wizard_active = false;
static int  remap_step = 0;
static uint32_t remap_prev_raw = 0;

static void mkdir_p(const char *path) {
    char cmd[300];
    snprintf(cmd, sizeof(cmd), "mkdir -p '%s'", path);
    system(cmd);
}

static void settings_apply(void) {
    extern const int theme_count;
    if (settings_theme_idx < 0 || settings_theme_idx >= theme_count) settings_theme_idx = 0;
    if (settings_font_idx < 0 || settings_font_idx >= font_count) settings_font_idx = 0;
    if (settings_brightness < 0)   settings_brightness = 0;
    if (settings_brightness > 100) settings_brightness = 100;
    theme_apply(settings_theme_idx);
    if (font_count > 0)
        font_load_file(font_files[settings_font_idx]);
    cube_set_backlight(settings_brightness);
    banner_set_anim(settings_anim);
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
            for (int i = 0; i < font_count; i++)
                if (strcasecmp(font_files[i], val) == 0 ||
                    strcasecmp(font_disp[i], val) == 0) { settings_font_idx = i; break; }
        } else if (strcmp(line, "brightness") == 0) {
            settings_brightness = atoi(val);
        } else if (strcmp(line, "filter") == 0) {
            for (int i = 0; i < FILTER_COUNT; i++)
                if (strcmp(filter_names[i], val) == 0) { settings_filter_idx = i; break; }
        } else if (strcmp(line, "animations") == 0) {
            settings_anim = (strcmp(val, "off") == 0) ? 0 : 1;
        } else if (strcmp(line, "auto_resume") == 0) {
            settings_auto_resume = (strcmp(val, "on") == 0) ? 1 : 0;
        } else if (strcmp(line, "hide_empty") == 0) {
            settings_hide_empty = (strcmp(val, "on") == 0) ? 1 : 0;
        } else if (strcmp(line, "game_switcher") == 0) {
            settings_game_switcher = (strcmp(val, "on") == 0) ? 1 : 0;
        } else if (strcmp(line, "load_recents") == 0) {
            settings_load_recents = (strcmp(val, "on") == 0) ? 1 : 0;
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
    fprintf(f, "font=%s\n", font_count > 0 ? font_files[settings_font_idx] : "");
    fprintf(f, "brightness=%d\n", settings_brightness);
    fprintf(f, "filter=%s\n", filter_names[settings_filter_idx]);
    fprintf(f, "auto_resume=%s\n", onoff_names[settings_auto_resume]);
    fprintf(f, "animations=%s\n", onoff_names[settings_anim]);
    fprintf(f, "hide_empty=%s\n", onoff_names[settings_hide_empty]);
    fprintf(f, "game_switcher=%s\n", onoff_names[settings_game_switcher]);
    fprintf(f, "load_recents=%s\n", onoff_names[settings_load_recents]);
    fflush(f);
    fsync(fileno(f));
    fclose(f);
    sync();  /* SD-card flush */
    { char buf[64]; snprintf(buf, sizeof(buf), "settings save: filter=%s idx=%d",
                                                filter_names[settings_filter_idx], settings_filter_idx);
      dbg(buf); }
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

/* True if `path` contains at least one game (any file that isn't artwork/metadata),
 * recursing into subfolders. Used to hide empty rom folders. Bounded depth. */
static int folder_has_games(const char *path, int depth) {
    if (depth > 3) return 0;
    DIR *d = opendir(path);
    if (!d) return 0;
    struct dirent *e;
    int found = 0;
    while ((e = readdir(d)) != NULL) {
        if (e->d_name[0] == '.') continue;
        char full[MAX_PATH_LEN];
        snprintf(full, sizeof(full), "%s/%s", path, e->d_name);
        struct stat st;
        if (stat(full, &st) != 0) continue;
        if (S_ISDIR(st.st_mode)) {
            if (folder_has_games(full, depth + 1)) { found = 1; break; }
        } else {
            size_t nlen = strlen(e->d_name);
            int is_p8png = nlen >= 7 && strcasecmp(e->d_name + nlen - 7, ".p8.png") == 0;
            const char *ext = strrchr(e->d_name, '.');
            if (ext && !is_p8png &&
                (strcasecmp(ext,".csv")==0 || strcasecmp(ext,".txt")==0 ||
                 strcasecmp(ext,".xml")==0 || strcasecmp(ext,".jpg")==0 ||
                 strcasecmp(ext,".png")==0)) continue;
            found = 1; break;
        }
    }
    closedir(d);
    return found;
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
            /* PICO-8 carts are .p8.png — keep them; only skip plain .png artwork. */
            size_t nlen = strlen(e->d_name);
            int is_p8png = nlen >= 7 && strcasecmp(e->d_name + nlen - 7, ".p8.png") == 0;
            if (ext && !is_p8png &&
                       (strcasecmp(ext,".csv")==0 || strcasecmp(ext,".txt")==0 ||
                        strcasecmp(ext,".xml")==0 || strcasecmp(ext,".jpg")==0 ||
                        strcasecmp(ext,".png")==0)) continue;
        }
        /* Always hide the internal "menu" folder at the root. */
        if (S_ISDIR(st.st_mode) && strcmp(path, ROMS_PATH) == 0 &&
            strcasecmp(e->d_name, "menu") == 0) continue;
        /* Hide-empty-folders: at the root, skip rom folders with no games. */
        if (S_ISDIR(st.st_mode) && settings_hide_empty &&
            strcmp(path, ROMS_PATH) == 0 && !folder_has_games(full, 0)) continue;
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
        /* Prepend Settings, then Favourites, then Recents (Recents ends up at index 0) */
        memmove(&entries[1], &entries[0], entry_count * sizeof(DirEntry));
        strncpy(entries[0].name, SETTINGS_ENTRY_NAME, 255);
        entries[0].name[255] = '\0';
        entries[0].is_dir = 0;
        entry_count++;
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

/* Total seconds played for a game, from picoarch's playtime.txt. */
static long playtime_lookup(const char *path) {
    if (!path || !*path) return 0;
    FILE *f = fopen("/mnt/sdcard/frogui/playtime.txt", "r");
    if (!f) return 0;
    char line[1100]; long sec = 0;
    while (fgets(line, sizeof line, f)) {
        char *t = strchr(line, '\t'); if (!t) continue;
        *t = 0; char *p = t + 1; p[strcspn(p, "\r\n")] = 0;
        if (!strcmp(p, path)) { sec = atol(line); break; }
    }
    fclose(f);
    return sec;
}

/* ---------------- OnionOS-style game switcher (recents as box-art carousel) ----
 * Art = box art (.res/<name>.rgb565); if missing, the newest save-state
 * screenshot picoarch wrote (.st<N>.bmp). Toggled by settings_game_switcher. */
#include "stb_image.h"

/* Newest save-state screenshot for a game: /mnt/sdcard/picoarch/<tag>/<base>.st<N>.bmp */
static int switcher_savestate_bmp(const char *full_path, char *out, size_t n) {
    char dir[640]; strncpy(dir, full_path, sizeof dir - 1); dir[sizeof dir - 1] = 0;
    char *sl = strrchr(dir, '/'); if (!sl) return 0;
    char base[256]; strncpy(base, sl + 1, sizeof base - 1); base[sizeof base - 1] = 0;
    *sl = 0;
    char *tagsl = strrchr(dir, '/'); const char *tag = tagsl ? tagsl + 1 : dir;
    char *dot = strrchr(base, '.'); if (dot) *dot = 0;     /* strip extension */
    /* dedicated per-game last-screen snapshot (written on exit) first */
    snprintf(out, n, "/mnt/sdcard/picoarch/%s/%s.scr.bmp", tag, base);
    if (access(out, F_OK) == 0) return 1;
    for (int slot = 9; slot >= 0; slot--) {                 /* then save states, prefer slot 9 */
        snprintf(out, n, "/mnt/sdcard/picoarch/%s/%s.st%d.bmp", tag, base, slot);
        if (access(out, F_OK) == 0) return 1;
    }
    return 0;
}

static void switcher_blit565(uint16_t *fb, const uint16_t *src, int sw, int sh,
                             int bx, int by, int bw, int bh) {
    if (!src || sw <= 0 || sh <= 0) return;
    int dw = bw, dh = sh * bw / sw;
    if (dh > bh) { dh = bh; dw = sw * bh / sh; }
    int ox = bx + (bw - dw) / 2, oy = by + (bh - dh) / 2;
    for (int y = 0; y < dh; y++) {
        const uint16_t *r = src + (size_t)(y * sh / dh) * sw;
        uint16_t *d = fb + (size_t)(oy + y) * SCREEN_WIDTH + ox;
        for (int x = 0; x < dw; x++) d[x] = r[x * sw / dw];
    }
}

static void render_game_switcher(uint16_t *framebuffer) {
    const RecentGame *list = recent_games_get_list();
    int n = recent_games_get_count();
    if (n <= 0) {
        render_header(framebuffer, "RECENT GAMES");
        font_draw_text(framebuffer, SCREEN_WIDTH, SCREEN_HEIGHT, PADDING, SCREEN_HEIGHT/2,
                       "No recent games yet", COLOR_TEXT);
        render_legend(framebuffer, LEGEND_X_NONE, 0, 0);
        return;
    }
    if (selected_index < 0) selected_index = 0;
    if (selected_index >= n) selected_index = n - 1;
    const RecentGame *g = &list[selected_index];

    /* Art fills the whole screen above the bottom info bar (no header, no legend
     * — they just shrink the art). */
    int barh = UI_S(30);
    int bx = 0, by = 0;
    int bw = SCREEN_WIDTH, bh = SCREEN_HEIGHT - barh;
    render_fill_rect(framebuffer, bx, by, bw, bh, 0x0000);

    int drawn = 0;
    char path[1024];
    get_thumbnail_path(g->full_path, path, sizeof path);
    Thumbnail tb;
    if (load_thumbnail(path, &tb) && tb.data) {
        switcher_blit565(framebuffer, tb.data, tb.width, tb.height, bx, by, bw, bh);
        free_thumbnail(&tb); drawn = 1;
    }
    if (!drawn && switcher_savestate_bmp(g->full_path, path, sizeof path)) {
        int w, h, ch; unsigned char *img = stbi_load(path, &w, &h, &ch, 3);
        if (img) {
            int dw = bw, dh = h * bw / w; if (dh > bh) { dh = bh; dw = w * bh / h; }
            int ox = bx + (bw - dw) / 2, oy = by + (bh - dh) / 2;
            for (int y = 0; y < dh; y++) {
                const unsigned char *rr = img + (size_t)(y * h / dh) * w * 3;
                uint16_t *d = framebuffer + (size_t)(oy + y) * SCREEN_WIDTH + ox;
                for (int x = 0; x < dw; x++) {
                    const unsigned char *p = rr + (size_t)(x * w / dw) * 3;
                    d[x] = ((p[0] & 0xF8) << 8) | ((p[1] & 0xFC) << 3) | (p[2] >> 3);
                }
            }
            stbi_image_free(img); drawn = 1;
        }
    }
    if (!drawn)
        font_draw_text(framebuffer, SCREEN_WIDTH, SCREEN_HEIGHT, bx + UI_S(16), by + bh/2,
                       "(no screenshot - open the in-game menu once)", COLOR_TEXT);

    /* Bottom info bar: game name (left) + position / play time (right). */
    int byb = SCREEN_HEIGHT - barh;
    render_fill_rect(framebuffer, 0, byb, SCREEN_WIDTH, barh, 0x2104);
    font_draw_text(framebuffer, SCREEN_WIDTH, SCREEN_HEIGHT, PADDING, byb + UI_S(8),
                   g->game_name, COLOR_TEXT);
    char info[96];
    long secs = playtime_lookup(g->full_path);
    if (secs >= 3600)
        snprintf(info, sizeof info, "Played %ldh %ldm   %d/%d", secs/3600, (secs%3600)/60, selected_index + 1, n);
    else if (secs >= 60)
        snprintf(info, sizeof info, "Played %ldm   %d/%d", secs/60, selected_index + 1, n);
    else if (secs > 0)
        snprintf(info, sizeof info, "Played %lds   %d/%d", secs, selected_index + 1, n);
    else
        snprintf(info, sizeof info, "%d/%d", selected_index + 1, n);
    int iw = (int)strlen(info) * UI_S(8);
    font_draw_text(framebuffer, SCREEN_WIDTH, SCREEN_HEIGHT, SCREEN_WIDTH - iw - PADDING, byb + UI_S(8),
                   info, COLOR_TEXT);
}

/* Switch the browser into the recents view (used by the Recents entry and, when
 * "Start in Recents" is on, at startup). */
static void enter_recents_view(void) {
    const RecentGame *rg = recent_games_get_list();
    int rc = recent_games_get_count();
    while (rc > entry_capacity) {
        entry_capacity = entry_capacity ? entry_capacity*2 : INITIAL_ENTRIES_CAPACITY;
        DirEntry *ne = realloc(entries, entry_capacity * sizeof(DirEntry));
        if (!ne) return;
        entries = ne;
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
}

/* Hand the active theme's colors to picoarch so its in-game menu matches FrogUI.
 * picoarch reads <exe_dir>/skin/skin.txt; its parser expects 24-bit RGB888 hex
 * (it converts to RGB565), so expand our RGB565 theme colors to RGB888. */
static unsigned rgb565_to_888(uint16_t c) {
    unsigned r5 = (c >> 11) & 0x1F, g6 = (c >> 5) & 0x3F, b5 = c & 0x1F;
    unsigned r8 = (r5 << 3) | (r5 >> 2);
    unsigned g8 = (g6 << 2) | (g6 >> 4);
    unsigned b8 = (b5 << 3) | (b5 >> 2);
    return (r8 << 16) | (g8 << 8) | b8;
}
static void write_picoarch_skin(void) {
    extern uint16_t theme_text(void);
    extern uint16_t theme_select_text(void);
    extern uint16_t theme_select_bg(void);
    mkdir("/mnt/sdcard/cubegm/skin", 0777);
    FILE *s = fopen("/mnt/sdcard/cubegm/skin/skin.txt", "w");
    if (!s) return;
    /* Match FrogUI: normal rows in theme text colour, selected row = select-text
     * on the select-bg pill. */
    fprintf(s, "text_color=0x%06X\n",     rgb565_to_888(theme_text()));
    fprintf(s, "selection_color=0x%06X\n", rgb565_to_888(theme_select_bg()));
    fprintf(s, "sel_text_color=0x%06X\n",  rgb565_to_888(theme_select_text()));
    fclose(s);
}

static void request_game_launch(const char *core_path, const char *rom_path) {
    write_picoarch_skin();
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

static bool is_pico286_folder(const char *folder) {
    return folder && strcasecmp(folder, "pico286") == 0;
}

/* A standalone-launched binary (run directly, not as a libretro core). */
static bool is_standalone_bin(const char *name) {
    return name && (strcmp(name, PCSX4ALL_BIN) == 0 ||
                    strcmp(name, PICO286_BIN)  == 0);
}

/* ----------------------------- Search (X button) ----------------------------- */

/* On-screen keyboard layout. Rows 0-3 are character keys; row 4 is special. */
static const char *KBD_ROWS[4] = {
    "1234567890",
    "QWERTYUIOP",
    "ASDFGHJKL",
    "ZXCVBNM",
};
#define KBD_SPECIAL_ROW 4
#define KBD_NROWS       5
static const char *KBD_SPECIAL[3] = { "SPACE", "DEL", "GO" };

static int kbd_row_len(int r) {
    return (r == KBD_SPECIAL_ROW) ? 3 : (int)strlen(KBD_ROWS[r]);
}

static int str_icontains(const char *hay, const char *needle) {
    if (!needle[0]) return 1;
    for (; *hay; hay++) {
        const char *h = hay, *n = needle;
        while (*h && *n && tolower((unsigned char)*h) == tolower((unsigned char)*n)) { h++; n++; }
        if (!*n) return 1;
    }
    return 0;
}

static void search_add_result(const char *name, const char *path) {
    if (search_results_count >= search_results_cap) {
        int nc = search_results_cap ? search_results_cap * 2 : 128;
        SearchResult *nr = realloc(search_results, nc * sizeof(SearchResult));
        if (!nr) return;
        search_results = nr; search_results_cap = nc;
    }
    SearchResult *r = &search_results[search_results_count];
    strncpy(r->name, name, sizeof(r->name) - 1); r->name[sizeof(r->name)-1] = '\0';
    strncpy(r->path, path, sizeof(r->path) - 1); r->path[sizeof(r->path)-1] = '\0';
    search_results_count++;
}

static void search_walk(const char *dir, int depth) {
    DIR *d = opendir(dir);
    if (!d) return;
    struct dirent *e;
    while ((e = readdir(d)) && search_results_count < 2000) {
        if (e->d_name[0] == '.') continue;
        char p[MAX_PATH_LEN];
        snprintf(p, sizeof(p), "%s/%s", dir, e->d_name);
        struct stat st;
        if (stat(p, &st) != 0) continue;
        if (S_ISDIR(st.st_mode)) {
            if (depth < 3) search_walk(p, depth + 1);
        } else if (str_icontains(e->d_name, search_query)) {
            search_add_result(e->d_name, p);
        }
    }
    closedir(d);
}

static void run_search(void) {
    search_results_count = 0;
    if (search_query[0])
        search_walk(search_scope[0] ? search_scope : ROMS_PATH, 0);

    /* mirror results into entries[] for the shared list renderer */
    while (search_results_count > entry_capacity) {
        entry_capacity = entry_capacity ? entry_capacity * 2 : INITIAL_ENTRIES_CAPACITY;
        entries = realloc(entries, entry_capacity * sizeof(DirEntry));
        if (!entries) return;
    }
    entry_count = 0;
    for (int i = 0; i < search_results_count; i++) {
        strncpy(entries[entry_count].name, search_results[i].name, 255);
        entries[entry_count].name[255] = '\0';
        entries[entry_count].is_dir = 0;
        entry_count++;
    }
    viewing_search = true;
    search_kbd_active = false;
    selected_index = 0; scroll_offset = 0;
}

static void search_launch(int idx) {
    if (idx < 0 || idx >= search_results_count) return;
    const char *path = search_results[idx].path;
    const char *folder = get_console_folder(path);
    const char *ov = core_override_lookup(path, NULL);
    const char *core = ov ? ov : get_core_for_folder(folder);
    if (!core) core = get_core_for_extension(path);
    if (ov)
        request_game_launch(ov, path);
    else if (core) {
        if (is_ps1_folder(folder) && access(PCSX4ALL_BIN, F_OK) == 0)
            request_standalone_launch(PCSX4ALL_BIN, path);
        else if (is_pico286_folder(folder) && access(PICO286_BIN, F_OK) == 0)
            request_standalone_launch(PICO286_BIN, path);
        else
            request_game_launch(core, path);
    } else {
        dbg("search: no core mapping for result");
    }
}

static void handle_input(void) {
    input_update();

    /* Search keyboard overlay */
    if (search_kbd_active) {
        if (input_was_pressed(FROG_BTN_UP))    search_kbd_r = (search_kbd_r - 1 + KBD_NROWS) % KBD_NROWS;
        if (input_was_pressed(FROG_BTN_DOWN))  search_kbd_r = (search_kbd_r + 1) % KBD_NROWS;
        if (input_was_pressed(FROG_BTN_LEFT))  search_kbd_c--;
        if (input_was_pressed(FROG_BTN_RIGHT)) search_kbd_c++;
        { int rl = kbd_row_len(search_kbd_r);
          if (search_kbd_c < 0) search_kbd_c = rl - 1;
          if (search_kbd_c >= rl) search_kbd_c = 0; }
        if (input_was_pressed(FROG_BTN_A)) {
            int len = (int)strlen(search_query);
            if (search_kbd_r == KBD_SPECIAL_ROW) {
                if (search_kbd_c == 0) { if (len < (int)sizeof(search_query)-1) { search_query[len]=' '; search_query[len+1]='\0'; } }
                else if (search_kbd_c == 1) { if (len > 0) search_query[len-1] = '\0'; }
                else run_search();
            } else if (len < (int)sizeof(search_query)-1) {
                search_query[len] = KBD_ROWS[search_kbd_r][search_kbd_c];
                search_query[len+1] = '\0';
            }
        }
        if (input_was_pressed(FROG_BTN_Y)) {            /* quick backspace */
            int len = (int)strlen(search_query); if (len > 0) search_query[len-1] = '\0';
        }
        if (input_was_pressed(FROG_BTN_START)) run_search();
        if (input_was_pressed(FROG_BTN_B)) {           /* cancel → restore browser list */
            search_kbd_active = false;
            scan_directory(current_path);
            selected_index = 0; scroll_offset = 0;
        }
        return;
    }

    /* Core picker overlay: choose an override core for the current ROM/folder */
    if (core_picker_active) {
        if (input_was_pressed(FROG_BTN_UP)) {
            core_picker_idx = (core_picker_idx - 1 + core_choice_count) % core_choice_count;
        }
        if (input_was_pressed(FROG_BTN_DOWN)) {
            core_picker_idx = (core_picker_idx + 1) % core_choice_count;
        }
        if (input_was_pressed(FROG_BTN_LEFT)) {
            core_picker_idx -= VISIBLE_ENTRIES;
            if (core_picker_idx < 0) core_picker_idx = 0;
        }
        if (input_was_pressed(FROG_BTN_RIGHT)) {
            core_picker_idx += VISIBLE_ENTRIES;
            if (core_picker_idx >= core_choice_count) core_picker_idx = core_choice_count - 1;
        }
        if (core_picker_idx < core_picker_scroll)
            core_picker_scroll = core_picker_idx;
        if (core_picker_idx >= core_picker_scroll + VISIBLE_ENTRIES)
            core_picker_scroll = core_picker_idx - VISIBLE_ENTRIES + 1;
        if (input_was_pressed(FROG_BTN_A)) {
            core_override_set(core_picker_key, core_choices[core_picker_idx].path);
            core_picker_active = false;
        }
        if (input_was_pressed(FROG_BTN_B)) {
            core_picker_active = false;
        }
        return;
    }

    /* Remap wizard: detect raw rising edge on any bit; B = skip this step */
    if (remap_wizard_active) {
        uint32_t raw   = input_get_raw_state();
        uint32_t risen = raw & ~remap_prev_raw;
        remap_prev_raw = raw;
        bool skip = (risen >> input_get_raw_bit(FROG_BTN_B)) & 1;
        int  pressed_bit = -1;
        if (!skip) {
            for (int bit = 0; bit < 16; bit++) {
                if ((risen >> bit) & 1) { pressed_bit = bit; break; }
            }
        }
        if (skip || pressed_bit >= 0) {
            if (!skip) input_set_raw_bit((FrogButton)remap_step, pressed_bit);
            remap_step++;
            if (remap_step >= FROG_BTN_COUNT) {
                remap_wizard_active = false;
                input_save_remap(KEYMAP_FILE);
            }
        }
        return;
    }

    if (settings_menu_active) {
        extern const int theme_count;
        if (input_was_pressed(FROG_BTN_UP))
            settings_menu_idx = (settings_menu_idx - 1 + SETTINGS_ROW_COUNT) % SETTINGS_ROW_COUNT;
        if (input_was_pressed(FROG_BTN_DOWN))
            settings_menu_idx = (settings_menu_idx + 1) % SETTINGS_ROW_COUNT;
        if (input_was_pressed(FROG_BTN_LEFT) || input_was_pressed(FROG_BTN_RIGHT)) {
            int delta = input_was_pressed(FROG_BTN_RIGHT) ? 1 : -1;
            if (settings_menu_idx == 0) {
                settings_theme_idx = (settings_theme_idx + delta + theme_count) % theme_count;
            } else if (settings_menu_idx == 1) {
                if (font_count > 0)
                    settings_font_idx = (settings_font_idx + delta + font_count) % font_count;
            } else if (settings_menu_idx == 2) {
                settings_brightness += delta * SETTINGS_BRIGHTNESS_STEP;
                if (settings_brightness < 0)   settings_brightness = 0;
                if (settings_brightness > 100) settings_brightness = 100;
            } else if (settings_menu_idx == SETTINGS_ROW_AUTORESUME) {
                settings_auto_resume = (settings_auto_resume + delta + 2) % 2;
            } else if (settings_menu_idx == SETTINGS_ROW_ANIM) {
                settings_anim = (settings_anim + delta + 2) % 2;
            } else if (settings_menu_idx == SETTINGS_ROW_HIDEEMPTY) {
                settings_hide_empty = (settings_hide_empty + delta + 2) % 2;
            } else if (settings_menu_idx == SETTINGS_ROW_SWITCHER) {
                settings_game_switcher = (settings_game_switcher + delta + 2) % 2;
            } else if (settings_menu_idx == SETTINGS_ROW_LOADRECENTS) {
                settings_load_recents = (settings_load_recents + delta + 2) % 2;
            }
            settings_apply();
        }
        if (input_was_pressed(FROG_BTN_A) && settings_menu_idx == SETTINGS_ROW_REMAP) {
            remap_step = 0;
            remap_prev_raw = input_get_raw_state();
            remap_wizard_active = true;
            settings_menu_active = false;
            return;
        }
        if ((input_was_pressed(FROG_BTN_A) && settings_menu_idx != SETTINGS_ROW_REMAP) ||
             input_was_pressed(FROG_BTN_B)) {
            settings_save_file();
            settings_menu_active = false;
            /* re-scan root so a hide-empty-folders change takes effect now */
            scan_directory(ROMS_PATH);
            strncpy(current_path, ROMS_PATH, MAX_PATH_LEN-1);
            selected_index = 0; scroll_offset = 0;
        }
        return;
    }

    /* X: open search. Scope = current folder (ROMS root → search everything). */
    if (input_was_pressed(FROG_BTN_X) && !viewing_recents && !viewing_favourites && !viewing_search) {
        search_query[0] = '\0';
        search_kbd_r = 0; search_kbd_c = 0;
        strncpy(search_scope, current_path, sizeof(search_scope)-1);
        search_scope[sizeof(search_scope)-1] = '\0';
        search_kbd_active = true;
        goto input_done;
    }

    /* SELECT: open core picker. On a ROM file → per-game override; on a system
     * folder → per-folder override. Not available in recents/favourites views. */
    if (input_was_pressed(FROG_BTN_SELECT) && !viewing_recents && !viewing_favourites && !viewing_search &&
        selected_index < entry_count &&
        strcmp(entries[selected_index].name, SETTINGS_ENTRY_NAME) != 0 &&
        strcmp(entries[selected_index].name, RECENTS_ENTRY_NAME) != 0 &&
        strcmp(entries[selected_index].name, FAVOURITES_ENTRY_NAME) != 0) {
        const char *cur;
        if (entries[selected_index].is_dir) {
            snprintf(core_picker_key, sizeof(core_picker_key), "%s/%s",
                     current_path, entries[selected_index].name);
            snprintf(core_picker_title, sizeof(core_picker_title), "Folder: %s",
                     entries[selected_index].name);
            cur = core_override_lookup(NULL, core_picker_key);
        } else {
            snprintf(core_picker_key, sizeof(core_picker_key), "%s/%s",
                     current_path, entries[selected_index].name);
            snprintf(core_picker_title, sizeof(core_picker_title), "Game: %s",
                     entries[selected_index].name);
            cur = core_override_lookup(core_picker_key, NULL);
        }
        core_picker_idx = core_choice_index_for_path(cur);
        core_picker_scroll = 0;
        if (core_picker_idx >= VISIBLE_ENTRIES)
            core_picker_scroll = core_picker_idx - VISIBLE_ENTRIES + 1;
        core_picker_active = true;
        return;
    }

    /* Y: toggle favourite for current ROM, or remove from favourites view */
    if (input_was_pressed(FROG_BTN_Y) && selected_index < entry_count) {
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

    if (input_was_pressed(FROG_BTN_A) && selected_index < entry_count) {
        if (viewing_search) {
            search_launch(selected_index);
        } else if (viewing_favourites) {
            /* Launch from favourites list */
            const FavoriteGame *fl = favorites_get_list();
            int fc = favorites_get_count();
            if (selected_index < fc) {
                if (is_standalone_bin(fl[selected_index].core_name))
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
                if (is_standalone_bin(rg[selected_index].core_name))
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
            enter_recents_view();
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
                settings_filter_idx_on_enter = settings_filter_idx;
            } else {
                const char *folder = get_console_folder(current_path);
                char rom_path[MAX_PATH_LEN];
                snprintf(rom_path, sizeof(rom_path), "%s/%s", current_path, entries[selected_index].name);
                /* per-game / per-folder override wins over folder/extension default */
                const char *ov = core_override_lookup(rom_path, current_path);
                const char *core = ov ? ov : get_core_for_folder(folder);
                if (!core)
                    core = get_core_for_extension(entries[selected_index].name);
                if (ov) {
                    request_game_launch(ov, rom_path);   /* override → always libretro */
                } else if (core) {
                    if (is_ps1_folder(folder) && access(PCSX4ALL_BIN, F_OK) == 0)
                        request_standalone_launch(PCSX4ALL_BIN, rom_path);
                    else if (is_pico286_folder(folder) && access(PICO286_BIN, F_OK) == 0)
                        request_standalone_launch(PICO286_BIN, rom_path);
                    else
                        request_game_launch(core, rom_path);
                } else {
                    dbg("no core mapping for this folder or extension");
                }
            }
        }
    }

    if (input_was_pressed(FROG_BTN_B)) {
        if (viewing_search) {
            /* back to the keyboard to refine the query */
            viewing_search = false;
            search_kbd_active = true;
        } else if (viewing_recents || viewing_favourites) {
            viewing_recents = false;
            viewing_favourites = false;
            scan_directory(ROMS_PATH);
            strncpy(current_path, ROMS_PATH, MAX_PATH_LEN-1);
        } else if (strcmp(current_path, ROMS_PATH) != 0) {
            char *slash = strrchr(current_path, '/');
            if (slash) { *slash = '\0'; scan_directory(current_path); }
        }
    }

    if (input_was_pressed(FROG_BTN_UP) && selected_index > 0) {
        selected_index--;
        if (selected_index < scroll_offset) scroll_offset = selected_index;
    }
    if (input_was_pressed(FROG_BTN_DOWN) && selected_index < entry_count-1) {
        selected_index++;
        if (selected_index >= scroll_offset + VISIBLE_ENTRIES)
            scroll_offset = selected_index - VISIBLE_ENTRIES + 1;
    }
    /* In the game switcher (one game on screen at a time), Left/Right step one
     * game like Up/Down — no page jumping. */
    bool switcher = viewing_recents && settings_game_switcher;
    if (input_was_pressed(FROG_BTN_LEFT)) {
        if (switcher) {
            if (selected_index > 0) selected_index--;
        } else {
            selected_index = (selected_index >= VISIBLE_ENTRIES) ? selected_index - VISIBLE_ENTRIES : 0;
            if (selected_index < scroll_offset) scroll_offset = selected_index;
        }
    }
    if (input_was_pressed(FROG_BTN_RIGHT)) {
        if (switcher) {
            if (selected_index < entry_count-1) selected_index++;
        } else {
            selected_index = (selected_index + VISIBLE_ENTRIES < entry_count) ? selected_index + VISIBLE_ENTRIES : entry_count - 1;
            if (selected_index >= scroll_offset + VISIBLE_ENTRIES)
                scroll_offset = selected_index - VISIBLE_ENTRIES + 1;
        }
    }

input_done:;
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
    /* Runtime panel geometry from picoarch (device-detected). Must run before
     * font_init / any layout use. Falls back to render.c defaults if env unset. */
    {
        const char *ew = getenv("TF_PANEL_W");
        const char *eh = getenv("TF_PANEL_H");
        const char *es = getenv("TF_UI_SCALE");
        int w = ew ? atoi(ew) : 0;
        int h = eh ? atoi(eh) : 0;
        int s = es ? atoi(es) : 0;
        render_set_geometry(w, h, s);
        dbg("geometry set");
    }
    input_init();
    input_load_remap(KEYMAP_FILE);
    font_init();
    dbg("font_init done");
    font_scan();
    dbg("font_scan done");
    theme_init();
    dbg("theme_init done");
    settings_load_file();
    fb1_set_visible(1);   /* restart cubevol for OSD overlay — also resets backlight */
    settings_apply();     /* apply AFTER cubevol restart so our brightness wins */
    dbg("settings loaded");
    recent_games_init();
    dbg("recent_games_init done");
    favorites_init();
    dbg("favorites_init done");
    core_override_load();
    build_core_choices();
    dbg("core overrides loaded");

    framebuffer = calloc(SCREEN_WIDTH * SCREEN_HEIGHT, sizeof(uint16_t));
    dbg("calloc done");
    render_init(framebuffer);
    dbg("render_init done");
    scan_directory(current_path);
    dbg("scan_directory done");
    if (settings_load_recents && recent_games_get_count() > 0)
        enter_recents_view();   /* "Start in Recents" setting */
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
    if (banner_is_loaded())
        banner_render(framebuffer);
    else
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
    snprintf(line, sizeof(line), "Font: < %s >",
             font_count > 0 ? font_disp[settings_font_idx] : "(none)");
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
    /* Auto-resume row */
    snprintf(line, sizeof(line), "Auto-resume: < %s >", onoff_names[settings_auto_resume]);
    int y3 = y2 + ITEM_HEIGHT;
    if (settings_menu_idx == SETTINGS_ROW_AUTORESUME) {
        render_text_pillbox(framebuffer, PADDING, y3, line, COLOR_SELECT_BG, COLOR_SELECT_TEXT, 7);
    } else {
        font_draw_text(framebuffer, SCREEN_WIDTH, SCREEN_HEIGHT, PADDING, y3, line, COLOR_TEXT);
    }
    /* Animations row */
    snprintf(line, sizeof(line), "Animations: < %s >", onoff_names[settings_anim]);
    int y4 = y3 + ITEM_HEIGHT;
    if (settings_menu_idx == SETTINGS_ROW_ANIM) {
        render_text_pillbox(framebuffer, PADDING, y4, line, COLOR_SELECT_BG, COLOR_SELECT_TEXT, 7);
    } else {
        font_draw_text(framebuffer, SCREEN_WIDTH, SCREEN_HEIGHT, PADDING, y4, line, COLOR_TEXT);
    }
    /* Hide empty folders row */
    snprintf(line, sizeof(line), "Hide Empty Folders: < %s >", onoff_names[settings_hide_empty]);
    int y5 = y4 + ITEM_HEIGHT;
    if (settings_menu_idx == SETTINGS_ROW_HIDEEMPTY) {
        render_text_pillbox(framebuffer, PADDING, y5, line, COLOR_SELECT_BG, COLOR_SELECT_TEXT, 7);
    } else {
        font_draw_text(framebuffer, SCREEN_WIDTH, SCREEN_HEIGHT, PADDING, y5, line, COLOR_TEXT);
    }
    /* Game switcher row */
    snprintf(line, sizeof(line), "Game Switcher: < %s >", onoff_names[settings_game_switcher]);
    int y6 = y5 + ITEM_HEIGHT;
    if (settings_menu_idx == SETTINGS_ROW_SWITCHER) {
        render_text_pillbox(framebuffer, PADDING, y6, line, COLOR_SELECT_BG, COLOR_SELECT_TEXT, 7);
    } else {
        font_draw_text(framebuffer, SCREEN_WIDTH, SCREEN_HEIGHT, PADDING, y6, line, COLOR_TEXT);
    }
    /* Load to recents row */
    snprintf(line, sizeof(line), "Start in Recents: < %s >", onoff_names[settings_load_recents]);
    int y7 = y6 + ITEM_HEIGHT;
    if (settings_menu_idx == SETTINGS_ROW_LOADRECENTS) {
        render_text_pillbox(framebuffer, PADDING, y7, line, COLOR_SELECT_BG, COLOR_SELECT_TEXT, 7);
    } else {
        font_draw_text(framebuffer, SCREEN_WIDTH, SCREEN_HEIGHT, PADDING, y7, line, COLOR_TEXT);
    }
    /* Button Mapping row */
    int y8 = y7 + ITEM_HEIGHT;
    if (settings_menu_idx == SETTINGS_ROW_REMAP) {
        render_text_pillbox(framebuffer, PADDING, y8, "Button Mapping", COLOR_SELECT_BG, COLOR_SELECT_TEXT, 7);
    } else {
        font_draw_text(framebuffer, SCREEN_WIDTH, SCREEN_HEIGHT, PADDING, y8, "Button Mapping", COLOR_TEXT);
    }
    render_legend(framebuffer, LEGEND_X_NONE, 0, 0);
}

static void render_remap_wizard(void) {
    if (banner_is_loaded())
        banner_render(framebuffer);
    else
        render_clear_screen(framebuffer);
    render_header(framebuffer, "BUTTON MAPPING");

    char line[128];
    int y = START_Y;
    snprintf(line, sizeof(line), "Press  %s  (%d / %d)", input_btn_name((FrogButton)remap_step), remap_step + 1, FROG_BTN_COUNT);
    font_draw_text(framebuffer, SCREEN_WIDTH, SCREEN_HEIGHT, PADDING, y, line, COLOR_TEXT);

    y += ITEM_HEIGHT;
    font_draw_text(framebuffer, SCREEN_WIDTH, SCREEN_HEIGHT, PADDING, y, "[B] = skip / keep default", COLOR_TEXT);
}

static void render_core_picker(void) {
    if (banner_is_loaded())
        banner_render(framebuffer);
    else
        render_clear_screen(framebuffer);
    render_header(framebuffer, "SELECT CORE");

    int y = START_Y;
    /* subtitle: which game/folder we're overriding */
    font_draw_text(framebuffer, SCREEN_WIDTH, SCREEN_HEIGHT, PADDING, y, core_picker_title, COLOR_TEXT);
    y += ITEM_HEIGHT;

    int rows = VISIBLE_ENTRIES - 1;   /* one row used by the subtitle */
    if (rows < 1) rows = 1;
    int visible = min(core_choice_count - core_picker_scroll, rows);
    for (int i = 0; i < visible; i++) {
        int idx = core_picker_scroll + i;
        const char *line = core_choices[idx].name;
        int ry = y + i * ITEM_HEIGHT;
        if (idx == core_picker_idx)
            render_text_pillbox(framebuffer, PADDING, ry, line, COLOR_SELECT_BG, COLOR_SELECT_TEXT, 7);
        else
            font_draw_text(framebuffer, SCREEN_WIDTH, SCREEN_HEIGHT, PADDING, ry, line, COLOR_TEXT);
    }
    render_legend(framebuffer, LEGEND_X_NONE, 0, 0);
}

static void render_search_kbd(void) {
    render_clear_screen(framebuffer);
    render_header(framebuffer, "SEARCH");

    int y = START_Y;
    char q[96];
    snprintf(q, sizeof(q), "> %s_", search_query);
    font_draw_text(framebuffer, SCREEN_WIDTH, SCREEN_HEIGHT, PADDING, y, q, COLOR_TEXT);
    y += ITEM_HEIGHT + UI_S(8);

    int cw = UI_S(26), ch = ITEM_HEIGHT;
    for (int r = 0; r < KBD_NROWS; r++) {
        int rl = kbd_row_len(r);
        int ry = y + r * ch;
        for (int c = 0; c < rl; c++) {
            char lbl[8];
            int cx;
            if (r == KBD_SPECIAL_ROW) { snprintf(lbl, sizeof(lbl), "%s", KBD_SPECIAL[c]); cx = PADDING + c * (cw * 3); }
            else { lbl[0] = KBD_ROWS[r][c]; lbl[1] = '\0'; cx = PADDING + c * cw; }
            if (r == search_kbd_r && c == search_kbd_c)
                render_text_pillbox(framebuffer, cx, ry, lbl, COLOR_SELECT_BG, COLOR_SELECT_TEXT, 7);
            else
                font_draw_text(framebuffer, SCREEN_WIDTH, SCREEN_HEIGHT, cx, ry, lbl, COLOR_TEXT);
        }
    }
    render_legend(framebuffer, LEGEND_X_NONE, 0, 0);
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
        if (settings_menu_active) {
            banner_path = "settings";
        } else if (!viewing_recents && !viewing_favourites &&
                   selected_index >= 0 && selected_index < entry_count) {
            if (strcmp(entries[selected_index].name, SETTINGS_ENTRY_NAME) == 0) {
                banner_path = "settings";
            } else if (entries[selected_index].is_dir && strcmp(current_path, ROMS_PATH) == 0) {
                snprintf(sel_path, sizeof(sel_path), "%s/%s",
                         current_path, entries[selected_index].name);
                banner_path = sel_path;
            }
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

    if (search_kbd_active) {
        render_search_kbd();
    } else if (core_picker_active) {
        render_core_picker();
    } else if (remap_wizard_active) {
        render_remap_wizard();
    } else if (settings_menu_active) {
        render_settings_menu();
    } else if (viewing_recents && settings_game_switcher) {
        if (banner_is_loaded())
            banner_render(framebuffer);
        else
            render_clear_screen(framebuffer);
        render_game_switcher(framebuffer);
    } else {
        if (banner_is_loaded())
            banner_render(framebuffer);
        else
            render_clear_screen(framebuffer);
        static char search_title[96];
        const char *title;
        if (viewing_search) {
            snprintf(search_title, sizeof(search_title), "SEARCH: %s (%d)", search_query, entry_count);
            title = search_title;
        } else {
            title = viewing_recents    ? "RECENT GAMES" :
                    viewing_favourites ? "FAVOURITES" :
                    (strcmp(current_path, ROMS_PATH) == 0)
                    ? "TREEFROGUI: SYSTEMS" : get_basename(current_path);
        }
        render_header(framebuffer, title);
        {
            int visible = min(entry_count - scroll_offset, VISIBLE_ENTRIES);
            for (int i = 0; i < visible; i++) {
                int idx = scroll_offset + i;
                render_menu_item(framebuffer, idx, entries[idx].name, entries[idx].is_dir,
                                 (idx == selected_index), scroll_offset, 0);
            }
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
        /* SELECT opens the core picker on folders + real ROMs (matches the SELECT
         * handler guard): show the "SEL-OPTIONS" hint there. */
        int show_select = (!viewing_recents && !viewing_favourites && !viewing_search &&
                           selected_index < entry_count &&
                           strcmp(entries[selected_index].name, SETTINGS_ENTRY_NAME) != 0 &&
                           strcmp(entries[selected_index].name, RECENTS_ENTRY_NAME) != 0 &&
                           strcmp(entries[selected_index].name, FAVOURITES_ENTRY_NAME) != 0);
        /* X opens search in the normal browser (systems root + any game folder). */
        int show_search = (!viewing_recents && !viewing_favourites && !viewing_search);

        /* Play-time for the selected game (browse view). */
        if (!viewing_recents && !viewing_favourites && !viewing_search &&
            selected_index < entry_count && !entries[selected_index].is_dir &&
            strcmp(entries[selected_index].name, SETTINGS_ENTRY_NAME) != 0 &&
            strcmp(entries[selected_index].name, RECENTS_ENTRY_NAME) != 0 &&
            strcmp(entries[selected_index].name, FAVOURITES_ENTRY_NAME) != 0) {
            char fp[1024];
            snprintf(fp, sizeof fp, "%s/%s", current_path, entries[selected_index].name);
            long s = playtime_lookup(fp);
            if (s > 0) {
                char t[64]; long h = s/3600, m = (s%3600)/60;
                if (h) snprintf(t, sizeof t, "Played %ldh %ldm", h, m);
                else   snprintf(t, sizeof t, "Played %ldm", m);
                font_draw_text(framebuffer, SCREEN_WIDTH, SCREEN_HEIGHT, PADDING,
                               SCREEN_HEIGHT - 56, t, COLOR_TEXT);
            }
        }

        render_legend(framebuffer, legend_mode, show_select, show_search);
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
