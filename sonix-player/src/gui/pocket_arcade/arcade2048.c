#include "arcade2048.h"

#include <string.h>

static uint32_t next_random(arcade2048_t *game) {
    uint32_t x = game->rng ? game->rng : UINT32_C(0x6d2b79f5);
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    game->rng = x;
    return x;
}

static void add_random_tile(arcade2048_t *game) {
    int empty[16];
    int count = 0;
    for (int i = 0; i < 16; ++i) {
        if (game->cells[i] == 0) empty[count++] = i;
    }
    if (count == 0) return;
    int index = empty[next_random(game) % (unsigned)count];
    game->cells[index] = next_random(game) % 10 == 0 ? 4 : 2;
}

void arcade2048_reset(arcade2048_t *game, uint32_t seed) {
    memset(game, 0, sizeof(*game));
    game->rng = seed ? seed : UINT32_C(0xa341316c);
    add_random_tile(game);
    add_random_tile(game);
}

static int cell_index(arcade_dir_t direction, int line, int position) {
    switch (direction) {
    case ARCADE_LEFT:  return line * 4 + position;
    case ARCADE_RIGHT: return line * 4 + (3 - position);
    case ARCADE_UP:    return position * 4 + line;
    case ARCADE_DOWN:  return (3 - position) * 4 + line;
    default:           return -1;
    }
}

bool arcade2048_move(arcade2048_t *game, arcade_dir_t direction) {
    if (direction < ARCADE_UP || direction > ARCADE_LEFT) return false;
    bool changed = false;
    uint32_t old_score=game->score, merges=0;
    for (int line = 0; line < 4; ++line) {
        uint32_t values[4] = {0, 0, 0, 0};
        uint32_t merged[4] = {0, 0, 0, 0};
        int n = 0;
        for (int i = 0; i < 4; ++i) {
            uint32_t val = game->cells[cell_index(direction, line, i)];
            if (val) values[n++] = val;
        }
        int out = 0;
        for (int i = 0; i < n; ++i) {
            if (i + 1 < n && values[i] == values[i + 1] &&
                values[i] <= UINT32_MAX / 2) {
                merged[out] = values[i] * 2;
                merges++;
                /* Score must saturate instead of silently dropping points
                 * when a very long run approaches the 32-bit limit. */
                game->score = UINT32_MAX - game->score < merged[out]
                                  ? UINT32_MAX : game->score + merged[out];
                ++i; /* A freshly merged tile cannot merge again this turn. */
            } else {
                merged[out] = values[i];
            }
            ++out;
        }
        for (int i = 0; i < 4; ++i) {
            int pos = cell_index(direction, line, i);
            if (game->cells[pos] != merged[i]) changed = true;
            game->cells[pos] = merged[i];
        }
    }
    if (changed) {
        game->last_gain=game->score-old_score;
        game->last_merges=merges;
        game->merge_streak=merges ? (game->merge_streak<UINT32_MAX ? game->merge_streak+1 : UINT32_MAX) : 0;
        if(game->merge_streak>game->best_streak)game->best_streak=game->merge_streak;
        add_random_tile(game);
    }
    return changed;
}

bool arcade2048_has_moves(const arcade2048_t *game) {
    for (int i = 0; i < 16; ++i) {
        if (game->cells[i] == 0) return true;
    }
    for (int r = 0; r < 4; ++r) {
        for (int c = 0; c < 4; ++c) {
            uint32_t n = game->cells[r * 4 + c];
            if (c < 3 && n == game->cells[r * 4 + c + 1]) return true;
            if (r < 3 && n == game->cells[(r + 1) * 4 + c]) return true;
        }
    }
    return false;
}

bool arcade2048_won(const arcade2048_t *game) {
    for (int i = 0; i < 16; ++i) {
        if (game->cells[i] >= 2048) return true;
    }
    return false;
}
