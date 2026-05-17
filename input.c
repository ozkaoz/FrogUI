/*
 * input.c - Cubevol shared memory input for SF3000
 * Reads button state from /tmp/joy_key shared memory
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <unistd.h>
#include "input.h"

// Pointer to cubevol shared memory (4 bytes, low 16 bits = button state)
static volatile uint32_t *cubevol_keys = NULL;
static int shmid = -1;

// Button state tracking
static uint32_t current_state = 0;

int input_init(void) {
    FILE *log = fopen("/mnt/sdcard/frogui_debug.log", "a");
    if (log) fprintf(log, "input_init: Connecting to cubevol...\n");

    // Get shared memory key for /tmp/joy_key
    key_t key = ftok("/tmp/joy_key", 'a');
    if (key == (key_t)-1) {
        if (log) {
            fprintf(log, "input_init: ERROR - ftok failed\n");
            fclose(log);
        }
        return -1;
    }

    // Get shared memory segment (4 bytes)
    shmid = shmget(key, 4, 0666);
    if (shmid < 0) {
        if (log) {
            fprintf(log, "input_init: ERROR - shmget failed (cubevol not running?)\n");
            fclose(log);
        }
        return -1;
    }

    // Attach to shared memory
    cubevol_keys = (volatile uint32_t *)shmat(shmid, NULL, 0);
    if (cubevol_keys == (void *)-1) {
        if (log) {
            fprintf(log, "input_init: ERROR - shmat failed\n");
            fclose(log);
        }
        cubevol_keys = NULL;
        return -1;
    }

    if (log) {
        fprintf(log, "input_init: Connected to cubevol (shmid=%d, ptr=%p)\n", shmid, cubevol_keys);
        fclose(log);
    }

    // Read initial state
    current_state = *cubevol_keys & 0xFFFF;
    return 0;
}

void input_deinit(void) {
    if (cubevol_keys != NULL) {
        shmdt((void *)cubevol_keys);
        cubevol_keys = NULL;
    }
    shmid = -1;
}

// SF3000 button bit positions (from picoarch)
#define BTN_UP_BIT     4
#define BTN_DOWN_BIT   6
#define BTN_LEFT_BIT   7
#define BTN_RIGHT_BIT  5
#define BTN_A_BIT     13
#define BTN_B_BIT     14
#define BTN_X_BIT     12
#define BTN_Y_BIT     15
#define BTN_L_BIT     10
#define BTN_R_BIT     11
#define BTN_SELECT_BIT 0
#define BTN_START_BIT  3

// Process all pending input events
void input_update(void) {
    if (cubevol_keys == NULL) {
        return;
    }

    // Read current button state from cubevol (low 16 bits only)
    uint32_t raw_state = *cubevol_keys & 0xFFFF;

    // Convert cubevol bit format to our button bitmask
    current_state = 0;

    if (raw_state & (1 << BTN_UP_BIT))     current_state |= (1 << FROG_BTN_UP);
    if (raw_state & (1 << BTN_DOWN_BIT))   current_state |= (1 << FROG_BTN_DOWN);
    if (raw_state & (1 << BTN_LEFT_BIT))   current_state |= (1 << FROG_BTN_LEFT);
    if (raw_state & (1 << BTN_RIGHT_BIT))  current_state |= (1 << FROG_BTN_RIGHT);
    if (raw_state & (1 << BTN_A_BIT))      current_state |= (1 << FROG_BTN_A);
    if (raw_state & (1 << BTN_B_BIT))      current_state |= (1 << FROG_BTN_B);
    if (raw_state & (1 << BTN_X_BIT))      current_state |= (1 << FROG_BTN_X);
    if (raw_state & (1 << BTN_Y_BIT))      current_state |= (1 << FROG_BTN_Y);
    if (raw_state & (1 << BTN_L_BIT))      current_state |= (1 << FROG_BTN_L);
    if (raw_state & (1 << BTN_R_BIT))      current_state |= (1 << FROG_BTN_R);
    if (raw_state & (1 << BTN_SELECT_BIT)) current_state |= (1 << FROG_BTN_SELECT);
    if (raw_state & (1 << BTN_START_BIT))  current_state |= (1 << FROG_BTN_START);
}

// Check if button is currently pressed
bool input_is_pressed(FrogButton btn) {
    return (current_state & (1 << btn)) != 0;
}

// Check if button was just pressed this frame
bool input_was_pressed(FrogButton btn) {
    // For simplicity, this just checks current state
    // A proper implementation would track previous state
    return input_is_pressed(btn);
}

// Get full button state as bitmask
uint32_t input_get_state(void) {
    return current_state;
}
