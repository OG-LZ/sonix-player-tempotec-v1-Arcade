/* Pocket Arcade bonus-game cores. GPL-3.0-or-later.
 * Pure deterministic gameplay: no LVGL, audio, timers, or filesystem access.
 *
 * NOTE: State structures are copied by value for undo and test fixtures.
 * Keep field order and types stable unless all affected callers are reviewed.
 */
#ifndef ARCADE_EXTRA7_H
#define ARCADE_EXTRA7_H

#include <stdbool.h>
#include <stdint.h>

#define PA_MAP 9
#define PA_MELODY_MAX 32
#define PA_BOX_LEVELS 8

typedef enum {
    PA_UP = 0,
    PA_RIGHT = 1,
    PA_DOWN = 2,
    PA_LEFT = 3
} pa_dir_t;

typedef struct {
    char tiles[PA_MAP][PA_MAP];
    char undo_tiles[PA_MAP][PA_MAP];
    int x, y, moves, level, total_moves;
    int undo_x, undo_y, undo_moves;
    bool won, can_undo, deadlocked;
} pa_boxes_t;

void pa_boxes_reset(pa_boxes_t *g);
void pa_boxes_start_level(pa_boxes_t *g, int level);
void pa_boxes_restart_level(pa_boxes_t *g);
bool pa_boxes_move(pa_boxes_t *g, pa_dir_t d);
bool pa_boxes_undo(pa_boxes_t *g);
bool pa_boxes_next(pa_boxes_t *g);
bool pa_boxes_deadlocked(const pa_boxes_t *g);

typedef struct {
    int x, y, strokes, hole, course, hole_strokes, stars;
    bool won;
    uint8_t mirrored[3]; /* transpose each course: keeps (1,1) reachable */
} pa_golf_t;

void pa_golf_reset(pa_golf_t *g);
/* Up to 2^3 reproducible tournaments, with unchanged 3-hole rules. */
void pa_golf_reset_seed(pa_golf_t *g, uint32_t seed);
bool pa_golf_shot(pa_golf_t *g, pa_dir_t d);
char pa_golf_tile(const pa_golf_t *g, int x, int y);
bool pa_golf_preview(const pa_golf_t *g, pa_dir_t d, int *x, int *y);

typedef struct {
    int phase, window, level, lives, score, perfect_streak, best_streak;
    bool over;
} pa_orbit_t;

void pa_orbit_reset(pa_orbit_t *g);
void pa_orbit_tick(pa_orbit_t *g);
bool pa_orbit_jump(pa_orbit_t *g);
uint32_t pa_orbit_period_ms(const pa_orbit_t *g);

typedef struct {
    uint8_t sequence[PA_MELODY_MAX];
    int length, position, last_pad, completed, hearts, mistakes;
    uint32_t seed;
    bool over, won;
} pa_melody_t;

void pa_melody_reset(pa_melody_t *g, uint32_t seed);
bool pa_melody_press(pa_melody_t *g, int pad);

/* Deterministic visual demo; each tick alternates light and dark.
 * Playback and mixer state are never accessed. */
typedef struct {
    int index, shown;
    bool finished;
} pa_melody_demo_t;

void pa_melody_demo_start(pa_melody_demo_t *d, const pa_melody_t *g);
void pa_melody_demo_tick(pa_melody_demo_t *d, const pa_melody_t *g);

typedef enum {
    PA_BEAT_DRONE = 0,
    PA_BEAT_ARMORED = 1,
    PA_BEAT_HAZARD = 2,
    PA_BEAT_SHIELD = 3,
    PA_BEAT_BOSS = 4
} pa_beat_kind_t;

typedef struct {
    int player_lane, enemy_lane, enemy_row, enemy_kind, enemy_hp;
    int score, lives, ticks, combo, best_combo, shield, wave;
    int flash, flash_lane, flash_row, flash_points; /* positive: hit; negative: hurt */
    int burst_charge;      /* 0..100 earned from kills and skilful dodges */
    int burst_ticks;       /* visual feedback, never affects playback/audio */
    int burst_lane;
    int danger_dodges;     /* perfect hazard evades */
    int bursts_used;
    uint32_t seed;
    bool over;
    /* R22.4: bounded, simultaneous threats and deliberate laser fire.
     * Legacy enemy_* is retained as a read-only snapshot for diagnostics. */
    int foe_lane[3], foe_row[3], foe_kind[3], foe_hp[3];
    int shot_row[3];                 /* -1 when no laser is in flight */
    int gun_cooldown, fire_flash, impact_flash, damage_flash;
    int formations, shots_fired, shots_hit, foes_killed, perfect_waves;
    /* R22.5 -- playback-reactive rhythm layer, never a playback dependency. */
    bool music_live;
    int music_level, music_bass, pulse_glow, pulse_age;
    int sync_lane, sync_window, rhythm_hits, rhythm_combo;
    int shot_sync[3], last_formation_tick;
    int music_onsets;
    int wave_intro; /* brief banner on a new sector; visual only */
    int hurt_guard; /* i-frames after shield/life damage */
} pa_beat_t;

void pa_beat_reset(pa_beat_t *g, uint32_t seed);
void pa_beat_step(pa_beat_t *g);
/* Called once per render tick.  An onset is a new transient, not a BPM claim.
 * No music: internal fallback keeps the shooter fully playable. */
void pa_beat_music(pa_beat_t *g, bool live, unsigned level, unsigned bass, bool onset);
bool pa_beat_fire(pa_beat_t *g);
void pa_beat_steer(pa_beat_t *g, int amount);
/* Fully charged STAR BURST: screen-clearing overdrive; music-independent. */
bool pa_beat_burst(pa_beat_t *g);
uint32_t pa_beat_period_ms(const pa_beat_t *g);

typedef enum {
    PA_DUNGEON_NONE,
    PA_DUNGEON_HIT,
    PA_DUNGEON_KILL,
    PA_DUNGEON_HURT,
    PA_DUNGEON_HEAL,
    PA_DUNGEON_LOOT,
    PA_DUNGEON_STAIRS,
    PA_DUNGEON_BLOCK,
    PA_DUNGEON_SEALED,
    PA_DUNGEON_ALTAR,
    PA_DUNGEON_RELIC
} pa_dungeon_event_t;

typedef struct {
    char tiles[PA_MAP][PA_MAP];
    uint8_t enemy_hp[PA_MAP][PA_MAP];
    int x, y, hp, max_hp, gold, floor, kills, potions, weapon, focus, guard;
    int pending_upgrade, upgrade_choice, turns, floor_kills, last_damage, last_event;
    int pending_relic, relic_choice, relic; /* 1: Vampire Fang, 2: Ward Charm */
    uint32_t seed;
    bool over;
} pa_dungeon_t;

void pa_dungeon_reset(pa_dungeon_t *g, uint32_t seed);
bool pa_dungeon_move(pa_dungeon_t *g, pa_dir_t d);
bool pa_dungeon_guard(pa_dungeon_t *g);
bool pa_dungeon_use_potion(pa_dungeon_t *g);
bool pa_dungeon_upgrade(pa_dungeon_t *g, int choice);
bool pa_dungeon_take_relic(pa_dungeon_t *g, int choice);

uint32_t pa_rand(uint32_t *seed);

#endif /* ARCADE_EXTRA7_H */
