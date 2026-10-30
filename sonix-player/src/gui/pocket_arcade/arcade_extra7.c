#include "arcade_extra7.h"

#include <limits.h>
#include <string.h>

uint32_t pa_rand(uint32_t *state) {
    if (!*state) {
        *state = 0x6d2b79f5u;
    }
    uint32_t value = *state;
    value ^= value << 13;
    value ^= value >> 17;
    value ^= value << 5;
    return *state = value;
}

static void move_vec(pa_dir_t direction, int *dx, int *dy) {
    static const int xs[4] = {0, 1, 0, -1};
    static const int ys[4] = {-1, 0, 1, 0};
    *dx = xs[direction & 3];
    *dy = ys[direction & 3];
}

static bool inside(int x, int y) {
    return x >= 0 && x < PA_MAP && y >= 0 && y < PA_MAP;
}

/* Eight designed, offline Sokoban stages. Stage zero is a tiny tutorial;
 * the remaining stages were independently BFS-solved at 7, 10, 13, 16,
 * 17, 22 and 30 moves. All boards are original and immutable. */
static const char *const box_levels[PA_BOX_LEVELS][PA_MAP] = {
    {"#########", "#########", "##     ##", "##  .  ##", "##  $  ##",
     "##  @$.##", "##     ##", "#########", "#########"},
    {"#########", "#       #", "# @ .   #", "#    #  #", "# # $   #",
     "#  $  # #", "#  .    #", "#       #", "#########"},
    {"#########", "#       #", "#  # .  #", "#    $  #", "# .     #",
     "# $     #", "#  #@   #", "#       #", "#########"},
    {"#########", "#       #", "#       #", "#     . #", "# .   $ #",
     "# @###  #", "# $     #", "#       #", "#########"},
    {"#########", "#       #", "# #  .$ #", "#  .    #", "# @     #",
     "#  #  # #", "#   $   #", "#       #", "#########"},
    {"#########", "#       #", "#     . #", "# #$ #  #", "#    $  #",
     "#       #", "#  .  @ #", "#       #", "#########"},
    {"#########", "#       #", "# .##   #", "#       #", "# $ #   #",
     "#  @ #. #", "# $#    #", "#       #", "#########"},
    {"#########", "#       #", "#   #$  #", "# #@    #", "#  .#   #",
     "#   $   #", "# #. #  #", "#       #", "#########"}
};

static bool is_box(char tile) {
    return tile == '$' || tile == '*';
}

static bool is_free(char tile) {
    return tile == ' ' || tile == '.';
}

static bool boxes_won(const pa_boxes_t *game) {
    for (int y = 0; y < PA_MAP; y++) {
        for (int x = 0; x < PA_MAP; x++) {
            if (game->tiles[y][x] == '.') {
                return false;
            }
        }
    }
    return true;
}

bool pa_boxes_deadlocked(const pa_boxes_t *game) {
    if (!game) {
        return false;
    }
    for (int y = 1; y < PA_MAP - 1; y++) {
        for (int x = 1; x < PA_MAP - 1; x++) {
            if (game->tiles[y][x] != '$') {
                continue; /* Crates already on goals are safe. */
            }
            bool vertical = game->tiles[y - 1][x] == '#' || game->tiles[y + 1][x] == '#';
            bool horizontal = game->tiles[y][x - 1] == '#' || game->tiles[y][x + 1] == '#';
            if (vertical && horizontal) {
                return true;
            }
        }
    }
    return false;
}

static void boxes_load(pa_boxes_t *game, int level) {
    game->level = level;
    game->moves = 0;
    game->won = false;
    game->can_undo = false;
    for (int y = 0; y < PA_MAP; y++) {
        for (int x = 0; x < PA_MAP; x++) {
            char tile = box_levels[level][y][x];
            if (tile == '@') {
                game->x = x;
                game->y = y;
                tile = ' ';
            } else if (tile == '+') {
                game->x = x;
                game->y = y;
                tile = '.';
            }
            game->tiles[y][x] = tile;
        }
    }
}

void pa_boxes_reset(pa_boxes_t *game) {
    memset(game, 0, sizeof(*game));
    boxes_load(game, 0);
}

void pa_boxes_start_level(pa_boxes_t *game, int level) {
    if (level < 0) {
        level = 0;
    }
    if (level >= PA_BOX_LEVELS) {
        level = PA_BOX_LEVELS - 1;
    }
    memset(game, 0, sizeof(*game));
    boxes_load(game, level);
}

