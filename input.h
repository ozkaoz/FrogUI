/*
 * input.h - Input device interface
 */

#ifndef INPUT_H
#define INPUT_H

#include <stdint.h>
#include <stdbool.h>

// Button enumeration matching SF3000 layout
typedef enum {
    FROG_BTN_UP = 0,
    FROG_BTN_DOWN,
    FROG_BTN_LEFT,
    FROG_BTN_RIGHT,
    FROG_BTN_A,
    FROG_BTN_B,
    FROG_BTN_X,
    FROG_BTN_Y,
    FROG_BTN_L,
    FROG_BTN_R,
    FROG_BTN_L2,
    FROG_BTN_R2,
    FROG_BTN_SELECT,
    FROG_BTN_START,
    FROG_BTN_COUNT
} FrogButton;

// Initialize input system (cubevol shared memory)
int input_init(void);

// Clean up input resources
void input_deinit(void);

// Process all pending input events
void input_update(void);

// Check if button is currently pressed
bool input_is_pressed(FrogButton btn);

// Check if button was just pressed this frame
bool input_was_pressed(FrogButton btn);

// Get full button state as bitmask
uint32_t input_get_state(void);

#endif // INPUT_H
