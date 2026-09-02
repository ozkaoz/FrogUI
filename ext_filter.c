/*
 * ext_filter.c - per-system-folder extension whitelist for the ROM browser.
 * See ext_filter.h.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "ext_filter.h"

#define FILTERS_FILE "/mnt/sdcard/frogui/ext_filters.txt"

typedef struct {
    char folder[32];
    char exts[EXT_FILTER_MAX_EXTS][EXT_FILTER_EXT_LEN + 1];
    int  ext_count;
    int  enabled;
} ExtFilter;

static ExtFilter filters[EXT_FILTER_MAX_FOLDERS];
static int filter_count = 0;

static ExtFilter *find(const char *folder_name) {
    if (!folder_name) return NULL;
    for (int i = 0; i < filter_count; i++)
        if (strcasecmp(filters[i].folder, folder_name) == 0)
            return &filters[i];
    return NULL;
}

static ExtFilter *find_or_create(const char *folder_name) {
    ExtFilter *f = find(folder_name);
    if (f) return f;
    if (!folder_name || !folder_name[0]) return NULL;
    if (filter_count >= EXT_FILTER_MAX_FOLDERS) return NULL;
    f = &filters[filter_count];
    strncpy(f->folder, folder_name, sizeof(f->folder) - 1);
    f->folder[sizeof(f->folder) - 1] = '\0';
    f->ext_count = 0;
    f->enabled = 1;
    filter_count++;
    return f;
}

/* Normalize "ext" or ".ext" (any case) to lowercase without dot. Returns 0
 * on success, -1 on empty/too-long input. */
static int norm_ext(const char *ext, char *out, size_t n) {
    if (!ext) return -1;
    while (*ext == '.') ext++;
    if (!*ext) return -1;
    size_t i = 0;
    for (; ext[i] && i < n - 1; i++)
        out[i] = (char)tolower((unsigned char)ext[i]);
    if (ext[i]) return -1;                 /* too long: reject */
    out[i] = '\0';
    return 0;
}

static void save_file(void) {
    FILE *f = fopen(FILTERS_FILE, "w");
    if (!f) return;
    for (int i = 0; i < filter_count; i++) {
        if (filters[i].ext_count == 0) continue;
        fprintf(f, "%s|%d|", filters[i].folder, filters[i].enabled ? 1 : 0);
        for (int e = 0; e < filters[i].ext_count; e++)
            fprintf(f, "%s%s", e ? "," : "", filters[i].exts[e]);
        fputc('\n', f);
    }
    fclose(f);
}

/* PS1 games ship as .cue + several .bin track blobs; listing every track
 * makes the same game appear many times. Whitelist the loadable formats so
 * each game shows once. ps1r uses the same disc formats. */
static void apply_builtin_defaults(void) {
    static const char *disc_exts[] = { "cue", "m3u", "pbp", "iso", "chd", "img", "mdf", NULL };
    static const char *folders[]   = { "ps1", "psx", "PS", "ps1r", NULL };
    for (int f = 0; folders[f]; f++)
        for (int e = 0; disc_exts[e]; e++)
            ext_filter_add(folders[f], disc_exts[e]);
}

void ext_filter_load(void) {
    filter_count = 0;
    apply_builtin_defaults();
    FILE *f = fopen(FILTERS_FILE, "r");
    if (!f) return;
    char line[256];
    while (filter_count < EXT_FILTER_MAX_FOLDERS && fgets(line, sizeof line, f)) {
        char *nl = strpbrk(line, "\r\n"); if (nl) *nl = '\0';
        char *bar1 = strchr(line, '|');
        if (!bar1) continue;
        *bar1 = '\0';
        char *enabled = bar1 + 1;
        char *bar2 = strchr(enabled, '|');
        if (!bar2) continue;
        *bar2 = '\0';
        char *list = bar2 + 1;
        if (line[0] == '\0' || list[0] == '\0') continue;
        ExtFilter *fl = find(line);        /* defaults may already exist */
        if (!fl) fl = find_or_create(line);
        if (!fl) continue;
        fl->ext_count = 0;
        fl->enabled = atoi(enabled) != 0;
        char *tok = strtok(list, ",");
        while (tok && fl->ext_count < EXT_FILTER_MAX_EXTS) {
            char norm[EXT_FILTER_EXT_LEN + 1];
            if (norm_ext(tok, norm, sizeof norm) == 0)
                strncpy(fl->exts[fl->ext_count++], norm, EXT_FILTER_EXT_LEN + 1);
            tok = strtok(NULL, ",");
        }
    }
    fclose(f);
}

