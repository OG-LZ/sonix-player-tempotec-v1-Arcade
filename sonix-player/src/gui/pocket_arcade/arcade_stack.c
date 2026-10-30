#include "arcade_stack.h"
#include <string.h>
#include <limits.h>

void arcade_stack_reset(arcade_stack_t *s, uint32_t seed) {
    memset(s, 0, sizeof *s);
    s->rng = seed ? seed : 0xC18E78ADu;
    s->width = 600;
    s->base_x = 200;
    s->moving_x = 0;
    s->direction = 1;
    s->speed = 12;
}

void arcade_stack_tick(arcade_stack_t *s) {
    if (s->over) return;
    int next = s->moving_x + s->direction * s->speed;
    if (next < 0) {
        next = -next;
        s->direction = 1;
    } else if (next > STACK_FIELD - s->width) {
        next = 2 * (STACK_FIELD - s->width) - next;
        s->direction = -1;
    }
    s->moving_x = next;
}

bool arcade_stack_drop(arcade_stack_t *s) {
    if (s->over) return false;
    /* A tiny accuracy grace window rewards good timing and makes the
     * experience fair on a resistive/small touchscreen and slow frame tick. */
    int off=s->moving_x-s->base_x;
    int tolerance=s->width/60;
    if(tolerance<6)tolerance=6;
    if(off>=-tolerance&&off<=tolerance){
        s->moving_x=s->base_x;
        if (s->perfect_streak < INT_MAX) s->perfect_streak++;
    }else s->perfect_streak=0;
    int l = s->moving_x > s->base_x ? s->moving_x : s->base_x;
    int right = s->moving_x + s->width < s->base_x + s->width
                ? s->moving_x + s->width : s->base_x + s->width;
    if (right <= l) {
        /* A miss is a full-width shave, not yesterday's last-shave result. */
        s->last_shave = s->width;
        s->over = true;
        return false;
    }
    int new_width = right - l;
    s->last_shave = s->width - new_width;
    s->base_x = l;
    s->width = new_width;
    /* Golden Snap: three consecutive perfects recover 24 field units.
     * Clamp to both the original maximum and the right-hand boundary. */
    if(s->perfect_streak>=3 && s->perfect_streak%3==0) {
        int restored=24;
        if(restored>600-s->width)restored=600-s->width;
        if(restored>STACK_FIELD-s->base_x-s->width)
            restored=STACK_FIELD-s->base_x-s->width;
        if(restored>0)s->width+=restored;
        if (s->snap_rewards < INT_MAX) s->snap_rewards++; /* reward the achievement even at full width */
    }
    memmove(&s->layer_x[1], &s->layer_x[0], (STACK_HISTORY - 1) * sizeof s->layer_x[0]);
    memmove(&s->layer_width[1], &s->layer_width[0], (STACK_HISTORY - 1) * sizeof s->layer_width[0]);
    s->layer_x[0] = s->base_x;
    s->layer_width[0] = s->width;
    if (s->placed < INT_MAX) s->placed++;
    if (s->width <= 12) { s->over = true; return true; }
    s->rng ^= s->rng << 13;
    s->rng ^= s->rng >> 17;
    s->rng ^= s->rng << 5;
    s->direction = (s->placed & 1) ? -1 : 1;
    s->moving_x = s->direction > 0 ? 0 : STACK_FIELD - s->width;
    s->speed = 12 + (s->placed < 30 ? s->placed : 30) * 2 + (int)(s->rng % 3u);
    return true;
}
