#include "arcade_racer.h"

#include <limits.h>
#include <string.h>

static uint32_t next_random(arcade_racer_t *game) {
    uint32_t value = game->rng ? game->rng : 0x9e3779b9u;
    value ^= value << 13;
    value ^= value >> 17;
    value ^= value << 5;
    return game->rng = value;
}

void arcade_racer_reset(arcade_racer_t *game, uint32_t seed) {
    memset(game, 0, sizeof *game);
    game->rng = seed ? seed : 0x3c6ef372u;
    game->player_lane = 1;
    for (int i = 0; i < RACER_ROWS; i++) {
        game->traffic[i] = -1;
    }
}

void arcade_racer_steer(arcade_racer_t *game, int direction) {
    /* Only a deliberate one-lane move should be able to collide here.
     * Ignore malformed input and release/no-movement callbacks. */
    if (game->over || direction < -1 || direction > 1 || direction == 0) {
        return;
    }
    int lane = game->player_lane + direction;
    if (lane >= 0 && lane < RACER_LANES) {
        game->player_lane = lane;
    }
    if (game->traffic[RACER_ROWS - 1] == game->player_lane && !game->boost_ticks) {
        game->over = true;
    }
}

bool arcade_racer_boost(arcade_racer_t *game) {
    if (game->over || game->nitro < 3 || game->boost_ticks) {
        return false;
    }
    game->nitro -= 3;
    game->boost_ticks = 3;
    if (game->boost_uses < UINT_MAX) {
        game->boost_uses++;
    }
    return true;
}

uint32_t arcade_racer_period_ms(const arcade_racer_t *game) {
    /* Each completed 100 points raises scroll SPEED 3% (period / 1.03).
     * Rounded to whole milliseconds for LVGL and capped at 80ms so the
     * 3-lane game remains human-playable on a 240x320 music player.
     * Lookup is O(1), with no floating point or per-tick division. */
    static const uint8_t period_ms[] = {145, 141, 137, 133, 129, 125, 121, 118, 114, 111, 108, 105, 102, 99, 96, 93, 90, 88, 85, 83, 80, 80, 80, 80, 80, 80, 80, 80, 80, 80, 80};
    if (!game) return period_ms[0];
    uint32_t tier = game->score / 100u;
    if (tier >= sizeof(period_ms) / sizeof(period_ms[0]))
        tier = (uint32_t)(sizeof(period_ms) / sizeof(period_ms[0]) - 1u);
    return period_ms[tier];
}

void arcade_racer_tick(arcade_racer_t *game) {
    if (game->over) {
        return;
    }
    for (int row = RACER_ROWS - 1; row > 0; --row) {
        game->traffic[row] = game->traffic[row - 1];
    }
    uint32_t random = next_random(game);
    /* Never produce a dense wall of consecutive hazards. Each spawned car
     * must be separated from the previous one by at least THREE empty rows.
     * With one obstacle per row and three lanes, this gives a player time
     * for the two single-lane moves needed to cross the entire road before
     * the next car reaches the bottom. Do not require Nitro to survive. */
    bool spawn_clear = true;
    for (int row = 1; row <= 3; ++row) {
        if (game->traffic[row] >= 0) { spawn_clear = false; break; }
    }
    unsigned density = game->score >= 220u ? 4u : 3u;
    game->traffic[0] = (spawn_clear && random % 5u < density)
                           ? (int8_t)((random >> 8) % RACER_LANES)
                           : -1;
    int obstacle = game->traffic[RACER_ROWS - 1];
    if (obstacle >= 0 && obstacle != game->player_lane &&
        (obstacle - game->player_lane == 1 || game->player_lane - obstacle == 1)) {
        if (game->near_misses < UINT_MAX) {
            game->near_misses++;
        }
        if (game->nitro < 3) {
            game->nitro++;
        }
    }
    if (obstacle == game->player_lane && !game->boost_ticks) {
        game->over = true;
    }
    if (!game->over) {
        uint32_t gain = game->boost_ticks ? 3u : 1u;
        if (UINT32_MAX - game->score < gain) {
            game->score = UINT32_MAX;
        } else {
            game->score += gain;
        }
    }
    if (game->boost_ticks) {
        game->boost_ticks--;
    }
}