void pa_boxes_restart_level(pa_boxes_t *game) {
    int level = game->level;
    int total = game->total_moves;
    boxes_load(game, level);
    game->total_moves = total;
}

bool pa_boxes_move(pa_boxes_t *game, pa_dir_t direction) {
    /* Avoid silently turning malformed directional values into UP/RIGHT. */
    if ((int)direction < PA_UP || (int)direction > PA_LEFT) return false;
    if (game->won) {
        return false;
    }
    int dx, dy;
    move_vec(direction, &dx, &dy);
    int nx = game->x + dx;
    int ny = game->y + dy;
    if (!inside(nx, ny)) {
        return false;
    }
    char tile = game->tiles[ny][nx];
    if (tile == '#') {
        return false;
    }
    if (is_box(tile)) {
        int tx = nx + dx;
        int ty = ny + dy;
        if (!inside(tx, ty) || !is_free(game->tiles[ty][tx])) {
            return false;
        }
    }

    /* Record only successful moves; an incorrect push remains undoable. */
    memcpy(game->undo_tiles, game->tiles, sizeof(game->tiles));
    game->undo_x = game->x;
    game->undo_y = game->y;
    game->undo_moves = game->moves;
    game->can_undo = true;
    if (is_box(tile)) {
        int tx = nx + dx;
        int ty = ny + dy;
        game->tiles[ty][tx] = game->tiles[ty][tx] == '.' ? '*' : '$';
        game->tiles[ny][nx] = tile == '*' ? '.' : ' ';
    }
    game->x = nx;
    game->y = ny;
    if (game->moves < INT_MAX) {
        game->moves++;
    }
    if (game->total_moves < INT_MAX) {
        game->total_moves++;
    }
    game->won = boxes_won(game);
    game->deadlocked = pa_boxes_deadlocked(game);
    return true;
}

bool pa_boxes_undo(pa_boxes_t *game) {
    if (!game->can_undo) {
        return false;
    }
    memcpy(game->tiles, game->undo_tiles, sizeof(game->tiles));
    game->x = game->undo_x;
    game->y = game->undo_y;
    game->moves = game->undo_moves;
    if (game->total_moves > 0) {
        game->total_moves--;
    }
    game->won = false;
    game->can_undo = false;
    game->deadlocked = pa_boxes_deadlocked(game);
    return true;
}

bool pa_boxes_next(pa_boxes_t *game) {
    if (!game->won || game->level >= PA_BOX_LEVELS - 1) {
        return false;
    }
    boxes_load(game, game->level + 1);
    return true;
}

/* Tiny golf: 3 deterministic compact holes. Shots travel until an obstacle,
 * boundary or cup; no floating point, no frame-rate-dependent physics. */
