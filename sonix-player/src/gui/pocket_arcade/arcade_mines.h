#ifndef ARCADE_MINES_H
#define ARCADE_MINES_H
#include <stdbool.h>
#include <stdint.h>
#define MINES_COLS 8
#define MINES_ROWS 6
#define MINES_CELLS (MINES_COLS * MINES_ROWS)
#define MINES_COUNT 8

typedef struct {
    uint8_t mine[MINES_CELLS];
    uint8_t adjacent[MINES_CELLS];
    uint8_t revealed[MINES_CELLS];
    uint8_t flagged[MINES_CELLS];
    uint32_t rng;
    unsigned uncovered;
    bool planted, over, won;
} arcade_mines_t;
void arcade_mines_reset(arcade_mines_t *g, uint32_t seed);
/* Returns false if index is invalid or already resolved. First reveal is safe. */
bool arcade_mines_reveal(arcade_mines_t *g, int index);
bool arcade_mines_toggle_flag(arcade_mines_t *g, int index);
bool arcade_mines_chord(arcade_mines_t *g, int index);
#endif
