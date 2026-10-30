#ifndef ARCADE_STACK_H
#define ARCADE_STACK_H
#include <stdbool.h>
#include <stdint.h>
#define STACK_HISTORY 6
#define STACK_FIELD 1000

typedef struct {
    int base_x, width, moving_x, direction, speed;
    int layer_x[STACK_HISTORY], layer_width[STACK_HISTORY];
    int placed, last_shave, perfect_streak, snap_rewards;
    bool over;
    uint32_t rng;
} arcade_stack_t;
void arcade_stack_reset(arcade_stack_t *s, uint32_t seed);
void arcade_stack_tick(arcade_stack_t *s);
/* Drop moving platform. Returns true on a successful landing. */
bool arcade_stack_drop(arcade_stack_t *s);
#endif
