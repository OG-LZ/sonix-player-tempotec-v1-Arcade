/* Black Testament R5: deterministic Heavensfall campaign, no save ABI changes. */
#ifndef DUNGEON_HEAVENSFALL_H
#define DUNGEON_HEAVENSFALL_H
#include "dungeon_deluxe.h"
/* Three contiguous depth chapters; 0 outside the historical campaign. */
unsigned dg_hf_chapter(const dg_game *g);
const char *dg_hf_title(unsigned chapter);
const char *dg_hf_evidence(unsigned chapter, unsigned room_index);
/* Applies an enemy's war-era tactical training at initial generation. */
void dg_hf_train(dg_foe *foe, unsigned chapter);
/* Adds a visible, walkable scar, never a blocking wall. */
void dg_hf_scar(dg_room *room, unsigned chapter);
/* R6: objective state lives in existing room tile bytes, not checkpoint ABI. */
void dg_hf_install(dg_game *g, dg_room *r, unsigned room_index);
bool dg_hf_can_descend(const dg_game *g);
unsigned dg_hf_remaining(const dg_game *g);
const char *dg_hf_objective(unsigned chapter);
/* R7: a final bell on the Ashen Breach exit. Choice is encoded in the
 * existing tile, surviving ordinary checkpoints without ABI changes. */
unsigned dg_hf_last_bell_state(const dg_game *g); /* 0 absent, 1 undecided, 2 silent, 3 speaking */
bool dg_hf_last_bell_choose(dg_game *g, bool let_speak);
const char *dg_hf_last_bell_testimony(unsigned state);
#endif