static const char *golf_courses[3][PA_MAP]={
    {"#########","#     # #","#       #","#  ##   #","#       #","#   ##  #","#       #","#      O#","#########"},
    {"#########","#       #","# ## ## #","#       #","#   #   #","#       #","# ##    #","#O      #","#########"},
    {"#########","#       #","#  ##   #","#       #","# #   # #","#       #","#     # #","#      O#","#########"}
};
char pa_golf_tile(const pa_golf_t *g,int x,int y){
    if (!g || !inside(x,y) || g->course<0 || g->course>2) return '#';
    int course=g->course;
    return g->mirrored[course] ? golf_courses[course][x][y] : golf_courses[course][y][x];
}
void pa_golf_reset(pa_golf_t *g){ memset(g,0,sizeof(*g));g->x=1;g->y=1;g->course=0;}
void pa_golf_reset_seed(pa_golf_t *g,uint32_t seed){
    pa_golf_reset(g);
    /* Independently reflect each designed room along the main diagonal.
     * These transformations preserve the starting square and path graph. */
    uint32_t r=seed?seed:0xace120u;
    for(int i=0;i<3;i++)g->mirrored[i]=(uint8_t)((pa_rand(&r)>>7u)&1u);
}
bool pa_golf_preview(const pa_golf_t *g,pa_dir_t d,int *ox,int *oy){
    if(!g||g->won||(int)d<PA_UP||(int)d>PA_LEFT)return false;
    int dx,dy;move_vec(d,&dx,&dy);int x=g->x,y=g->y;
    for(int step=0;step<8;step++){
        int nx=x+dx,ny=y+dy;
        if(!inside(nx,ny)||pa_golf_tile(g,nx,ny)=='#')break;
        x=nx;y=ny;if(pa_golf_tile(g,x,y)=='O')break;
    }
    if(ox)*ox=x;
    if(oy)*oy=y;
    return x!=g->x||y!=g->y;
}
bool pa_golf_shot(pa_golf_t *g,pa_dir_t d){
    if(g->won||(int)d<PA_UP||(int)d>PA_LEFT)return false;
    int dx,dy;move_vec(d,&dx,&dy);bool moved=false;
    for(int step=0;step<8;step++){
        int nx=g->x+dx,ny=g->y+dy;
        if(!inside(nx,ny)||pa_golf_tile(g,nx,ny)=='#')break;
        g->x=nx;g->y=ny;moved=true;
        if(pa_golf_tile(g,nx,ny)=='O')break;
    }
    if(moved){
        if(g->strokes<INT_MAX)g->strokes++;
        if(g->hole_strokes<INT_MAX)g->hole_strokes++;
        if(pa_golf_tile(g,g->x,g->y)=='O'){
            static const int par[]={3,4,5};
            int over=g->hole_strokes-par[g->course];
            int earned=over<=0?3:over<=2?2:1;
            g->stars=earned>INT_MAX-g->stars?INT_MAX:g->stars+earned;
            g->hole_strokes=0;
            if(g->course==2){g->won=true;}
            else {g->course++;g->x=1;g->y=1;}
        }
    }
    return moved;
}

void pa_orbit_reset(pa_orbit_t *g){memset(g,0,sizeof(*g));g->lives=3;g->window=4;}
void pa_orbit_tick(pa_orbit_t *g){
    if(!g||g->over)return;
    /* 16-point ring: wrap without risking an overflowing signed addition. */
    g->phase=((unsigned)g->phase+1u)&15u;
}
uint32_t pa_orbit_period_ms(const pa_orbit_t *g){
    if(!g)return 120u;
    return g->level>=36?90u:g->level>=20?100u:g->level>=8?110u:120u;
}
bool pa_orbit_jump(pa_orbit_t *g){
    if(!g||g->over)return false;
    if(g->lives<=0){g->over=true;return false;}
    /* Win window moves after each successful orbit to avoid rote tapping. */
    int diff=(g->phase-g->window+16)%16;
    bool hit=diff<=1||diff>=15;
    if(hit){
        if(g->level<INT_MAX)g->level++;
        if(diff==0){if(g->perfect_streak<INT_MAX)g->perfect_streak++;if(g->perfect_streak>g->best_streak)
            g->best_streak=g->perfect_streak;}
        else g->perfect_streak=0;
        int multiplier=g->perfect_streak<5?g->perfect_streak:5;
        /* Saturate the run summary instead of overflowing after extreme play. */
        int gain = g->level > (INT_MAX-50)/2 ? INT_MAX :
                   10+g->level*2+multiplier*8;
        g->score = gain > INT_MAX-g->score ? INT_MAX : g->score+gain;
        g->window=(4+(g->level%16)*5)%16;
    } else {
        g->perfect_streak=0;
        if(--g->lives<=0)g->over=true;
    }
    return hit;
}

/* Memory game is always silent: a playback-safe color/note sequence, independent
 * of ALSA, Qobuz, and game sound effects. The UI owns the demo clock. */
void pa_melody_reset(pa_melody_t *g,uint32_t seed){
    memset(g,0,sizeof(*g));g->seed=seed?seed:13u;g->last_pad=-1;
    /* First four notes teach each pad once, in a seed-specific order. Later
     * rounds avoid three identical notes consecutively without easy patterns. */
    for(int i=0;i<4;i++)g->sequence[i]=(uint8_t)i;
    for(int i=3;i>0;i--){
        int j=(int)(pa_rand(&g->seed)%(unsigned)(i+1));
        uint8_t tmp=g->sequence[i];g->sequence[i]=g->sequence[j];g->sequence[j]=tmp;
    }
    for(int i=4;i<PA_MELODY_MAX;i++){
        uint8_t pad=(uint8_t)(pa_rand(&g->seed)%4u);
        if(pad==g->sequence[i-1]&&pad==g->sequence[i-2])pad=(uint8_t)((pad+1u)%4u);
        g->sequence[i]=pad;
    }
    g->length=1;g->hearts=3;
}
bool pa_melody_press(pa_melody_t *g,int pad){
    if(!g||g->over||g->won||pad<0||pad>3 ||
       g->length<1||g->length>PA_MELODY_MAX ||
       g->position<0||g->position>=g->length)return false;
    g->last_pad=pad;
    if(pad!=g->sequence[g->position]){
        if(g->mistakes<INT_MAX)g->mistakes++;
        g->position=0;
        if(--g->hearts<=0)g->over=true;
        return false;
    }
    g->position++;
    if(g->position==g->length){
        g->completed++;
        if(g->length==PA_MELODY_MAX)g->won=true;
        else {g->length++;g->position=0;}
    }
    return true;
}

