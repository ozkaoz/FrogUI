/*
 * input.c - Cubevol shared memory input for SF3000
 * Reads button state from /tmp/joy_key shared memory.
 * Remap table loaded from KEYMAP_FILE; falls back to hardcoded defaults.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include "input.h"

static volatile uint32_t *cubevol_keys = NULL;
static int shmid = -1;

static uint32_t current_state = 0;
static uint32_t prev_state    = 0;

/* Default raw bit for each logical button (-1 = unmapped). */
static const int default_bits[FROG_BTN_COUNT] = {
    [FROG_BTN_UP]     = 4,
    [FROG_BTN_DOWN]   = 6,
    [FROG_BTN_LEFT]   = 7,
    [FROG_BTN_RIGHT]  = 5,
    [FROG_BTN_A]      = 13,
    [FROG_BTN_B]      = 14,
    [FROG_BTN_X]      = 12,
    [FROG_BTN_Y]      = 15,
    [FROG_BTN_L1]     = 10,
    [FROG_BTN_R1]     = 11,
    [FROG_BTN_L2]     = -1,
    [FROG_BTN_R2]     = -1,
    [FROG_BTN_START]  = 3,
    [FROG_BTN_SELECT] = 0,
};

static int remap_bits[FROG_BTN_COUNT];
static uint32_t remap_raw_masks[FROG_BTN_COUNT]; /* precomputed: 1<<bit or 0 if unmapped */
static uint32_t remap_logical_bits[FROG_BTN_COUNT]; /* precomputed: 1<<logical */

static void rebuild_masks(void) {
    for (int i = 0; i < FROG_BTN_COUNT; i++) {
        int b = remap_bits[i];
        remap_raw_masks[i]    = (b >= 0 && b <= 15) ? (1u << b) : 0;
        remap_logical_bits[i] = 1u << i;
    }
}

static const char *btn_names[FROG_BTN_COUNT] = {
    "UP","DOWN","LEFT","RIGHT","A","B","X","Y","L1","R1","L2","R2","START","SELECT"
};

const char *input_btn_name(FrogButton btn) {
    if (btn < 0 || btn >= FROG_BTN_COUNT) return "?";
    return btn_names[btn];
}

void input_reset_defaults(void) {
    for (int i = 0; i < FROG_BTN_COUNT; i++)
        remap_bits[i] = default_bits[i];
    rebuild_masks();
}

int input_init(void) {
    input_reset_defaults();
    key_t key = ftok("/tmp/joy_key", 'a');
    if (key == (key_t)-1) return -1;
    shmid = shmget(key, 4, 0666);
    if (shmid < 0) return -1;
    cubevol_keys = (volatile uint32_t *)shmat(shmid, NULL, 0);
    if (cubevol_keys == (void *)-1) { cubevol_keys = NULL; return -1; }
    current_state = 0;
    prev_state    = 0;
    return 0;
}

void input_deinit(void) {
    if (cubevol_keys) { shmdt((void *)cubevol_keys); cubevol_keys = NULL; }
    shmid = -1;
}

/* Right-stick directions reach us as the A/B/X/Y bits (cubevol merges the analog
 * stick into the same GPIO matrix bits as the face buttons — no separable signal
 * anywhere). In games that's fine (stick acts like the buttons), but in menus the
 * stick's drift and accidental brushes fire false A/B/X/Y. We can't tell a real
 * press from the stick (same bits), so we debounce: a face bit must stay set for
 * FACE_HOLD consecutive frames before it registers. Drift glances and quick
 * brushes (shorter than that) are dropped; a deliberate tap/hold passes with a
 * small latency. Release clears instantly. Dpad and the rest stay instant.
 * This runs only in the FrogUI menu (games read the shm directly), so in-game
 * response is unaffected. */
/* Raw state from a libretro frontend (rkgame/picoarch), set each frame by the core
 * via input_set_ext_raw — this is picoarch's ALREADY-debounced input. */
static uint32_t ext_raw = 0;

#define FACE_BITS 0xF000u
#define FACE_HOLD 4   /* frames (~65ms @ 60fps) a face bit must be held to count */
#define NAV_HOLD  3   /* dpad/shoulders/start/select: ~50ms — kills the two-writer
                       * (rkgame+cubevol) flicker that ghosts the menu */

void input_update(void) {
    /* Combine the raw joy_key shm with ext_raw (picoarch's ALREADY-debounced input
     * via input_state_cb). The shm alone flickers from the rkgame+cubevol two-writer
     * race → ghost menu inputs; ORing+debouncing below removes that. */
    uint32_t raw = (cubevol_keys ? (*cubevol_keys & 0xFFFF) : 0) | (ext_raw & 0xFFFF);

    /* Per-bit hold debounce on ALL 16 bits (was face-only — that left dpad ghosting).
     * Face bits hold longer (stick drift); the rest use NAV_HOLD. Release clears
     * instantly so let-go stays snappy. */
    static uint8_t cnt[16] = {0};
    static uint32_t committed = 0;
    for (int b = 0; b < 16; b++) {
        uint32_t m = 1u << b;
        int hold = (m & FACE_BITS) ? FACE_HOLD : NAV_HOLD;
        if (raw & m) {
            if (cnt[b] < 255) cnt[b]++;
            if (cnt[b] >= hold) committed |= m;
        } else {
            cnt[b] = 0;
            committed &= ~m;
        }
    }
    raw = committed;

    prev_state = current_state;
    uint32_t s = 0;
    for (int i = 0; i < FROG_BTN_COUNT; i++) {
        if (raw & remap_raw_masks[i])
            s |= remap_logical_bits[i];
    }
    current_state = s;
}

bool input_is_pressed(FrogButton btn) {
    return (current_state >> btn) & 1;
}

bool input_was_pressed(FrogButton btn) {
    return ((current_state >> btn) & 1) && !((prev_state >> btn) & 1);
}

void input_set_ext_raw(uint32_t raw) { ext_raw = raw; }

uint32_t input_get_raw_state(void) {
    uint32_t shm = cubevol_keys ? (*cubevol_keys & 0xFFFF) : 0;
    return shm | ext_raw;
}

void input_set_raw_bit(FrogButton btn, int raw_bit) {
    if (btn >= 0 && btn < FROG_BTN_COUNT) {
        remap_bits[btn] = raw_bit;
        rebuild_masks();
    }
}

int input_get_raw_bit(FrogButton btn) {
    if (btn < 0 || btn >= FROG_BTN_COUNT) return -1;
    return remap_bits[btn];
}

int input_load_remap(const char *path) {
    static int loaded = 0;
    if (loaded) return 0;
    FILE *f = fopen(path, "r");
    if (!f) { loaded = 1; return -1; }
    char line[64];
    while (fgets(line, sizeof(line), f)) {
        char *eq = strchr(line, '=');
        if (!eq) continue;
        *eq = '\0';
        int val = atoi(eq + 1);
        for (int i = 0; i < FROG_BTN_COUNT; i++) {
            if (strcmp(line, btn_names[i]) == 0) {
                remap_bits[i] = val;
                break;
            }
        }
    }
    fclose(f);
    rebuild_masks();
    loaded = 1;
    return 0;
}

int input_save_remap(const char *path) {
    FILE *f = fopen(path, "w");
    if (!f) return -1;
    for (int i = 0; i < FROG_BTN_COUNT; i++)
        fprintf(f, "%s=%d\n", btn_names[i], remap_bits[i]);
    fflush(f);
    fclose(f);
    return 0;
}
