#ifndef ARCADE_BRICKS_H
#define ARCADE_BRICKS_H
#include <stdbool.h>
#include <stdint.h>
#define BRICKS_COLS 8
#define BRICKS_ROWS 5
#define BRICKS_FIELD 1000
#define BRICKS_PADDLE_W 210

typedef struct {
    int x, y, vx, vy, paddle_x;
    uint8_t brick_rows[BRICKS_ROWS]; /* 1 bit for each of eight columns */
    uint8_t hard_rows[BRICKS_ROWS]; /* still armored until first impact */
    int combo, best_combo;
    uint32_t score;
    int lives;
    bool over, won;
} arcade_bricks_t;
void arcade_bricks_reset(arcade_bricks_t *g);
void arcade_bricks_move(arcade_bricks_t *g, int center_x);
void arcade_bricks_tick(arcade_bricks_t *g);
#endif