void pa_melody_demo_start(pa_melody_demo_t *d,const pa_melody_t *g){
    d->shown=0;d->index=-1;d->finished=true;
    if(!g||g->length<1||g->length>PA_MELODY_MAX)return;
    d->shown=1;d->index=g->sequence[0];d->finished=false;
}
void pa_melody_demo_tick(pa_melody_demo_t *d,const pa_melody_t *g){
    if(d->finished||!g)return;
    if(d->index>=0){d->index=-1;return;}
    if(d->shown<g->length){d->index=g->sequence[d->shown++];return;}
    d->shown=0;d->finished=true;
}

/* Beat Raiders R22.6: music-driven three-lane arcade shooter.
 * Every state update is bounded O(3) and deterministic. The game never calls
 * audio, network, threads or decoders; music can play independently. */
static void beat_add_score(pa_beat_t *g,int bonus){
    if(bonus>0)g->score=bonus>INT_MAX-g->score?INT_MAX:g->score+bonus;
}
static void beat_add_charge(pa_beat_t *g,int bonus){
    if(bonus>0)g->burst_charge=bonus>100-g->burst_charge?100:g->burst_charge+bonus;
}
static void beat_sync_legacy(pa_beat_t *g){
    int at=-1;
    for(int i=0;i<3;i++)if(g->foe_hp[i]>0 && (at<0||g->foe_row[i]>g->foe_row[at]))at=i;
    if(at<0){g->enemy_lane=1;g->enemy_row=0;g->enemy_kind=PA_BEAT_DRONE;g->enemy_hp=0;}
    else{g->enemy_lane=g->foe_lane[at];g->enemy_row=g->foe_row[at];
         g->enemy_kind=g->foe_kind[at];g->enemy_hp=g->foe_hp[at];}
}
static void beat_impact(pa_beat_t *g,int lane,int row,int points){
    g->flash=3;g->flash_lane=lane;g->flash_row=row;g->flash_points=points;
    g->impact_flash=2;
}
static void beat_kill_slot(pa_beat_t *g,int slot){
    int kind=g->foe_kind[slot],row=g->foe_row[slot],lane=g->foe_lane[slot];
    if(g->combo<INT_MAX)g->combo++;
    if(g->combo>g->best_combo)g->best_combo=g->combo;
    int mult=1+(g->combo>12?12:g->combo)/4;
    int base=kind==PA_BEAT_BOSS?70:kind==PA_BEAT_ARMORED?24:kind==PA_BEAT_HAZARD?18:12;
    beat_add_score(g,base*mult);
    beat_add_charge(g,kind==PA_BEAT_BOSS?40:kind==PA_BEAT_ARMORED?18:12);
    if(g->foes_killed<INT_MAX)g->foes_killed++;
    g->foe_hp[slot]=0;
    beat_impact(g,lane,row,base);
}
static void beat_spawn_formation(pa_beat_t *g){
    int available[3],cnt=0;
    for(int i=0;i<3;i++)if(!g->foe_hp[i])available[cnt++]=i;
    if(cnt==0)return;
    /* A sector is FOUR formations, not one spawn. Wave labels now reflect
     * actual progression, so an early death no longer reads 'WAVE 26'. */
    if(g->formations>0 && g->formations%4==0 && g->wave<INT_MAX){
        g->wave++;
        g->wave_intro=8;
    }
    if(g->formations<INT_MAX)g->formations++;
    int n=g->wave<3?1:2; /* never make a three-lane unavoidable wall */
    /* One flagship boss formation at the START of every fifth sector. */
    bool boss=(g->wave%5)==0 && (g->formations%4)==1;
    if(boss)n=1;
    if(n>cnt)n=cnt;
    int shift=(int)(pa_rand(&g->seed)%3u);
    for(int j=0;j<n;j++){
        int slot=available[j],lane=(shift+j)%3;
        unsigned roll=pa_rand(&g->seed)%12u;
        int kind=boss?PA_BEAT_BOSS:roll<6?PA_BEAT_DRONE:
                 roll<9?PA_BEAT_ARMORED:roll<11?PA_BEAT_HAZARD:PA_BEAT_SHIELD;
        if(g->wave<3 && kind==PA_BEAT_HAZARD)kind=PA_BEAT_DRONE;
        if(g->music_live && !boss && g->wave>=3){
            /* Drum-heavy phrases prefer armoured foes; quieter ones prefer
             * drones and collectible shields, never three lethal lanes. */
            if(g->music_bass>=65 && kind==PA_BEAT_DRONE)kind=PA_BEAT_ARMORED;
            else if(g->music_level<=18 && kind==PA_BEAT_ARMORED)kind=PA_BEAT_DRONE;
        }
        g->foe_lane[slot]=lane;g->foe_row[slot]=0;g->foe_kind[slot]=kind;
        g->foe_hp[slot]=kind==PA_BEAT_BOSS?6:kind==PA_BEAT_ARMORED?2:1;
    }
    g->last_formation_tick=g->ticks;
    beat_sync_legacy(g);
}
void pa_beat_reset(pa_beat_t *g,uint32_t seed){
    if(!g)return;
    memset(g,0,sizeof(*g));g->seed=seed?seed:101u;g->lives=3;
    g->player_lane=1;g->flash_lane=-1;g->flash_row=-1;g->burst_lane=-1;
    g->sync_lane=1;g->pulse_age=100;g->wave=1;g->wave_intro=12;
    for(int i=0;i<3;i++)g->shot_row[i]=-1;
    beat_spawn_formation(g);
}
uint32_t pa_beat_period_ms(const pa_beat_t *g){
    /* 100ms motion ticks, enemy descent at ~300ms; target V1 redraw budget. */
    if(!g)return 100u;
    return g->wave<12?110u:g->wave<24?100u:90u;
}
void pa_beat_music(pa_beat_t *g, bool live, unsigned level, unsigned bass, bool onset){
    if(!g || g->over)return;
    g->music_live=live;
    if(!live){g->music_level=0;g->music_bass=0;g->sync_window=0;return;}
    g->music_level=level>100u?100:(int)level;
    g->music_bass=bass>100u?100:(int)bass;
    if(onset){
        if(g->music_onsets<INT_MAX)g->music_onsets++;
        g->pulse_glow=4;
        g->pulse_age=0;
        g->sync_window=3;
        /* Cycle the rhythm prompt across the three shooter lanes.  No
         * precision-timing penalty: a correct hit is only a bonus. */
        g->sync_lane=(g->sync_lane+1)%3;
        /* Song transients change formation cadence and enemy roles, but
         * minimum spacing is preserved to prevent impossible walls. */
        if(g->ticks-g->last_formation_tick>=7)
            beat_spawn_formation(g);
    }
}
void pa_beat_steer(pa_beat_t *g,int amount){
    if(!g||amount<-1||amount>1||g->over)return;
    int next=g->player_lane+amount;
    if(next>=0&&next<3)g->player_lane=next;
}
bool pa_beat_fire(pa_beat_t *g){
    if(!g||g->over||g->gun_cooldown>0)return false;
    if(g->shot_row[g->player_lane]>=0)return false;
    g->shot_row[g->player_lane]=4;
    /* Reward meeting the highlighted lane shortly after a real transient.
     * No penalty for playing ambient/classical music or having no audio. */
    g->shot_sync[g->player_lane]=(g->music_live && g->sync_window>0 &&
                                   g->player_lane==g->sync_lane)?1:0;
    if(g->shot_sync[g->player_lane]){
        beat_add_score(g,6);beat_add_charge(g,4);
        if(g->rhythm_hits<INT_MAX)g->rhythm_hits++;
        if(g->rhythm_combo<999)g->rhythm_combo++;
        g->pulse_glow=5;
        g->sync_window=0; /* once per transient */
    }
    g->gun_cooldown=2;g->fire_flash=2;
    if(g->shots_fired<INT_MAX)g->shots_fired++;
    return true;
}
static void beat_laser_hit(pa_beat_t *g,int lane,int row){
    for(int i=0;i<3;i++){
        if(g->foe_hp[i]>0&&g->foe_lane[i]==lane&&g->foe_row[i]==row){
            /* A shield pickup absorbs a normal shot. Don't accidentally kill it. */
            if(g->foe_kind[i]==PA_BEAT_SHIELD)return;
            if(g->shots_hit<INT_MAX)g->shots_hit++;
            g->foe_hp[i]--;
            beat_impact(g,lane,row,g->foe_hp[i]?0:12);
            if(!g->foe_hp[i])beat_kill_slot(g,i);
            return;
        }
    }
}
bool pa_beat_burst(pa_beat_t *g){
    if(!g||g->over||g->burst_charge<100)return false;
    g->burst_charge=0;
    if(g->bursts_used<INT_MAX)g->bursts_used++;
    g->burst_ticks=5;g->burst_lane=g->player_lane;
    /* A genuine panic button: hit every occupied lane and clear incoming hazards. */
    for(int i=0;i<3;i++){
        if(g->foe_hp[i]<=0||g->foe_kind[i]==PA_BEAT_SHIELD)continue;
        g->foe_hp[i]-=3;
        if(g->foe_hp[i]<=0)beat_kill_slot(g,i);
        else beat_impact(g,g->foe_lane[i],g->foe_row[i],0);
    }
    beat_sync_legacy(g);
    return true;
}
void pa_beat_step(pa_beat_t *g){
    if(!g||g->over)return;
    if(g->ticks<INT_MAX)g->ticks++;
    if(g->flash>0)g->flash--;
    if(g->burst_ticks>0)g->burst_ticks--;
    if(g->fire_flash>0)g->fire_flash--;
    if(g->impact_flash>0)g->impact_flash--;
    if(g->damage_flash>0)g->damage_flash--;
    if(g->gun_cooldown>0)g->gun_cooldown--;
    if(g->hurt_guard>0)g->hurt_guard--;
    if(g->wave_intro>0)g->wave_intro--;
    if(g->pulse_glow>0)g->pulse_glow--;
    if(g->sync_window>0)g->sync_window--;
    if(g->pulse_age<100)g->pulse_age++;
    /* Projectile steps resolve before formation movement. Check each crossed
     * tile to prevent tunneling; only three shots/enemies can exist. */
    for(int lane=0;lane<3;lane++){
        if(g->shot_row[lane]<0)continue;
        bool hit=false;
        for(int step=0;step<2 && !hit;step++){
            for(int i=0;i<3;i++){
                if(g->foe_hp[i] && g->foe_lane[i]==lane &&
                   g->foe_row[i]==g->shot_row[lane] &&
                   g->foe_kind[i]!=PA_BEAT_SHIELD){
                    beat_laser_hit(g,lane,g->shot_row[lane]);hit=true;break;
                }
            }
            if(!hit)g->shot_row[lane]--;
            if(g->shot_row[lane]<0)break;
        }
        if(hit){g->shot_row[lane]=-1;g->shot_sync[lane]=0;}
        else if(g->shot_row[lane]<0)g->shot_sync[lane]=0;
    }
    /* Four ticks per row gives ~2 seconds from spawn to impact,
     * enough to acquire targets on a physical 240px touch panel. */
    if(g->ticks%4==0){
        for(int i=0;i<3;i++){
            if(g->foe_hp[i]<=0)continue;
            int lane=g->foe_lane[i],kind=g->foe_kind[i];
            /* Centipede-style armored sweep, telegraphed with 2+ rows
             * remaining. It never switches lane on the collision row. */
            if(g->foe_kind[i]==PA_BEAT_ARMORED && g->wave>=4 &&
               g->foe_row[i]==1)
                g->foe_lane[i]=(g->foe_lane[i]+((g->wave&1)?1:2))%3;
            lane=g->foe_lane[i];
            if(++g->foe_row[i]<5)continue;
            if(kind==PA_BEAT_SHIELD&&lane==g->player_lane){
                if(g->shield<2)g->shield++;
                beat_add_charge(g,25);beat_add_score(g,10);
                beat_impact(g,lane,5,10);
            }else if(lane==g->player_lane){
                /* Collision damage has short invulnerability. Several
                 * objects arriving together cannot drain the whole run. */
                if(g->hurt_guard==0){
                    if(g->shield>0)g->shield--;
                    else if(g->lives>0)g->lives--;
                    g->combo=0;g->damage_flash=5;g->hurt_guard=8;
                    beat_impact(g,lane,5,-1);
                    if(g->lives==0)g->over=true;
                }
            }else if(kind==PA_BEAT_HAZARD){
                beat_add_score(g,8);beat_add_charge(g,9);
                if(g->danger_dodges<INT_MAX)g->danger_dodges++;
            }else if(kind==PA_BEAT_SHIELD){/* missed shield pickup */}
            else g->combo=0;  /* missed enemy breaks kill combo */
            g->foe_hp[i]=0;
        }
    }
    /* Stagger formations so new waves cannot materialize every tick. */
    if(!g->over && (g->music_live ?
        (g->ticks-g->last_formation_tick>=16) : (g->ticks%10==0)))
        beat_spawn_formation(g);
    beat_sync_legacy(g);
}

