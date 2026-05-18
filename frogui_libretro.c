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
#include <dirent.h>
#include <stdbool.h>

#include "libretro.h"
#include "font.h"
#include "render.h"
#include "theme.h"
#include "recent_games.h"
#include "favorites.h"
#include "settings.h"

#define SDCARD_BASE  "/mnt/sdcard"
#define CORES_PATH   SDCARD_BASE "/cubegm/cores"
#define ROMS_PATH    SDCARD_BASE "/roms"
#define LAUNCH_FILE  "/tmp/frogui_launch.txt"

/* Console → core mapping (folder name determines core) */
typedef struct { const char *console_name; const char *core_path; } ConsoleMapping;
static const ConsoleMapping console_mappings[] = {
    {"FC",    CORES_PATH "/fceumm_libretro.so"},
    {"NES",   CORES_PATH "/quicknes_libretro.so"},
    {"GBA",   CORES_PATH "/mgba_libretro.so"},
    {"SFC",   CORES_PATH "/snes9x2005_plus_libretro.so"},
    {"MD",    CORES_PATH "/picodrive_libretro.so"},
    {"SMS",   CORES_PATH "/picodrive_libretro.so"},
    {"GG",    CORES_PATH "/picodrive_libretro.so"},
    {"Quake", CORES_PATH "/tyrquake_libretro.so"},
    {NULL, NULL}
};

static const char* get_core_for_folder(const char *folder) {
    for (int i = 0; console_mappings[i].console_name; i++)
        if (strcasecmp(console_mappings[i].console_name, folder) == 0)
            return console_mappings[i].core_path;
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

static const char* get_basename(const char *path) {
    const char *b = strrchr(path, '/');
    return b ? b+1 : path;
}

#define SETTINGS_ENTRY_NAME ">> Settings"

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
    /* Append Settings shortcut at root level */
    if (strcmp(path, ROMS_PATH) == 0) {
        if (entry_count >= entry_capacity) {
            entry_capacity = entry_capacity ? entry_capacity*2 : INITIAL_ENTRIES_CAPACITY;
            entries = realloc(entries, entry_capacity * sizeof(DirEntry));
        }
        strncpy(entries[entry_count].name, SETTINGS_ENTRY_NAME, 255);
        entries[entry_count].name[255] = '\0';
        entries[entry_count].is_dir = 0;  /* treat as file so it appears last */
        entry_count++;
    }

    selected_index = 0;
    scroll_offset  = 0;
}

static void request_game_launch(const char *core_path, const char *rom_path) {
    /* Write launch info so icube can read it after picoarch exits */
    FILE *f = fopen(LAUNCH_FILE, "w");
    if (f) {
        fprintf(f, "%s\n%s\n", core_path, rom_path);
        fflush(f);
        fclose(f);
    }
    shutdown_requested = true;
}

static void handle_input(void) {
    if (!input_poll_cb || !input_state_cb) return;
    input_poll_cb();

    static bool a_last=false, b_last=false, up_last=false, dn_last=false;

    bool a  = input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_A);
    bool b  = input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_B);
    bool up = input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_UP);
    bool dn = input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_DOWN);

    if (a && !a_last && selected_index < entry_count) {
        if (entries[selected_index].is_dir) {
            char new_path[MAX_PATH_LEN];
            snprintf(new_path, sizeof(new_path), "%s/%s", current_path, entries[selected_index].name);
            strncpy(current_path, new_path, MAX_PATH_LEN-1);
            current_path[MAX_PATH_LEN-1] = '\0';
            scan_directory(current_path);
        } else {
            /* Settings shortcut */
            if (strcmp(entries[selected_index].name, SETTINGS_ENTRY_NAME) == 0) {
                settings_load();
                settings_show_menu();
            } else {
                const char *folder = get_basename(current_path);
                const char *core   = get_core_for_folder(folder);
                if (core) {
                    char rom_path[MAX_PATH_LEN];
                    snprintf(rom_path, sizeof(rom_path), "%s/%s", current_path, entries[selected_index].name);
                    request_game_launch(core, rom_path);
                }
            }
        }
    }

    if (b && !b_last && strcmp(current_path, ROMS_PATH) != 0) {
        char *slash = strrchr(current_path, '/');
        if (slash) { *slash = '\0'; scan_directory(current_path); }
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

    a_last=a; b_last=b; up_last=up; dn_last=dn;
}

/* ---- libretro API ---- */

unsigned retro_api_version(void) { return RETRO_API_VERSION; }

void retro_get_system_info(struct retro_system_info *info) {
    memset(info, 0, sizeof(*info));
    info->library_name     = "FrogUI";
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

static void dbg(const char *msg) {
    FILE *f = fopen("/mnt/sdcard/frogui_crash.log", "a");
    if (f) { fputs(msg, f); fputs("\n", f); fclose(f); }
}

void retro_init(void) {
    dbg("retro_init start");
    font_init();
    dbg("font_init done");
    theme_init();
    dbg("theme_init done");
    settings_init();
    dbg("settings_init done");
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

void retro_run(void) {
    handle_input();

    /* Render menu to RGB565 framebuffer */
    render_clear_screen(framebuffer);
    const char *title = (strcmp(current_path, ROMS_PATH) == 0)
                        ? "FROGUI: SYSTEMS" : get_basename(current_path);
    render_header(framebuffer, title);
    int visible = min(entry_count - scroll_offset, VISIBLE_ENTRIES);
    for (int i = 0; i < visible; i++) {
        int idx = scroll_offset + i;
        render_menu_item(framebuffer, i, entries[idx].name, entries[idx].is_dir,
                         (idx == selected_index), scroll_offset, 0);
    }
    render_legend(framebuffer, LEGEND_X_NONE);

    /* Hand frame to picoarch — it handles display rotation/scaling */
    if (video_cb)
        video_cb(framebuffer, SCREEN_WIDTH, SCREEN_HEIGHT, SCREEN_WIDTH * sizeof(uint16_t));

    /* Signal picoarch to exit so icube can launch the selected game */
    if (shutdown_requested && environ_cb)
        environ_cb(RETRO_ENVIRONMENT_SHUTDOWN, NULL);
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
