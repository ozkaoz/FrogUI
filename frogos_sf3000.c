/*
 * FrogUI - MinUI-style File Browser for SF3000
 * Standalone launcher for picoarch
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>
#include <sys/wait.h>
#include <errno.h>
#include <fcntl.h>
#include <dlfcn.h>

#include "font.h"
#include "render.h"
#include "theme.h"
#include "recent_games.h"
#include "favorites.h"
#include "settings.h"
#include "fbdev.h"
#include "input.h"

// SF3000 paths
#define SDCARD_BASE "/mnt/sdcard"
#define CORES_PATH SDCARD_BASE "/cubegm/cores"
#define ROMS_PATH SDCARD_BASE "/roms"
#define PICOARCH_PATH SDCARD_BASE "/cubegm/picoarch"

// Console to core path mapping for SF3000
typedef struct {
    const char *console_name;
    const char *core_path;
} ConsoleMapping;


// Folder names match stock SF3000 SD card convention (case-insensitive via strcasecmp)
static const ConsoleMapping console_mappings[] = {
    {"FC",    CORES_PATH "/fceumm_libretro.so"},           // Famicom / NES
    {"NES",   CORES_PATH "/quicknes_libretro.so"},         // NES alt (faster)
    {"GBA",   CORES_PATH "/mgba_libretro.so"},             // Game Boy Advance
    {"SFC",   CORES_PATH "/snes9x2005_plus_libretro.so"}, // Super Famicom / SNES
    {"MD",    CORES_PATH "/picodrive_libretro.so"},        // Mega Drive / Genesis
    {"SMS",   CORES_PATH "/picodrive_libretro.so"},        // Master System
    {"GG",    CORES_PATH "/picodrive_libretro.so"},        // Game Gear
    {"Quake", CORES_PATH "/tyrquake_libretro.so"},         // Quake (tyrquake)
    {NULL, NULL}
};
// Get core path for console folder
static const char* get_core_path_for_console(const char* console_name) {
    for (int i = 0; console_mappings[i].console_name != NULL; i++) {
        if (strcasecmp(console_mappings[i].console_name, console_name) == 0) {
            return console_mappings[i].core_path;
        }
    }
    return NULL;
}

#ifndef min
#define min(a, b) ((a) < (b) ? (a) : (b))
#endif

#define MAX_PATH_LEN 512
#define MAX_RECENT_GAMES 10
#define INITIAL_ENTRIES_CAPACITY 64

// Directory entry
typedef struct {
    char name[256];
    int is_dir;
} DirEntry;

// Application state
static DirEntry *entries = NULL;
static int entry_count = 0;
static int entry_capacity = 0;
static char current_path[MAX_PATH_LEN] = ROMS_PATH;
static int selected_index = 0;
static int scroll_offset = 0;
static int game_launched = 0;

// Framebuffer
static uint16_t *framebuffer = NULL;

// Logging function
static void log_message(const char *msg) {
    fprintf(stderr, "FROGUI: %s\n", msg);  // goes to frogui.log via icube redirect
    FILE *log = fopen("/mnt/sdcard/frogui_debug.log", "a");
    if (log) {
        fprintf(log, "%s\n", msg);
        fflush(log);
        fsync(fileno(log));
        fclose(log);
    }
}

// Get basename from path
static const char* get_basename(const char *path) {
    const char *basename = strrchr(path, '/');
    return basename ? basename + 1 : path;
}

// Scan directory and populate entries list
static void scan_directory(const char *path) {
    DIR *dir = opendir(path);
    if (!dir) {
        printf("Failed to open directory: %s\n", path);
        return;
    }

    // Clear existing entries
    entry_count = 0;

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_name[0] == '.') continue;  // Skip hidden files

        // Check if it's a directory
        struct stat statbuf;
        char full_path[MAX_PATH_LEN];
        snprintf(full_path, sizeof(full_path), "%s/%s", path, entry->d_name);

        if (stat(full_path, &statbuf) != 0) continue;

        // Skip non-game files and non-directories
        // Show all files except metadata files
        if (!S_ISDIR(statbuf.st_mode)) {
            const char *ext = strrchr(entry->d_name, '.');
            if (ext) {
                // Skip metadata and system files
                if (strcasecmp(ext, ".csv") == 0 ||
                    strcasecmp(ext, ".txt") == 0 ||
                    strcasecmp(ext, ".xml") == 0 ||
                    strcasecmp(ext, ".jpg") == 0 ||
                    strcasecmp(ext, ".png") == 0) {
                    continue;
                }
            }
        }

        // Resize array if needed
        if (entry_count >= entry_capacity) {
            entry_capacity = entry_capacity == 0 ? INITIAL_ENTRIES_CAPACITY : entry_capacity * 2;
            entries = realloc(entries, entry_capacity * sizeof(DirEntry));
            if (!entries) {
                printf("Memory allocation failed\n");
                closedir(dir);
                return;
            }
        }

        // Add entry
        strncpy(entries[entry_count].name, entry->d_name, sizeof(entries[entry_count].name) - 1);
        entries[entry_count].name[sizeof(entries[entry_count].name) - 1] = '\0';
        entries[entry_count].is_dir = S_ISDIR(statbuf.st_mode);
        entry_count++;
    }

    closedir(dir);

    // Sort entries (directories first, then alphabetically)
    for (int i = 0; i < entry_count - 1; i++) {
        for (int j = i + 1; j < entry_count; j++) {
            if (entries[i].is_dir < entries[j].is_dir ||
                (entries[i].is_dir == entries[j].is_dir &&
                 strcasecmp(entries[i].name, entries[j].name) > 0)) {
                DirEntry temp = entries[i];
                entries[i] = entries[j];
                entries[j] = temp;
            }
        }
    }

    // Reset selection
    selected_index = 0;
    scroll_offset = 0;
}

// Launch picoarch with selected game
static void launch_picoarch(const char *core_path, const char *rom_path) {
    char msg[512];
    snprintf(msg, sizeof(msg), "launch_picoarch: core=%s rom=%s", core_path, rom_path);
    log_message(msg);

    log_message("fbdev_deinit before fork");
    fbdev_deinit();

    pid_t pid = fork();

    if (pid == 0) {
        setenv("SDCARD_PATH", SDCARD_BASE, 1);
        setenv("LD_LIBRARY_PATH", SDCARD_BASE "/cubegm/lib:" SDCARD_BASE "/cubegm/usr/lib", 1);

        char *const argv[] = {
            PICOARCH_PATH,
            (char*)core_path,
            (char*)rom_path,
            NULL
        };

        execv(PICOARCH_PATH, argv);
        // Only reached if execv fails
        FILE *f = fopen("/mnt/sdcard/frogui_debug.log", "a");
        if (f) { fprintf(f, "execv FAILED: %s\n", strerror(errno)); fclose(f); }
        _exit(1);
    } else if (pid > 0) {
        int status;
        waitpid(pid, &status, 0);
        char m[128];
        snprintf(m, sizeof(m), "picoarch exited: status=%d wexitstatus=%d wifexited=%d",
                 status, WEXITSTATUS(status), WIFEXITED(status));
        log_message(m);
    } else {
        log_message("ERROR: fork failed");
        fbdev_init();
        return;
    }

    log_message("fbdev_init after picoarch");
    if (fbdev_init() != 0) {
        log_message("CRITICAL: fbdev_init failed after picoarch exit");
        return;
    }
    log_message("fbdev_init OK");

    game_launched = 0;
    render_clear_screen(framebuffer);
    log_message("calling fbdev_blit to restore display");
    fbdev_blit(framebuffer, SCREEN_WIDTH, SCREEN_HEIGHT);
    log_message("fbdev_blit done - FrogUI should be visible");
}

// Handle button input
static void handle_input(void) {
    input_update();

    static bool a_pressed_last = false;
    static bool b_pressed_last = false;
    static bool up_pressed_last = false;
    static bool down_pressed_last = false;

    bool a_pressed = input_is_pressed(FROG_BTN_A);
    bool b_pressed = input_is_pressed(FROG_BTN_B);
    bool up_pressed = input_is_pressed(FROG_BTN_UP);
    bool down_pressed = input_is_pressed(FROG_BTN_DOWN);

    // A button - launch game or enter directory
    if (a_pressed && !a_pressed_last && selected_index < entry_count) {
        if (entries[selected_index].is_dir) {
            // Enter directory
            char new_path[MAX_PATH_LEN];
            snprintf(new_path, sizeof(new_path), "%s/%s", current_path, entries[selected_index].name);
            strncpy(current_path, new_path, sizeof(current_path) - 1);
            current_path[sizeof(current_path) - 1] = '\0';
            scan_directory(current_path);
        } else {
            // Launch game
            const char *folder_name = get_basename(current_path);
            const char *core_path = get_core_path_for_console(folder_name);

            if (core_path) {
                char rom_path[MAX_PATH_LEN];
                snprintf(rom_path, sizeof(rom_path), "%s/%s", current_path, entries[selected_index].name);

                game_launched = 1;
                render_clear_screen(framebuffer);
                fbdev_blit(framebuffer, SCREEN_WIDTH, SCREEN_HEIGHT);

                launch_picoarch(core_path, rom_path);
            } else {
                printf("No core found for console: %s\n", folder_name);
            }
        }
    }

    // B button - go back
    if (b_pressed && !b_pressed_last) {
        if (strcmp(current_path, ROMS_PATH) != 0) {
            // Go to parent directory
            char *last_slash = strrchr(current_path, '/');
            if (last_slash) {
                *last_slash = '\0';
                scan_directory(current_path);
            }
        }
    }

    // D-pad navigation
    if (up_pressed && !up_pressed_last) {
        if (selected_index > 0) {
            selected_index--;
            if (selected_index < scroll_offset) {
                scroll_offset = selected_index;
            }
        }
    }

    if (down_pressed && !down_pressed_last) {
        if (selected_index < entry_count - 1) {
            selected_index++;
            if (selected_index >= scroll_offset + VISIBLE_ENTRIES) {
                scroll_offset = selected_index - VISIBLE_ENTRIES + 1;
            }
        }
    }

    a_pressed_last = a_pressed;
    b_pressed_last = b_pressed;
    up_pressed_last = up_pressed;
    down_pressed_last = down_pressed;
}

// Render current screen
static void render_screen(void) {
    render_clear_screen(framebuffer);

    const char *title = (strcmp(current_path, ROMS_PATH) == 0) ? "FROGUI: SYSTEMS" : get_basename(current_path);
    render_header(framebuffer, title);

    int visible_count = min(entry_count - scroll_offset, VISIBLE_ENTRIES);
    for (int i = 0; i < visible_count; i++) {
        int idx = scroll_offset + i;
        int is_selected = (idx == selected_index);
        render_menu_item(framebuffer, i, entries[idx].name, entries[idx].is_dir,
                         is_selected, scroll_offset, 0);
    }

    render_legend(framebuffer, LEGEND_X_NONE);
    fbdev_blit(framebuffer, SCREEN_WIDTH, SCREEN_HEIGHT);
}

// Main application loop
int main(int argc, char **argv) {
    (void)argc;
    (void)argv;

    log_message("=== FrogUI starting ===");

    // Initialize systems
    log_message("Initializing framebuffer...");
    if (fbdev_init() != 0) {
        log_message("ERROR: Failed to initialize framebuffer");
        fprintf(stderr, "Failed to initialize framebuffer\n");
        return 1;
    }
    log_message("Framebuffer OK");

    log_message("Initializing input...");
    if (input_init() != 0) {
        log_message("ERROR: Failed to initialize input");
        fprintf(stderr, "Failed to initialize input\n");
        fbdev_deinit();
        return 1;
    }
    log_message("Input OK");

    log_message("Initializing font...");
    font_init();
    log_message("Font OK");

    log_message("Initializing theme...");
    theme_init();
    log_message("Theme OK");

    settings_init();
    recent_games_init();
    favorites_init();

    log_message("Allocating framebuffer memory...");
    // Allocate framebuffer
    framebuffer = calloc(SCREEN_WIDTH * SCREEN_HEIGHT, sizeof(uint16_t));
    if (!framebuffer) {
        log_message("ERROR: Failed to allocate framebuffer memory");
        fprintf(stderr, "Failed to allocate framebuffer\n");
        input_deinit();
        fbdev_deinit();
        return 1;
    }
    log_message("Framebuffer memory allocated");

    render_init(framebuffer);
    log_message("Render system initialized");

    // Scan initial directory
    log_message("Scanning ROMS directory...");
    scan_directory(current_path);
    char scan_msg[256];
    snprintf(scan_msg, sizeof(scan_msg), "Found %d entries in %s", entry_count, current_path);
    log_message(scan_msg);

    printf("FrogUI started - %dx%d\n", SCREEN_WIDTH, SCREEN_HEIGHT);

    // Diagnostic: write opaque red to EVERY byte of ALL fb pages
    // Tells us which page (0-3) the display controller is reading from
    {
        extern void fbdev_fill_all_pages_red(void);
        fbdev_fill_all_pages_red();
        log_message("DIAG: all pages filled red - should see red SOMEWHERE");
        sleep(3);
        log_message("DIAG: sleep done");
    }

    log_message("Entering main loop...");

    // Main loop
    while (1) {
        handle_input();
        render_screen();
        usleep(16667);  // ~60 FPS
    }

    // Cleanup (shouldn't reach here)
    log_message("FrogUI exiting unexpectedly");
    free(framebuffer);
    input_deinit();
    fbdev_deinit();

    return 0;
}
