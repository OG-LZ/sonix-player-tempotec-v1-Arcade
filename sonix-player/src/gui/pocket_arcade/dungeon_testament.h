/* Pocket Dungeon: Black Testament narrative layer. GPL-3.0-or-later.
 * Pure deterministic presentation helpers; no heap, RNG, or save ABI changes.
 */
#ifndef DUNGEON_TESTAMENT_H
#define DUNGEON_TESTAMENT_H
#include "dungeon_deluxe.h"
#include <stdint.h>
#define DG_TESTIMONY_COUNT 12u
#define DG_FACE_COUNT 8u
typedef struct { uint8_t id; const char *title; const char *text; } dg_testimony;
/* IDs are stable and suitable for future versioned sidecar bitsets. */
const dg_testimony *dg_testimony_at(unsigned id);
uint16_t dg_testimony_available(const dg_game *g);
uint8_t dg_borrowed_face(const dg_game *g);
const char *dg_borrowed_face_name(uint8_t id);
const char *dg_candlewake_state(const dg_game *g);
const char *dg_testament_boss_epitaph(const dg_game *g);
#endif
