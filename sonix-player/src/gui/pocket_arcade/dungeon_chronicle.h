#ifndef DUNGEON_CHRONICLE_H
#define DUNGEON_CHRONICLE_H
#include "dungeon_testament.h"
#include <stdbool.h>
#include <stdint.h>
#define DG_CHRONICLE_VERSION 1u
#define DG_CHRONICLE_MAGIC 0x31435442u /* BTC1 */
typedef struct {
 uint16_t discovered;
 uint8_t face_decisions; /* 2 bits per decision: 0 untouched, 1 carried, 2 refused */
 uint8_t burden_on_player;
 uint8_t burden_on_forge;
 uint8_t candlewake_visits;
 uint8_t final_ending; /* R9: persistent epilogue, byte 13 */
 uint8_t last_bell_evidence; /* 0 unknown, 2 silenced, 3 Choir; persisted at byte 12 */
} dg_chronicle;
bool dg_chronicle_record_ending(dg_chronicle *c, unsigned ending);
void dg_chronicle_init(dg_chronicle *c);
bool dg_chronicle_record_last_bell(dg_chronicle *c, unsigned outcome);
/* Returns newly unlocked bits. The caller explicitly invokes this at gameplay milestones. */
uint16_t dg_chronicle_observe(dg_chronicle *c, const dg_game *g);
bool dg_chronicle_knows(const dg_chronicle *c, unsigned id);
/* Only first two faces have implemented obligations; refusal transfers debt to forge keeper. */
bool dg_chronicle_decide(dg_chronicle *c, unsigned obligation, bool accept);
const char *dg_chronicle_candlewake_dialogue(const dg_chronicle *c, const dg_game *g);
const char *dg_chronicle_forge_dialogue(const dg_chronicle *c);
/* Independent versioned sidecar, not a modification of dg_game checkpoint. */
bool dg_chronicle_save(const dg_chronicle *c, const char *path);
bool dg_chronicle_load(dg_chronicle *c, const char *path);
#endif
