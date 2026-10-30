#include "arcade_mines.h"
#include <string.h>

static uint32_t next_random(arcade_mines_t *g) {
    uint32_t x = g->rng;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    return g->rng = x;
}
static bool neighbor(int a, int b) {
    int dx = a % MINES_COLS - b % MINES_COLS;
    int dy = a / MINES_COLS - b / MINES_COLS;
    return dx >= -1 && dx <= 1 && dy >= -1 && dy <= 1;
}
void arcade_mines_reset(arcade_mines_t *g, uint32_t seed) {
    memset(g, 0, sizeof *g);
    g->rng = seed ? seed : 0x9e3779b9u;
}
static void plant(arcade_mines_t *g, int first) {
    int placed = 0;
    while (placed < MINES_COUNT) {
        int index = (int)(next_random(g) % MINES_CELLS);
        if (g->mine[index] || neighbor(index, first)) continue;
        g->mine[index] = 1;
        placed++;
    }
    /* Each mine influences only eight surrounding cells; do not compare
     * every cell against every other cell during board generation. */
    for (int index = 0; index < MINES_CELLS; ++index) {
        if (!g->mine[index]) continue;
        int x = index % MINES_COLS, y = index / MINES_COLS;
        for (int dy = -1; dy <= 1; ++dy) {
            for (int dx = -1; dx <= 1; ++dx) {
                if (!dx && !dy) continue;
                int nx = x + dx, ny = y + dy;
                if (nx >= 0 && nx < MINES_COLS && ny >= 0 && ny < MINES_ROWS)
                    g->adjacent[ny * MINES_COLS + nx]++;
            }
        }
    }
    g->planted = true;
}
bool arcade_mines_toggle_flag(arcade_mines_t *g, int index) {
    if (index < 0 || index >= MINES_CELLS || g->over || g->won || g->revealed[index]) return false;
    g->flagged[index] ^= 1;
    return true;
}
bool arcade_mines_reveal(arcade_mines_t *g, int index) {
    if (index < 0 || index >= MINES_CELLS || g->over || g->won || g->revealed[index] || g->flagged[index]) return false;
    if (!g->planted) plant(g, index);
    if (g->mine[index]) { g->over = true; return true; }
    int queue[MINES_CELLS], head = 0, tail = 0;
    g->revealed[index] = 1;
    g->uncovered++;
    queue[tail++] = index;
    while (head < tail) {
        int current = queue[head++];
        if (g->adjacent[current]) continue;
        for (int i = 0; i < MINES_CELLS; ++i) {
            if (!neighbor(i, current) || g->revealed[i] || g->flagged[i] || g->mine[i]) continue;
            g->revealed[i] = 1;
            g->uncovered++;
            /* Every cell enqueues at most once; tail stays <= MINES_CELLS. */
            queue[tail++] = i;
        }
    }
    if (g->uncovered == MINES_CELLS - MINES_COUNT) g->won = true;
    return true;
}
bool arcade_mines_chord(arcade_mines_t *g,int index) {
    if(!g || index<0 || index>=MINES_CELLS || !g->revealed[index] ||
       !g->adjacent[index] || g->over || g->won)return false;
    unsigned flags=0;
    for(int i=0;i<MINES_CELLS;i++)
        if(i!=index && neighbor(i,index) && g->flagged[i])flags++;
    if(flags!=g->adjacent[index])return false;
    bool any=false;
    for(int i=0;i<MINES_CELLS && !g->over;i++)
        if(i!=index && neighbor(i,index) && !g->flagged[i] &&
           !g->revealed[i])any=arcade_mines_reveal(g,i)||any;
    return any;
}
