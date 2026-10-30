#ifndef POCKET_ARCADE_2048_H
#define POCKET_ARCADE_2048_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    ARCADE_UP = 0,
    ARCADE_RIGHT = 1,
    ARCADE_DOWN = 2,
    ARCADE_LEFT = 3
} arcade_dir_t;

typedef struct {
    uint32_t cells[16];
    uint32_t score;
    uint32_t rng;
    uint32_t last_gain, last_merges, merge_streak, best_streak;
} arcade2048_t;

/* Independent of LVGL/audio so the rules can be tested on a PC. */
void arcade2048_reset(arcade2048_t *game, uint32_t seed);
/* Returns false when the board did not change; does not spawn a tile then. */
bool arcade2048_move(arcade2048_t *game, arcade_dir_t direction);
bool arcade2048_has_moves(const arcade2048_t *game);
bool arcade2048_won(const arcade2048_t *game);

#endif
