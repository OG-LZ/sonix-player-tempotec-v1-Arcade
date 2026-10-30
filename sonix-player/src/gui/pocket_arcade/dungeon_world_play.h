/* Black Testament R2: deterministic World Bible gameplay bindings. GPL-3.0-or-later. */
#ifndef DUNGEON_WORLD_PLAY_H
#define DUNGEON_WORLD_PLAY_H
#include "dungeon_deluxe.h"
#include "dungeon_chronicle.h"
/* Pure derived presentation: no RNG consumption, heap, or checkpoint changes. */
unsigned dg_world_house(const dg_game *g);
const char *dg_world_house_name(unsigned house);
const char *dg_world_room_legend(const dg_game *g);
const char *dg_world_enemy_variant(const dg_game *g, unsigned enemy_kind);
const char *dg_world_relic_provenance(unsigned relic_bit);
const char *dg_world_npc_knowledge(const dg_game *g,const dg_chronicle *c);
/* Deterministic House modifiers applied to enemies when floors are first built. */
/* Living Houses: deterministic architecture and historical combat patterns. */
void dg_world_furnish(dg_room *room, unsigned house);
unsigned dg_world_hazard(dg_game *g, unsigned house); /* effect code; 0 = none */
unsigned dg_world_archive_testimony(unsigned house);
const char *dg_world_hazard_name(unsigned house);
void dg_world_mark_foe(dg_foe *foe, unsigned house);
#endif
