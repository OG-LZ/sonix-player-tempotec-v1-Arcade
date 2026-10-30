#include "dungeon_testament.h"
#include <stddef.h>
static const dg_testimony texts[DG_TESTIMONY_COUNT] = {
 {0,"The Unwritten","The record has a space where your name should be."},
 {1,"The First Lantern","Mother promised she would leave the light burning."},
 {2,"The Second Cup","Melira set two cups. She did not ask who would drink the other."},
 {3,"A Name Unburied","I kept their names. Surely that must count for something."},
 {4,"The Prisoner's Light","The lantern does not burn. It forgets."},
 {5,"Borrowed Mercy","Every wound I healed was given to someone not yet born."},
 {6,"The Ledger","A life outside the count cannot appear in the sum."},
 {7,"The Gallery","There has always been an empty frame at the end."},
 {8,"The Ninth Age","Eight histories were discarded. The ninth has not yet ended."},
 {9,"The Warning","A prophecy and a grievance read differently from opposite ends."},
 {10,"The Hearthkeeper","I helped you because I feared you would succeed."},
 {11,"The Witness","We did not ask to be saved. We asked to be remembered."}
};
static const char *faces[DG_FACE_COUNT] = {
 "The Nameless", "The Oathbearer", "The Mourner", "The Jailer",
 "The Pilgrim", "The Penitent", "The Hearthkeeper", "The Unclaimed"
};
const dg_testimony *dg_testimony_at(unsigned id) {
 return id < DG_TESTIMONY_COUNT ? &texts[id] : NULL;
}
uint16_t dg_testimony_available(const dg_game *g) {
 uint16_t mask = 1u;
 if (!g) return 0;
 if (g->turns >= 1) mask |= 1u<<1;
 if (g->depth >= 2) mask |= (1u<<2)|(1u<<7);
 if (g->boss_count >= 1) mask |= (1u<<3)|(1u<<8);
 if (g->boss_count >= 2) mask |= (1u<<4)|(1u<<9);
 if (g->boss_count >= 3) mask |= (1u<<5)|(1u<<6)|(1u<<11);
 if (g->depth >= 5) mask |= 1u<<10;
 return mask;
}
uint8_t dg_borrowed_face(const dg_game *g) {
 /* Identity is a stable presentation hint, not a random stat loadout. */
 if (!g) return 0;
 return (uint8_t)(dg_hash(g->seed ^ (g->depth * 0x9e3779b9u)) % DG_FACE_COUNT);
}
const char *dg_borrowed_face_name(uint8_t id) {
 return id < DG_FACE_COUNT ? faces[id] : "Unknown";
}
const char *dg_candlewake_state(const dg_game *g) {
 if (!g) return "Unrecorded";
 if (g->boss_count >= 3) return "Recognition";
 if (g->boss_count >= 1 || g->depth >= 3) return "Contradiction";
 return "Sanctuary";
}
const char *dg_testament_boss_epitaph(const dg_game *g) {
 if (!g) return "";
 switch (g->depth % 3u) {
 case 1: return "Aldren kept seven names beneath his crown.";
 case 2: return "Sereth guarded the innocent from their own memories.";
 default: return "Vaelor called every borrowed wound a miracle.";
 }
}
