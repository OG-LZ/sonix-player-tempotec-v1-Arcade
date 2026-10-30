#ifndef ARCADE_RACER_H
#define ARCADE_RACER_H
#include <stdbool.h>
#include <stdint.h>
#define RACER_ROWS 8
#define RACER_LANES 3

typedef struct {
    int8_t traffic[RACER_ROWS];  /* -1 empty, 0..2 occupied */
    int player_lane;
    uint32_t score, rng;
    unsigned nitro, near_misses, boost_ticks, boost_uses;
    bool over;
} arcade_racer_t;
void arcade_racer_reset(arcade_racer_t *g, uint32_t seed);
void arcade_racer_steer(arcade_racer_t *g, int direction);
void arcade_racer_tick(arcade_racer_t *g);
bool arcade_racer_boost(arcade_racer_t *g);
/* +3% speed for each 100 points, 145ms baseline, 80ms playability floor. */
uint32_t arcade_racer_period_ms(const arcade_racer_t *g);
#endif
