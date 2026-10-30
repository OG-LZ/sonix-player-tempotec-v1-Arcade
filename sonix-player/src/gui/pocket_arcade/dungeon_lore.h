/* The Black Testament: stateless, deterministic Pocket Dungeon narrative.
 * GPL-3.0-or-later. No heap, LVGL, filesystem or gameplay RNG consumption.
 * Lore progress is derived from existing dg_game data, NOT saved separately. */
#ifndef DUNGEON_LORE_H
#define DUNGEON_LORE_H

#include "dungeon_deluxe.h"
#include <stdbool.h>

typedef struct {
    const char *title;
    const char *text; /* Four short lines for the 240x320 dialogue cabinet. */
} dg_lore_fragment;

/* Index zero is the current room's echo. Other pages unlock by floor and relic.
 * Enumeration is stable for a given state; callers must clamp on change. */
unsigned dg_lore_count(const dg_game *game);
bool dg_lore_at(const dg_game *game, unsigned index, dg_lore_fragment *out);
const char *dg_lore_room_whisper(const dg_game *game);
const char *dg_lore_boss_omen(const dg_game *game);

#endif /* DUNGEON_LORE_H */
