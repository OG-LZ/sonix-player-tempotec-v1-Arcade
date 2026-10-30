/* Pocket Dungeon: Relics of the Lantern. GPL-3.0-or-later.
 * Deterministic roguelite core. No LVGL, audio, filesystem or heap in gameplay.
 *
 * SAVE COMPATIBILITY: dg_game, dg_room, and dg_foe are serialized in the
 * existing versioned native-format checkpoint. Never change field layout
 * casually; any ABI change requires an explicit save migration and tests.
 */
#ifndef DUNGEON_DELUXE_H
#define DUNGEON_DELUXE_H

#include <stdbool.h>
#include <stdint.h>

#define DG_W 10
#define DG_H 8
#define DG_MAP_W 4
#define DG_MAP_H 3
#define DG_ROOMS 12
#define DG_FOES 6
#define DG_NONE 255u
#define DG_N 1u
#define DG_E 2u
#define DG_S 4u
#define DG_WEST 8u
#define DG_MAX_HP 24

typedef enum {
    DG_COMBAT,
    DG_ELITE,
    DG_TREASURE,
    DG_HEAL,
    DG_SHRINE,
    DG_MERCHANT,
    DG_PUZZLE,
    DG_BOSS,
    DG_ENTRANCE
} dg_room_kind;

typedef enum {
    DG_SKEL,
    DG_SLIME,
    DG_BAT,
    DG_CULTIST,
    DG_STONE,
    DG_GHOST,
    DG_BEETLE,
    DG_MIMIC
} dg_enemy_kind;

typedef enum {
    DG_SWORD,
    DG_SPEAR,
    DG_HAMMER,
    DG_DAGGER,
    DG_WAND,
    DG_LANTERN
} dg_weapon;

typedef enum {
    DG_EVT_NONE,
    DG_EVT_MOVE,
    DG_EVT_BLOCKED,
    DG_EVT_HIT,
    DG_EVT_KILL,
    DG_EVT_HURT,
    DG_EVT_DODGE,
    DG_EVT_TREASURE,
    DG_EVT_HEAL,
    DG_EVT_SHRINE,
    DG_EVT_BUY,
    DG_EVT_DOOR,
    DG_EVT_DESCEND,
    DG_EVT_GUARD,
    DG_EVT_BOSS,
    DG_EVT_DEAD,
    DG_EVT_WIN,
    DG_EVT_RELIC,
    DG_EVT_PUZZLE,
    DG_EVT_BOSS_PHASE,
    DG_EVT_WEAPON,
    DG_EVT_SKILL,
    DG_EVT_SLIME_SPLIT,
    DG_EVT_STOLEN,
    DG_EVT_FLAME_STEP,
    DG_EVT_FATE_VAULT,
    DG_EVT_HOUSE_ARCHIVE,
    DG_EVT_HOUSE_HAZARD,
    DG_EVT_HF_BELL, DG_EVT_HF_EVIDENCE, DG_EVT_HF_BREACH, DG_EVT_HF_CHOIR, DG_EVT_HF_SILENCE
} dg_event;

typedef enum {
    DG_RELIC_BLOOD = 1u,
    DG_RELIC_MIRROR = 2u,
    DG_RELIC_EMBER = 4u,
    DG_RELIC_GHOST = 8u,
    DG_RELIC_FROST = 16u,
    DG_RELIC_THORN = 32u,
    DG_RELIC_MOON = 64u,
    DG_RELIC_STONE = 128u
} dg_relic;

typedef struct {
    uint8_t x, y, kind, hp, max_hp, windup, tx, ty, active, stunned;
} dg_foe;

typedef struct {
    uint8_t gx, gy, kind, exits, visited, cleared, claimed, foe_count;
    uint8_t puzzle_start_x, puzzle_start_y, puzzle_x, puzzle_y;
    uint8_t puzzle_goal_x, puzzle_goal_y, puzzle_solved, puzzle_moves;
    uint8_t boss_phase, boss_variant; /* phase 1..3; Bone/Warden/Bishop */
    uint8_t tiles[DG_H][DG_W];
    dg_foe foes[DG_FOES];
} dg_room;

typedef struct {
    uint32_t seed, depth, floor_seed, turns, total_kills, total_gold, gold;
    uint8_t count, current, exit_room, x, y;
    uint8_t hp, max_hp, attack, potions, guard, focus, weapon;
    uint8_t relics, relic_slots, boss_count;
    uint8_t room_positions[DG_MAP_H][DG_MAP_W];
    uint8_t pending_relic, room_choice, alive, weapon_unlocked;
    uint8_t mana, ghost_charge, recovery;
    dg_event event;
    uint8_t event_x, event_y, event_amount;
    dg_room rooms[DG_ROOMS];
} dg_game;

uint32_t dg_hash(uint32_t x);
void dg_new(dg_game *g, uint32_t seed);
bool dg_next_floor(dg_game *g);
/* R9 finale: 1 Reset, 2 Unmake, 3 Tenth Witness. */
unsigned dg_final_state(const dg_game *g);
bool dg_final_choose(dg_game *g, unsigned ending);
bool dg_action(dg_game *g, int direction); /* 0:N, 1:E, 2:S, 3:W; attacks foes */
bool dg_guard(dg_game *g);
bool dg_potion(dg_game *g);
bool dg_lantern_nova(dg_game *g); /* 2 mana: area strike and stagger */
bool dg_flame_step(dg_game *g, int direction); /* 1 mana, 2 safe cells, 1 turn */
bool dg_is_fate_vault(const dg_game *g); /* selected treasure rooms, deterministic */
bool dg_choose_vault(dg_game *g, int choice); /* safe / risk / return */
const char *dg_chamber_title(const dg_game *g); /* seeded room identity */
bool dg_cycle_weapon(dg_game *g); /* non-combat action */
bool dg_reset_puzzle(dg_game *g); /* reset crate without resetting run */
bool dg_danger_at(const dg_game *g, int x, int y);

const char *dg_weapon_name(uint8_t weapon);
const char *dg_resonance_name(const dg_game *g); /* compatible derived relic synergy */
const char *dg_boss_name(const dg_game *g);
const char *dg_room_name(dg_room_kind kind);
const char *dg_biome_name(uint32_t depth);
const char *dg_relic_name(uint8_t relic);
const char *dg_relic_detail(uint8_t relic);

bool dg_interact(dg_game *g); /* open shrine or merchant choice */
uint8_t dg_merchant_weapon(const dg_game *g); /* DG_NONE when all owned */
bool dg_choose_merchant(dg_game *g,int index); /* 0 weapon/rest, 1 potion, 2 leave */
bool dg_choose_relic(dg_game *g, int index); /* 0/1: choose; 2: decline */
uint8_t dg_relic_offer(const dg_game *g, int index);
int dg_enemies_alive(const dg_room *r);
bool dg_map_reachable(const dg_room *r, int fromx, int fromy, int tox, int toy);
bool dg_validate(const dg_game *g);

/* Atomic/checksummed native-format checkpoint, with .bak fallback. Save only
 * under Sonix's device-local writable configuration directory. */
bool dg_save(const dg_game *g, const char *path);
bool dg_load(dg_game *g, const char *path);

#endif /* DUNGEON_DELUXE_H */
