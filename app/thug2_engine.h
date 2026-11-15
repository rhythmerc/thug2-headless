/* The THUG2 engine, stepped by its host one game frame at a time (the C API skate3-quest-ar calls).
 *
 * thug2_boot starts the game on a thread of its own and returns once the game's main loop is ready for its first
 * frame. After that the game and its host take turns, never running together: thug2_step hands the game a pad, lets it
 * run exactly one frame of its main loop (logic only, flat out: no 60 Hz wait, nothing drawn), and returns once the
 * game is back at the top of its loop - the skater as that frame left it. The handover is the game's own loop pacing
 * hook (an extension's loop_pace): the decomp's main loop is not changed. */
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* A pad as the game reads one: sticks 0-255 (0x80 at rest; up and left are 0), buttons by bit. */
enum {
    THUG2_CROSS = 1 << 0,
    THUG2_CIRCLE = 1 << 1,
    THUG2_SQUARE = 1 << 2,
    THUG2_TRIANGLE = 1 << 3,
    THUG2_L1 = 1 << 4,
    THUG2_R1 = 1 << 5,
    THUG2_L2 = 1 << 6,
    THUG2_R2 = 1 << 7,
};
typedef struct Thug2Pad {
    uint8_t left_x, left_y, right_x, right_y;
    uint16_t buttons;
} Thug2Pad;

/* The local skater after a frame. state: 0 ground, 1 air, 2 wall, 3 lip, 4 rail, 5 wallplant, -1 unknown. Game units
 * (inches), world space. */
typedef struct Thug2Skater {
    int present;
    float position[3];
    float velocity[3];
    int state;
    uint32_t frame;
} Thug2Skater;

/* Boots the game from `game_dir` (the folder holding Data/) with the game's command line (e.g. "-level", "BO").
 * Returns 0 when the first frame is ready; non-zero if the game ended before it got there. */
int thug2_boot(const char* game_dir, int argc, const char* const* argv);
/* One frame with `pad` (null: at rest); `skater` (may be null) as the frame left it. Returns 0, or non-zero once the
 * game has ended. */
int thug2_step(const Thug2Pad* pad, Thug2Skater* skater);
/* Asks the game to end (its next frame quits the loop). */
void thug2_quit(void);

#ifdef __cplusplus
}
#endif