bool ext_filter_should_hide(const char *folder_name, const char *file_name) {
    if (!folder_name || !file_name) return false;
    const ExtFilter *f = find(folder_name);
    if (!f || !f->enabled || f->ext_count == 0) return false;
    /* No extension: an extension whitelist can't vouch for it. */
    const char *dot = strrchr(file_name, '.');
    if (!dot || !dot[1]) return true;
    char ext[EXT_FILTER_EXT_LEN + 1];
    if (norm_ext(dot + 1, ext, sizeof ext) != 0) return true;
    for (int e = 0; e < f->ext_count; e++)
        if (strcmp(f->exts[e], ext) == 0) return false;
    return true;
}

bool ext_filter_folder_active(const char *folder_name) {
    const ExtFilter *f = find(folder_name);
    return f && f->enabled && f->ext_count > 0;
}

bool ext_filter_has_list(const char *folder_name) {
    const ExtFilter *f = find(folder_name);
    return f && f->ext_count > 0;
}

void ext_filter_add(const char *folder_name, const char *ext) {
    char norm[EXT_FILTER_EXT_LEN + 1];
    if (norm_ext(ext, norm, sizeof norm) != 0) return;
    ExtFilter *f = find_or_create(folder_name);
    if (!f) return;
    for (int e = 0; e < f->ext_count; e++)
        if (strcmp(f->exts[e], norm) == 0) return;   /* dedupe */
    if (f->ext_count >= EXT_FILTER_MAX_EXTS) return;
    strncpy(f->exts[f->ext_count], norm, EXT_FILTER_EXT_LEN + 1);
    f->ext_count++;
}

void ext_filter_set_enabled(const char *folder_name, bool enabled) {
    ExtFilter *f = find(folder_name);
    if (!f) return;
    f->enabled = enabled ? 1 : 0;
    save_file();
}

bool ext_filter_get_enabled(const char *folder_name) {
    const ExtFilter *f = find(folder_name);
    return f ? f->enabled != 0 : false;
}

bool ext_filter_has_ext(const char *folder_name, const char *ext) {
    const ExtFilter *f = find(folder_name);
    if (!f) return false;
    char norm[EXT_FILTER_EXT_LEN + 1];
    if (norm_ext(ext, norm, sizeof norm) != 0) return false;
    for (int e = 0; e < f->ext_count; e++)
        if (strcmp(f->exts[e], norm) == 0) return true;
    return false;
}

int ext_filter_ext_count(const char *folder_name) {
    const ExtFilter *f = find(folder_name);
    return f ? f->ext_count : 0;
}

int ext_filter_get_ext_at(const char *folder_name, int idx, char *out, size_t n) {
    const ExtFilter *f = find(folder_name);
    if (!f || idx < 0 || idx >= f->ext_count || !out || n == 0) return 0;
    strncpy(out, f->exts[idx], n - 1);
    out[n - 1] = '\0';
    return 1;
}

bool ext_filter_toggle_ext(const char *folder_name, const char *ext) {
    char norm[EXT_FILTER_EXT_LEN + 1];
    if (norm_ext(ext, norm, sizeof norm) != 0) return false;
    ExtFilter *f = find_or_create(folder_name);
    if (!f) return false;
    for (int e = 0; e < f->ext_count; e++) {
        if (strcmp(f->exts[e], norm) == 0) {
            for (int k = e; k < f->ext_count - 1; k++)
                memcpy(f->exts[k], f->exts[k + 1], EXT_FILTER_EXT_LEN + 1);
            f->ext_count--;
            save_file();
            return true;
        }
    }
    if (f->ext_count >= EXT_FILTER_MAX_EXTS) return false;
    strncpy(f->exts[f->ext_count], norm, EXT_FILTER_EXT_LEN + 1);
    f->ext_count++;
    f->enabled = 1;
    save_file();
    return true;
}
