#include "arcade_bricks.h"
#include <string.h>
#include <limits.h>

static int clamp(int n, int lo, int hi) { return n < lo ? lo : n > hi ? hi : n; }

void arcade_bricks_reset(arcade_bricks_t *g) {
    memset(g, 0, sizeof *g);
    for (int i = 0; i < BRICKS_ROWS; ++i) g->brick_rows[i] = 0xff;
    g->hard_rows[0]=0xff;g->hard_rows[2]=0x55;
    g->x = 500; g->y = 690;
    g->vx = 9; g->vy = -14;
    g->paddle_x = (BRICKS_FIELD - BRICKS_PADDLE_W) / 2;
    g->lives = 3;
}
void arcade_bricks_move(arcade_bricks_t *g, int center_x) {
    /* Clamp before subtraction: touch coordinates can be INT_MIN/INT_MAX. */
    int half = BRICKS_PADDLE_W / 2;
    if (center_x <= half) g->paddle_x = 0;
    else if (center_x >= BRICKS_FIELD - half)
        g->paddle_x = BRICKS_FIELD - BRICKS_PADDLE_W;
    else g->paddle_x = center_x - half;
}
void arcade_bricks_tick(arcade_bricks_t *g) {
    if (g->over || g->won) return;
    int nx = g->x + g->vx;
    int ny = g->y + g->vy;
    if (nx <= 15 || nx >= BRICKS_FIELD - 15) {
        g->vx = -g->vx;
        nx = clamp(nx, 15, BRICKS_FIELD - 15);
    }
    if (ny <= 15) { g->vy = -g->vy; ny = 15; }

    /* Five brick rows at y=100..349. The ball does not use per-pixel assets. */
    if (ny >= 100 && ny < 350) {
        int row = (ny - 100) / 50;
        int col = clamp(nx / (BRICKS_FIELD / BRICKS_COLS), 0, BRICKS_COLS - 1);
        int old_row = (g->y >= 100 && g->y < 350) ? (g->y - 100) / 50 : -1;
        int old_col = clamp(g->x / (BRICKS_FIELD / BRICKS_COLS), 0, BRICKS_COLS - 1);
        /* Contact is a *crossing* into a brick cell, not merely being inside
         * its logical row. This prevents successive hits while bouncing away. */
        if ((row != old_row || col != old_col) &&
            (g->brick_rows[row] & (1u << col))) {
            uint8_t bit=(uint8_t)(1u<<col);
            if(g->hard_rows[row]&bit) {
                g->hard_rows[row]&=(uint8_t)~bit;
                g->score = g->score > UINT32_MAX - 5u ? UINT32_MAX : g->score + 5u;
            } else {
                g->brick_rows[row]&=(uint8_t)~bit;
                if (g->combo < INT_MAX) g->combo++;
                if (g->combo > g->best_combo) g->best_combo = g->combo;
                uint32_t reward = (uint32_t)(10 + (g->combo < 10 ? g->combo : 10) * 2);
                g->score = g->score > UINT32_MAX - reward ? UINT32_MAX : g->score + reward;
            }
            /* Resolve side contacts horizontally, top/bottom contacts
             * vertically. Restore the pre-contact coordinate to avoid a
             * repeated hit while the ball rebounds out of the brick. */
            if (col != old_col && row == old_row) {
                g->vx = -g->vx;
                nx = g->x;
            } else {
                g->vy = -g->vy;
                ny = g->y;
            }
            bool any = false;
            for (int i = 0; i < BRICKS_ROWS; ++i) if (g->brick_rows[i]) any = true;
            if (!any) g->won = true;
        }
    }
    /* Paddle spans y=895..915; bounce angle depends on where ball lands. */
    if (g->vy > 0 && g->y < 895 && ny >= 895 &&
        nx >= g->paddle_x - 15 && nx <= g->paddle_x + BRICKS_PADDLE_W + 15) {
        int relative = nx - (g->paddle_x + BRICKS_PADDLE_W / 2);
        g->vx = clamp(relative / 9, -16, 16);
        if (!g->vx) g->vx = 2;
        g->vy = -14;
        ny = 885;
    }
    if (ny > BRICKS_FIELD + 15) {
        --g->lives;g->combo=0;
        if (!g->lives) { g->over = true; return; }
        g->x = 500; g->y = 690;
        g->vx = 9; g->vy = -14;
        return;
    }
    g->x = nx; g->y = ny;
}