/* Tiny dungeon: designed for one-handed, turn-based play. Four enemy classes
 * (rat, guard, brute, wisp) differ in hit points, attack, and shielding.
 * Choices: spend scarce focus on guard, consume a potion, or save them;
 * choose a permanent Heart or Blade upgrade at each floor transition. */
static int dungeon_enemy(char c){return c=='r'||c=='G'||c=='B'||c=='W';}
static int dungeon_max(int a,int b){return a>b?a:b;}
static int dungeon_min(int a,int b){return a<b?a:b;}
static int dungeon_enemy_hp(char c){
    return c=='r'?1:c=='G'?2:c=='B'?3:c=='W'?2:0;
}
static int dungeon_enemy_damage(char c){return c=='B'||c=='W'?2:1;}
static void dungeon_floor(pa_dungeon_t *g){
    for(int y=0;y<PA_MAP;y++)for(int x=0;x<PA_MAP;x++){
        char c=(x==0||y==0||x==PA_MAP-1||y==PA_MAP-1)?'#':' ';
        if(c==' ' && (pa_rand(&g->seed)%10)==0)c='#';
        g->tiles[y][x]=c;g->enemy_hp[y][x]=0;
    }
    /* Guaranteed enemy-free corridor from spawn across the north edge
     * and down the east edge to the exit. Random objects never block it. */
    for(int x=1;x<=7;x++)g->tiles[1][x]=' ';
    for(int y=1;y<=7;y++)g->tiles[y][7]=' ';
    g->tiles[7][7]='>';
    /* Optional altar on the guaranteed open corridor. Never seals the exit. */
    if(!g->relic && g->floor<=4)g->tiles[2][4]='!';
    /* A guaranteed reachable gatekeeper: the stairs cannot be rushed past
     * without choosing at least one fight. This tile lies on the safe path. */
    const char guardians[]={'r','G','B','W'};
    char guardian=guardians[(g->floor-1)%4];
    g->tiles[4][7]=guardian;
    g->enemy_hp[4][7]=(uint8_t)(dungeon_enemy_hp(guardian)+g->floor/7);
    g->floor_kills=0;
    int count=dungeon_min(3+g->floor/3,6);
    const char enemies[]={'r','G','B','W'};
    for(int i=0;i<count+2;i++){
        /* Bounded placement even for unlucky seeds. */
        for(int attempt=0;attempt<48;attempt++){
            int x=2+(int)(pa_rand(&g->seed)%5),y=2+(int)(pa_rand(&g->seed)%5);
            if(g->tiles[y][x]!=' ')continue;
            char c=(i<count)?enemies[(pa_rand(&g->seed)+i)%4]:(i==count?'+':'$');
            g->tiles[y][x]=c;g->enemy_hp[y][x]=(uint8_t)dungeon_enemy_hp(c);break;
        }
    }
    g->x=1;g->y=1;g->guard=(g->relic==2);g->last_event=PA_DUNGEON_NONE;g->last_damage=0;
    g->focus=dungeon_min(g->focus+1,3);
}
void pa_dungeon_reset(pa_dungeon_t *g,uint32_t seed){
    memset(g,0,sizeof(*g));g->seed=seed?seed:321u;
    g->hp=g->max_hp=6;g->floor=1;g->potions=1;g->focus=2;
    dungeon_floor(g);
}
bool pa_dungeon_upgrade(pa_dungeon_t *g,int choice){
    if(g->over||!g->pending_upgrade||choice<0||choice>1)return false;
    if(choice==0){g->max_hp=dungeon_min(g->max_hp+1,12);
                  g->hp=dungeon_min(g->max_hp,g->hp+3);}
    else {g->weapon=dungeon_min(g->weapon+1,3);}
    g->upgrade_choice=choice;g->pending_upgrade=0;g->last_event=PA_DUNGEON_HEAL;
    return true;
}
bool pa_dungeon_take_relic(pa_dungeon_t *g,int choice){
    if(!g||g->over||!g->pending_relic||choice<0||choice>1||g->max_hp<=3)return false;
    /* A visible, meaningful tradeoff: permanent max HP for a run-wide perk. */
    g->max_hp--;
    if(g->hp>g->max_hp)g->hp=g->max_hp;
    g->relic=choice+1;
    g->pending_relic=0;g->last_event=PA_DUNGEON_RELIC;
    if(g->relic==2)g->guard=1;
    return true;
}
bool pa_dungeon_guard(pa_dungeon_t *g){
    if(g->over||g->pending_upgrade||g->pending_relic||g->guard||g->focus<=0)return false;
    g->focus--;g->guard=1;g->last_event=PA_DUNGEON_NONE;return true;
}
bool pa_dungeon_use_potion(pa_dungeon_t *g){
    if(g->over||g->pending_upgrade||g->pending_relic||g->potions<=0||g->hp>=g->max_hp)return false;
    g->potions--;g->hp=dungeon_min(g->max_hp,g->hp+4);
    g->last_event=PA_DUNGEON_HEAL;return true;
}
bool pa_dungeon_move(pa_dungeon_t *g,pa_dir_t d){
    if(g->over||g->pending_upgrade||g->pending_relic)return false;
    int dx,dy;move_vec(d,&dx,&dy);int nx=g->x+dx,ny=g->y+dy;
    if(!inside(nx,ny)||g->tiles[ny][nx]=='#'){
        g->last_event=PA_DUNGEON_BLOCK;return false;
    }
    char c=g->tiles[ny][nx];g->turns++;g->last_damage=0;
    if(dungeon_enemy(c)){
        int damage=1+g->weapon;
        int left=(int)g->enemy_hp[ny][nx]-damage;
        if(left<=0){
            g->tiles[ny][nx]=' ';g->enemy_hp[ny][nx]=0;
            g->kills++;g->floor_kills++;g->gold+=c=='B'?5:c=='W'?4:2;
            g->focus=dungeon_min(g->focus+1,3);
            if(g->relic==1 && g->kills%2==0)
                g->hp=dungeon_min(g->max_hp,g->hp+1);
            g->last_event=PA_DUNGEON_KILL;
        }else{
            g->enemy_hp[ny][nx]=(uint8_t)left;
            /* Guard absorbs two points, or one against piercing wisps. */
            int retaliation=dungeon_enemy_damage(c)+((g->floor>=7&&c=='B')?1:0);
            if(g->guard)retaliation=dungeon_max(0,retaliation-(c=='W'?1:2));
            g->guard=0;
            g->hp-=retaliation;g->last_damage=retaliation;
            g->last_event=retaliation?PA_DUNGEON_HURT:PA_DUNGEON_HIT;
            if(g->hp<=0){g->hp=0;g->over=true;}
            return true; /* a hit without entering the occupied tile */
        }
    }else if(c=='!'){
        g->pending_relic=1;g->relic_choice=0;
        g->last_event=PA_DUNGEON_ALTAR;
    }else if(c=='+'){
        g->potions=dungeon_min(g->potions+1,5);g->last_event=PA_DUNGEON_HEAL;
    }else if(c=='$'){
        g->gold+=8;g->last_event=PA_DUNGEON_LOOT;
    }else if(c=='>'){
        if(g->floor_kills==0){g->last_event=PA_DUNGEON_SEALED;return false;}
        g->floor++;g->gold+=5;dungeon_floor(g);
        g->pending_upgrade=1;g->upgrade_choice=0;
        g->last_event=PA_DUNGEON_STAIRS;return true;
    }else{
        g->last_event=PA_DUNGEON_NONE;
    }
    g->tiles[ny][nx]=' ';g->x=nx;g->y=ny;return true;
}
