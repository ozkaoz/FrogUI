#ifndef RENDER_H
#define RENDER_H

#include <stdint.h>
#include <stddef.h>
#include "theme.h"

/*
 * Screen dimensions + UI scale are RUNTIME values (single dynamic core supports
 * both SF3000 854x480 and R36SX 640x480). picoarch detects the panel and passes
 * it via env (TF_PANEL_W / TF_PANEL_H / TF_UI_SCALE); retro_init reads them and
 * calls render_set_geometry() before any layout/font init. Defaults below apply
 * if env is absent. UI_SCALE: baseline layout designed for 480x320 (100 = 1x).
 */
extern int g_screen_w;   /* panel width  px */
extern int g_screen_h;   /* panel height px */
extern int g_ui_scale;   /* integer scale, 100 = 1x of 480x320 baseline */

#define SCREEN_WIDTH  g_screen_w
#define SCREEN_HEIGHT g_screen_h
#define UI_SCALE      g_ui_scale

/* Set the runtime geometry. Call once in retro_init before font/layout use. */
void render_set_geometry(int w, int h, int ui_scale);

/* Scale a baseline constant by UI_SCALE (runtime) */
#define UI_S(x) ((x) * UI_SCALE / 100)

// Colors are now provided by the theme system
#define COLOR_BG          theme_bg()
#define COLOR_TEXT        theme_text()
#define COLOR_SELECT_BG   theme_select_bg()
#define COLOR_SELECT_TEXT theme_select_text()
#define COLOR_HEADER      theme_header()
#define COLOR_FOLDER      theme_folder()
#define COLOR_LEGEND      theme_legend()
#define COLOR_LEGEND_BG   theme_legend_bg()
#define COLOR_DISABLED    theme_disabled()

// Layout — all derived from UI_SCALE
#define HEADER_HEIGHT UI_S(30)
#define ITEM_HEIGHT   UI_S(24)
#define PADDING       UI_S(16)
#define START_Y       UI_S(40)
#define VISIBLE_ENTRIES ((SCREEN_HEIGHT - START_Y - ITEM_HEIGHT) / ITEM_HEIGHT)

// Thumbnail layout
#define THUMBNAIL_AREA_X     UI_S(160)
#define THUMBNAIL_AREA_Y     UI_S(40)
#define THUMBNAIL_MAX_WIDTH  UI_S(160)
#define THUMBNAIL_MAX_HEIGHT UI_S(200)

// Text — more chars visible at larger scales (wider screen)
#define MAX_FILENAME_DISPLAY_LEN   (20 * UI_SCALE / 100)
#define MAX_UNSELECTED_DISPLAY_LEN (10 * UI_SCALE / 100)
#define SCROLL_DELAY_FRAMES 60
#define SCROLL_SPEED_FRAMES 8

// Initialize rendering system
void render_init(uint16_t *framebuffer);

// Clear screen with background color
void render_clear_screen(uint16_t *framebuffer);

// Draw a filled rectangle
void render_fill_rect(uint16_t *framebuffer, int x, int y, int width, int height, uint16_t color);

// Draw a rounded rectangle (pill shape)
void render_rounded_rect(uint16_t *framebuffer, int x, int y, int width, int height, int radius, uint16_t color);

// Draw a text pillbox with proper padding (unified method)
void render_text_pillbox(uint16_t *framebuffer, int x, int y, const char *text,
                        uint16_t bg_color, uint16_t text_color, int padding);

// Draw menu header with title
void render_header(uint16_t *framebuffer, const char *title);

// Legend modes for X button
#define LEGEND_X_NONE      0
#define LEGEND_X_FAVOURITE 1
#define LEGEND_X_REMOVE    2

// Draw menu legend at bottom
void render_legend(uint16_t *framebuffer, int x_button_mode, int show_select);

// Draw a menu item (file or folder)
void render_menu_item(uint16_t *framebuffer, int index, const char *name, int is_dir,
                     int is_selected, int scroll_offset, int is_favorited);

// Thumbnail functions
typedef struct {
    uint16_t *data;  // Changed to uint16_t for ARGB8888
    int width;
    int height;
} Thumbnail;

// Load thumbnail from PNG file
int load_thumbnail(const char *png_path, Thumbnail *thumb);

// Load raw RGB565 file (fallback) - converts to ARGB8888
int load_raw_rgb565(const char *path, Thumbnail *thumb);

// Free thumbnail memory
void free_thumbnail(Thumbnail *thumb);

// Draw thumbnail in the thumbnail area
void render_thumbnail(uint16_t *framebuffer, const Thumbnail *thumb);

// Get thumbnail path for a given game file
void get_thumbnail_path(const char *game_path, char *thumb_path, size_t thumb_path_size);

#endif // RENDER_H