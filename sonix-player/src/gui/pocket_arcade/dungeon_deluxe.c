#include "dungeon_heavensfall.h"
#define _POSIX_C_SOURCE 200809L
/* Pocket Dungeon: Relics of the Lantern - procedural core. GPL-3.0-or-later. */
#include "dungeon_deluxe.h"
#include "dungeon_world_play.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <limits.h>
#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

static int min_int(int a,int b){return a<b?a:b;}
static uint32_t saturated_add(uint32_t old,uint32_t n){return UINT32_MAX-old<n?UINT32_MAX:old+n;}
static void earn_gold(dg_game *g,uint32_t n){
    g->gold=saturated_add(g->gold,n);
    g->total_gold=saturated_add(g->total_gold,n);
}
static int abs_int(int a){return a<0?-a:a;}
uint32_t dg_hash(uint32_t v){v^=v>>16;v*=0x7feb352du;v^=v>>15;v*=0x846ca68bu;v^=v>>16;return v?v:0xa341316cu;}
static uint32_t rng_next(uint32_t *seed){uint32_t x=*seed;x^=x<<13;x^=x>>17;x^=x<<5;*seed=x;return x;}
static unsigned rng_below(uint32_t *seed,unsigned max){return max?rng_next(seed)%max:0;}
static const int dir_dx[4] = {0, 1, 0, -1};
static const int dir_dy[4] = {-1, 0, 1, 0};
static const uint8_t mask[4]={DG_N,DG_E,DG_S,DG_WEST};
static const uint8_t reverse[4]={DG_S,DG_WEST,DG_N,DG_E};
static const char *const WEAPON_NAMES[]={"Knight Sword","Hunter Spear","Grave Hammer","Shadow Dagger","Rune Wand","Sacred Lantern"};
const char *dg_weapon_name(uint8_t weapon){return weapon<=DG_LANTERN?WEAPON_NAMES[weapon]:"Unknown";}

/* Resonances are derived from the existing relic bitset.  No new persistent
 * state, allocation, or RNG is involved: old checkpoints remain compatible. */
const char *dg_resonance_name(const dg_game *g){
    if(!g)return "None";
    if((g->relics&(DG_RELIC_BLOOD|DG_RELIC_THORN))==(DG_RELIC_BLOOD|DG_RELIC_THORN))return "Thornheart";
    if((g->relics&(DG_RELIC_MIRROR|DG_RELIC_STONE))==(DG_RELIC_MIRROR|DG_RELIC_STONE))return "Iron Oath";
    if((g->relics&(DG_RELIC_EMBER|DG_RELIC_FROST))==(DG_RELIC_EMBER|DG_RELIC_FROST))return "Winterfire";
    if((g->relics&(DG_RELIC_MOON|DG_RELIC_GHOST))==(DG_RELIC_MOON|DG_RELIC_GHOST))return "Second Dawn";
    return "None";
}
static bool has_pair(const dg_game *g,uint8_t pair){return (g->relics&pair)==pair;}

const char *dg_boss_name(const dg_game *g){
    static const char *const names[]={"The Bone King","The Lantern Warden","The Red Bishop"};
    /* The ordinal is only meaningful on a real four-floor boss milestone. */
    if(!g || g->depth<4u || g->depth%4u!=0u)return "Unknown";
    return names[((g->depth/4u)-1u)%3u];
}
static uint32_t floor_seed(uint32_t root,uint32_t depth){return dg_hash(root^dg_hash(depth*0x9e3779b9u));}
static int room_at(const dg_game *g,int x,int y){return x<0||x>=DG_MAP_W||y<0||y>=DG_MAP_H?-1:(g->room_positions[y][x]==DG_NONE?-1:g->room_positions[y][x]);}
static void carve_room(dg_room *r,uint32_t seed){
    uint32_t rng=dg_hash(seed);
    for(int y=0;y<DG_H;y++)for(int x=0;x<DG_W;x++){
        r->tiles[y][x]=(x==0||x==DG_W-1||y==0||y==DG_H-1)?'#':(rng_below(&rng,9)==0?':':'.');
    }
    if(r->exits&DG_N)r->tiles[0][4]='D';
    if(r->exits&DG_E)r->tiles[3][DG_W-1]='D';
    if(r->exits&DG_S)r->tiles[DG_H-1][4]='D';
    if(r->exits&DG_WEST)r->tiles[3][0]='D';
    /* Hand-placed-looking obstacles, sampled procedurally, never blocking doors,
     * center spawn, or important objects. Connectivity is verified and rejected. */
    for(int i=0;i<7;i++){
        int x=2+(int)rng_below(&rng,6),y=2+(int)rng_below(&rng,4);
        if(x==4||x==5||y==3||y==4||x==8||(x==7&&y==5))continue;
        r->tiles[y][x]='O';
        bool good=true;
        for(int d=0;d<4;d++)if(r->exits&mask[d]){
            int ex=d==1?8:d==3?1:4,ey=d==0?1:d==2?6:3;
            if(!dg_map_reachable(r,4,4,ex,ey)){good=false;break;}
        }
        if(!good)r->tiles[y][x]='.';
    }
}
bool dg_map_reachable(const dg_room *r,int sx,int sy,int gx,int gy){
    if(!r||sx<0||sy<0||gx<0||gy<0||sx>=DG_W||gx>=DG_W||sy>=DG_H||gy>=DG_H)return false;
    uint8_t seen[DG_H][DG_W]={{0}};int queue[DG_W*DG_H],h=0,t=0;
    if(r->tiles[sy][sx]=='#'||r->tiles[sy][sx]=='O'||r->tiles[gy][gx]=='#'||r->tiles[gy][gx]=='O')return false;
    seen[sy][sx]=1;queue[t++]=sy*DG_W+sx;
    while(h<t){int n=queue[h++],x=n%DG_W,y=n/DG_W;if(x==gx&&y==gy)return true;
        for(int d=0;d<4;d++){int nx=x+dir_dx[d],ny=y+dir_dy[d];
            if(nx<0||ny<0||nx>=DG_W||ny>=DG_H||seen[ny][nx]||r->tiles[ny][nx]=='#'||r->tiles[ny][nx]=='O')continue;
            seen[ny][nx]=1;queue[t++]=ny*DG_W+nx;
        }
    }return false;
}
int dg_enemies_alive(const dg_room *r){int n=0;for(int i=0;i<DG_FOES;i++)if(r->foes[i].active)n++;return n;}
const char *dg_room_name(dg_room_kind kind) {
    static const char *const names[] = {
        "Combat Hall", "Elite Chamber", "Treasure Vault",
        "Healing Spring", "Relic Shrine", "Wandering Merchant",
        "Rune Puzzle", "Boss Sanctum", "Entrance"
    };
    return (unsigned)kind < sizeof(names) / sizeof(names[0])
               ? names[kind] : "Unknown";
}
const char *dg_biome_name(uint32_t depth) {
    static const char *const names[] = {
        "Old Catacombs", "Mold Caverns", "Frozen Vault",
        "Crimson Chapel", "Abyss Forge", "Relic Sanctum"
    };
    if (depth == 0) return "Unknown";
    return names[((depth - 1u) / 2u) % (sizeof(names) / sizeof(names[0]))];
}
/* Atmospheric names are derived only from a room's stable seed and kind.
 * No RNG draws, extra save bytes, heap allocations or audio operations. */
const char *dg_chamber_title(const dg_game *g){
    static const char *const names[9][3] = {
        {"Hall of Falling Ash", "The Bone Gallery", "Whispering Passage"},
        {"Black-Iron Trial", "The Executioner's Walk", "Crown of Thorns"},
        {"Starless Vault", "The Sunken Reliquary", "The Gilded Tomb"},
        {"The Last Warm Spring", "Moonlit Sanctuary", "Well of Cinders"},
        {"Altar of Forgotten Kings", "The Quiet Covenant", "The Broken Halo"},
        {"The Wandering Forge", "Smith of Lost Names", "The Ember Merchant"},
        {"The Rune-Locked Door", "Chamber of Echoes", "The Stone Testament"},
        {"The Sovereign Throne", "Chapel of Last Light", "The Eternal Sentence"},
        {"Lantern's Rest", "The First Descent", "The Unbroken Oath"}
    };
    if(!g || g->current>=g->count)return "Unknown Chamber";
    const dg_room *r=&g->rooms[g->current];
    if(r->kind>DG_ENTRANCE)return "Unknown Chamber";
    uint32_t roll=dg_hash(g->floor_seed ^ ((uint32_t)g->current*0x9e3779b9u));
    return names[r->kind][roll%3u];
}

/* Fate Vaults replace the automatic payout in some treasure chambers.
 * The offer can be re-opened after leaving without altering the save ABI. */
bool dg_is_fate_vault(const dg_game *g){
    if(!g || g->current>=g->count)return false;
    if(g->rooms[g->current].kind!=DG_TREASURE)return false;
    return (dg_hash(g->floor_seed ^ ((uint32_t)g->current*0xa511e9b3u))&1u)!=0;
}

static int enemy_health(uint8_t kind,uint32_t depth){static const int h[] = {2, 3, 1, 2, 4, 3, 2, 3};return min_int(h[kind]+(int)(((depth-1u)/8u)>6u?6u:(depth-1u)/8u),18);}
static void populate(dg_room *r,uint32_t root,uint32_t depth,int id){
    uint32_t rng=dg_hash(root^dg_hash((uint32_t)id*0x517cc1b7u));
    carve_room(r,rng);
    /* Always leave a clear central corridor and door approach. */
    const int objx=7,objy=5;
    if(r->kind==DG_TREASURE)r->tiles[objy][objx]='?';
    if(r->kind==DG_HEAL)r->tiles[objy][objx]='+';
    if(r->kind==DG_SHRINE)r->tiles[objy][objx]='!';
    if(r->kind==DG_MERCHANT)r->tiles[objy][objx]='$';
    if(r->kind==DG_PUZZLE){
        /* Four authored push puzzles, mirrored/rotated by seed. This is a
         * push-the-crate-on-the-rune objective, not free treasure. Clear
         * incidental obstacles: each variation has a documented solution. */
        for(int y=1;y<DG_H-1;y++)for(int x=1;x<DG_W-1;x++)if(r->tiles[y][x]=='O')r->tiles[y][x]='.';
        static const uint8_t starts[][4]={{5,3,7,3},{5,4,3,4},{3,4,3,2},{6,2,6,5}};
        unsigned v=rng_below(&rng,4);
        r->puzzle_start_x=r->puzzle_x=starts[v][0];
        r->puzzle_start_y=r->puzzle_y=starts[v][1];
        r->puzzle_goal_x=starts[v][2];r->puzzle_goal_y=starts[v][3];
        r->tiles[r->puzzle_goal_y][r->puzzle_goal_x]='*';
    }
    /* Room identity is authored on top of the seeded floor geometry.
     * Decorative glyphs are walkable and never replace doorways/loot. */
    if(r->kind==DG_BOSS){
        r->tiles[1][2]='t';r->tiles[1][7]='t';
        r->tiles[5][2]='v';r->tiles[5][7]='v';
        r->tiles[2][4]='r';
        /* Ensure the royal path from door approach to center stays open. */
    }else if(r->kind==DG_SHRINE){r->tiles[1][2]='t';r->tiles[2][5]='r';}
    else if(r->kind==DG_ELITE){r->tiles[1][2]='v';r->tiles[2][4]='r';}
    else if(r->kind==DG_ENTRANCE){r->tiles[1][2]='t';}
    if(r->kind==DG_ENTRANCE){r->cleared=1;return;}
    if(r->kind==DG_TREASURE||r->kind==DG_HEAL||r->kind==DG_SHRINE||r->kind==DG_MERCHANT||r->kind==DG_PUZZLE){r->cleared=1;return;}
    int limit=r->kind==DG_BOSS?1:r->kind==DG_ELITE?3:2+(int)rng_below(&rng,2);
    for(int i=0;i<limit&&i<DG_FOES;i++){
        /* Slots avoid center spawn, entrances and each other. */
        static const int sx[]={6,7,6,3,2,7},sy[]={2,4,5,5,2,2};
        dg_foe *e=&r->foes[i];
        static const int boss_kinds[]={DG_STONE,DG_GHOST,DG_CULTIST};
        unsigned pool=depth>=12u?8u:2u+depth/2u;
        int k=r->kind==DG_BOSS?boss_kinds[((depth/4u)-1u)%3u]:(int)rng_below(&rng,pool);
        if(r->kind==DG_ELITE&&i==0)k=DG_STONE;
        e->kind=(uint8_t)k;e->x=(uint8_t)sx[i];e->y=(uint8_t)sy[i];
        if(r->tiles[e->y][e->x]=='O')r->tiles[e->y][e->x]='.';
        e->max_hp=e->hp=(uint8_t)min_int(enemy_health((uint8_t)k,depth)+(r->kind==DG_BOSS?6:0),24);
        e->active=1;r->foe_count++;
        if(r->kind==DG_BOSS){r->boss_variant=(uint8_t)(((depth/4u)-1u)%3u);r->boss_phase=1; e->hp=e->max_hp=(uint8_t)min_int(14+(int)min_int(depth/8u,6u),24);}
    }
}
static void make_floor(dg_game *g){
    /* Independent floor seed: save/reload and action timing never affect layout. */
    uint32_t rng=floor_seed(g->seed,g->depth);
    g->floor_seed=rng;
    g->count=0;g->current=0;g->exit_room=0;
    memset(g->rooms,0,sizeof(g->rooms));memset(g->room_positions,DG_NONE,sizeof(g->room_positions));
    int startx=1,starty=1;g->rooms[0].gx=(uint8_t)startx;g->rooms[0].gy=(uint8_t)starty;
    g->room_positions[starty][startx]=0;g->count=1;
    int desired=8+(int)rng_below(&rng,5);
    while(g->count<desired){
        /* Grow a random spanning tree. Every new room connects to an existing
         * room; loops can subsequently be added. No disconnected rooms. */
        int options[DG_ROOMS*4][3],n=0;
        for(int i=0;i<g->count;i++)for(int d=0;d<4;d++){
            int x=g->rooms[i].gx+dir_dx[d],y=g->rooms[i].gy+dir_dy[d];
            if(room_at(g,x,y)<0&&x>=0&&x<DG_MAP_W&&y>=0&&y<DG_MAP_H){options[n][0]=i;options[n][1]=d;options[n][2]=x+y*DG_MAP_W;n++;}
        }
        if(n==0)break;
        int choice=(int)rng_below(&rng,(unsigned)n),parent=options[choice][0],d=options[choice][1];
        int loc=options[choice][2],x=loc%DG_MAP_W,y=loc/DG_MAP_W,k=g->count++;
        g->rooms[k].gx=(uint8_t)x;g->rooms[k].gy=(uint8_t)y;g->room_positions[y][x]=(uint8_t)k;
        g->rooms[parent].exits|=mask[d];g->rooms[k].exits|=reverse[d];
    }
    /* Add occasional cycle links for route variety, still deterministic. */
    for(int i=0;i<g->count;i++)for(int d=0;d<2;d++){
        int k=room_at(g,g->rooms[i].gx+dir_dx[d],g->rooms[i].gy+dir_dy[d]);
        if(k>=0&&!(g->rooms[i].exits&mask[d])&&rng_below(&rng,5)==0){
            g->rooms[i].exits|=mask[d];g->rooms[k].exits|=reverse[d];
        }
    }
    /* Farthest room by door distance, tie-break via random room permutation. */
    int distance[DG_ROOMS];for(int i=0;i<DG_ROOMS;i++)distance[i]=-1;
    int q[DG_ROOMS],front=0,back=0;q[back++]=0;distance[0]=0;
    while(front<back){int i=q[front++];dg_room *r=&g->rooms[i];
        for(int d=0;d<4;d++)if(r->exits&mask[d]){
            int k=room_at(g,r->gx+dir_dx[d],r->gy+dir_dy[d]);
            if(k>=0&&distance[k]<0){distance[k]=distance[i]+1;q[back++]=k;}
        }
    }
    int far=0;for(int i=1;i<g->count;i++)if(distance[i]>distance[far])far=i;
    g->exit_room=(uint8_t)far;
    int other[DG_ROOMS],others=0;
    for(int i=1;i<g->count;i++)if(i!=far)other[others++]=i;
    /* Shuffle functional room types; preserve entrance and farthest exit. */
    for(int i=others-1;i>0;i--){int j=(int)rng_below(&rng,(unsigned)(i+1));int t=other[i];other[i]=other[j];other[j]=t;}
    dg_room_kind kinds[]={DG_COMBAT,DG_ELITE,DG_TREASURE,DG_HEAL,DG_SHRINE,DG_MERCHANT,DG_PUZZLE,DG_COMBAT,DG_COMBAT,DG_COMBAT};
    g->rooms[0].kind=DG_ENTRANCE;
    g->rooms[far].kind=(g->depth%4u==0u)?DG_BOSS:DG_COMBAT;
    for(int i=0;i<others;i++)g->rooms[other[i]].kind=kinds[i%10];
    for(int i=0;i<g->count;i++){
        dg_room *r=&g->rooms[i];populate(r,rng,g->depth,i);
        /* House-specific historical enemy variants use existing foe slots and
         * hit points; no extra save fields or gameplay RNG draws. */
        unsigned house=dg_hash(g->floor_seed ^ (0x7c4a9e13u + (uint32_t)i*0x9e3779b9u))%8u;
        for(int j=0;j<DG_FOES;j++)dg_world_mark_foe(&r->foes[j],house);
        dg_world_furnish(r,house);
        if(r->kind!=DG_BOSS){
            unsigned chapter=dg_hf_chapter(g);
            for(int j=0;j<DG_FOES;j++)dg_hf_train(&r->foes[j],chapter);
            dg_hf_scar(r,chapter);
            dg_hf_install(g,r,(unsigned)i);
        }
        if(i==far)r->tiles[5][8]='>'; /* stairs don't change until all foes dead */
    }
    g->x=4;g->y=4;g->rooms[0].visited=1;
    g->pending_relic=0;g->room_choice=0;g->event=DG_EVT_NONE;
}
void dg_new(dg_game *g,uint32_t seed){
    memset(g,0,sizeof(*g));g->seed=seed?seed:0xc01d1234u;
    g->depth=1;g->max_hp=8;g->hp=8;g->attack=2;g->potions=2;g->focus=2;g->weapon=DG_SWORD;g->weapon_unlocked=(1u<<DG_SWORD);g->mana=3;g->relic_slots=3;g->alive=1;make_floor(g);
}
bool dg_next_floor(dg_game *g){
    if(!g||!g->alive||g->depth==UINT32_MAX)return false;
    g->depth++;g->mana=3;if(g->depth%4u==1u){g->max_hp=(uint8_t)min_int(DG_MAX_HP,g->max_hp+1);g->hp=(uint8_t)min_int(g->max_hp,g->hp+3);g->attack=(uint8_t)min_int(12,g->attack+1);g->focus=2;g->mana=3;}
    g->ghost_charge=(g->relics&DG_RELIC_GHOST)?1u:0u;
    make_floor(g);g->event=DG_EVT_DESCEND;return true;
}
static bool blocked(const dg_room *r,int x,int y){return x<0||x>=DG_W||y<0||y>=DG_H||r->tiles[y][x]=='#'||r->tiles[y][x]=='O';}
static int foe_at(const dg_room *r,int x,int y){for(int i=0;i<DG_FOES;i++)if(r->foes[i].active&&r->foes[i].x==x&&r->foes[i].y==y)return i;return -1;}
static void note(dg_game *g,dg_event event,int x,int y,int amount){g->event=event;g->event_x=(uint8_t)x;g->event_y=(uint8_t)y;g->event_amount=(uint8_t)min_int(255,amount);}
static bool boss_area(const dg_room *r,const dg_foe *e,int x,int y){
    if(!e->windup)return false;
    if(r->boss_variant==0){ /* Bone King's hammer: a three-cell shockwave. */
        return abs_int(y-(int)e->ty)<=(r->boss_phase>=2?1:0) && abs_int(x-(int)e->tx)<=1;
    }
    if(r->boss_variant==1){ /* Warden: either a cross or a narrow prison lane. */
        return r->boss_phase>=2?(x==e->tx||y==e->ty):(x==e->tx);
    }
    /* Bishop: a 3x3 curse, growing to a cross near defeat. */
    return (abs_int(x-(int)e->tx)<=1&&abs_int(y-(int)e->ty)<=1) ||
        (r->boss_phase>=3 && (x==e->tx||y==e->ty));
}
bool dg_danger_at(const dg_game *g,int x,int y){
    if(!g||g->current>=g->count||x<0||x>=DG_W||y<0||y>=DG_H)return false;
    const dg_room *r=&g->rooms[g->current];
    for(int i=0;i<DG_FOES;i++){const dg_foe *e=&r->foes[i];if(!e->active||!e->windup)continue;
        if(r->kind==DG_BOSS && i==0){if(boss_area(r,e,x,y))return true;}
        else if(e->tx==x&&e->ty==y)return true;
    }return false;
}
static void kill_enemy(dg_game *g,dg_room *r,dg_foe *e){
    /* A mature slime divides once into a fragile hatchling.  The offspring
     * has max_hp=1 so it cannot recursively split.  Spawn only onto walkable,
     * unoccupied interior cells (never the player or an exit). */
    const bool split=e->kind==DG_SLIME && e->max_hp>1;
    const int dead_x=e->x,dead_y=e->y;
    e->active=0;
    if(split){
        int free_slot=-1;
        for(int i=0;i<DG_FOES;i++)if(!r->foes[i].active){free_slot=i;break;}
        if(free_slot>=0){
            for(int d=0;d<4;d++){
                int nx=dead_x+dir_dx[d],ny=dead_y+dir_dy[d];
                if(blocked(r,nx,ny)||nx<=0||ny<=0||nx>=DG_W-1||ny>=DG_H-1||
                   foe_at(r,nx,ny)>=0||(g->x==nx&&g->y==ny))continue;
                dg_foe hatchling={0};
                hatchling.x=(uint8_t)nx;hatchling.y=(uint8_t)ny;
                hatchling.tx=hatchling.x;hatchling.ty=hatchling.y;
                hatchling.kind=DG_SLIME;hatchling.hp=1;
                hatchling.max_hp=1;hatchling.active=1;hatchling.stunned=1;
                r->foes[free_slot]=hatchling;
                if(r->foe_count<DG_FOES)r->foe_count++;
                break;
            }
        }
    }
    if(g->total_kills<UINT32_MAX)g->total_kills++;
    unsigned gold=2u+(g->depth/4u>10u?10u:g->depth/4u);
    if(g->relics&DG_RELIC_MOON)gold+=gold/2+1;
    earn_gold(g,gold);
    if(g->relics&DG_RELIC_BLOOD&&g->hp<g->max_hp){
        g->hp++;
        if(has_pair(g,DG_RELIC_BLOOD|DG_RELIC_THORN)&&
           g->hp*2u<=g->max_hp&&g->hp<g->max_hp)g->hp++;
    }
    if(g->weapon==DG_WAND && g->mana<5)g->mana++;
    if(!dg_enemies_alive(r)){r->cleared=1;if(r->kind==DG_BOSS&&g->boss_count<UINT8_MAX)g->boss_count++;}
}
static void boss_health_phase(dg_game *g,dg_room *r){
    if(r->kind!=DG_BOSS||!r->foes[0].active)return;
    dg_foe *boss=&r->foes[0];
    uint8_t phase=(uint8_t)(boss->hp*2u<=boss->max_hp?2:1);
    if(r->boss_variant==2 && boss->hp*4u<=boss->max_hp)phase=3;
    if(phase<=r->boss_phase)return;
    r->boss_phase=phase;
    /* A phase change changes the attack footprint. Cancel any old warning so
     * a newly expanded attack is visibly telegraphed before it can hit. */
    boss->windup=0;
    /* At most two adds. Only spawn on free, traversable cells. */
    static const uint8_t sx[]={2,7},sy[]={2,5};
    for(int j=0;j<(r->boss_variant==0?1:2);j++){
        dg_foe *e=&r->foes[j+1];
        if(e->active||blocked(r,sx[j],sy[j])||foe_at(r,sx[j],sy[j])>=0||(g->x==sx[j]&&g->y==sy[j]))continue;
        *e=(dg_foe){.x=sx[j],.y=sy[j],.tx=sx[j],.ty=sy[j],.kind=(uint8_t)(r->boss_variant==0?DG_SKEL:r->boss_variant==1?DG_GHOST:DG_CULTIST),.hp=2,.max_hp=2,.active=1};
        if(r->foe_count<DG_FOES)r->foe_count++;
    }
    note(g,DG_EVT_BOSS_PHASE,boss->x,boss->y,phase);
}
static bool in_attack_range(const dg_foe *e,int x,int y){
    int dist=abs_int((int)e->x-x)+abs_int((int)e->y-y);
    /* Beetles prepare a heavy, clearly marked straight-line charge; cultists
     * retain their ranged curse. Every attack still consumes a windup turn. */
    if(e->kind==DG_CULTIST||e->kind==DG_BEETLE)
        return dist<=3&&(e->x==x||e->y==y);
    return dist==1;
}
/* R16 opt-in instrumentation: zero cost in production builds. */
#ifdef DG_PROFILE_BFS
#include "dungeon_bfs_profile.h"
static dg_bfs_counters dg_bfs_stats;
void dg_bfs_reset(void){dg_bfs_stats=(dg_bfs_counters){0};}
dg_bfs_counters dg_bfs_read(void){return dg_bfs_stats;}
#define BFS_COUNT(field) (++dg_bfs_stats.field)
#else
#define BFS_COUNT(field) ((void)0)
#endif
/* Deterministic bounded breadth-first pursuit.  The original straight-line
 * chase got stuck against furniture indefinitely.  This searches at most 80
 * tiles, respects occupied cells and preserves the windup combat cadence. */
/* R17: failed-search memo is scoped to one enemy phase. Exact room bytes
 * (not a hash) guard against stale occupancy, tile or boss-state reuse.
 * Only failures are cached; successful path tie-breaking is untouched. */
#ifndef DG_DISABLE_FAILED_BFS_CACHE
static struct {
    dg_room room;
    int goal;
    uint8_t reachable[DG_W*DG_H];
    bool valid;
} failed_bfs_cache;
#ifdef DG_PROFILE_BFS
static unsigned long r17_cache_hits;
unsigned long dg_r17_cache_hits(void){return r17_cache_hits;}
#endif
#endif
static bool pursuit_step(const dg_room *r, const dg_foe *e,
                         int gx, int gy, int *outx, int *outy){
    int parent[DG_W*DG_H];
    int queue[DG_W*DG_H];
    for(int i=0;i<DG_W*DG_H;i++)parent[i]=-1;
    int start=(int)e->y*DG_W+e->x;
    int goal=gy*DG_W+gx;
    int head=0,tail=0;
#ifndef DG_DISABLE_FAILED_BFS_CACHE
    if(failed_bfs_cache.valid && failed_bfs_cache.goal==goal &&
       failed_bfs_cache.reachable[start] &&
       memcmp(&failed_bfs_cache.room,r,sizeof(*r))==0){
#ifdef DG_PROFILE_BFS
        ++r17_cache_hits;
#endif
        return false;
    }
#endif
    BFS_COUNT(searches);
    parent[start]=start;
    queue[tail++]=start;
    while(head<tail && parent[goal]<0){
        int pos=queue[head++];
        BFS_COUNT(expansions);
        int px=pos%DG_W,py=pos/DG_W;
        for(int d=0;d<4;d++){
            int nx=px+dir_dx[d],ny=py+dir_dy[d];
            if(blocked(r,nx,ny))continue;
            int next=ny*DG_W+nx;
            if(parent[next]>=0)continue;
            if(next!=goal && foe_at(r,nx,ny)>=0)continue;
            /* No enemy stands in a portal or on an exit staircase. */
            if(next!=goal && (r->tiles[ny][nx]=='D'||r->tiles[ny][nx]=='>'))continue;
            parent[next]=pos;
            queue[tail++]=next;
            BFS_COUNT(enqueues);
        }
    }
    if(parent[goal]<0){
        BFS_COUNT(failures);
#ifndef DG_DISABLE_FAILED_BFS_CACHE
        failed_bfs_cache.room=*r;
        failed_bfs_cache.goal=goal;
        for(int k=0;k<DG_W*DG_H;k++)
            failed_bfs_cache.reachable[k]=(uint8_t)(parent[k]>=0);
        failed_bfs_cache.valid=true;
#endif
        return false;
    }
    int at=goal;
    while(parent[at]!=start && at!=start)at=parent[at];
    if(at==start)return false;
    BFS_COUNT(successes);
    *outx=at%DG_W;
    *outy=at/DG_W;
    return true;
}

/* R2: Cultists threaten straight corridors from a distance.  The shot uses
 * the existing telegraph target and windup, so old saves and renderers work.
 * No gameplay RNG is consumed; obstacles block the line of sight. */
static bool cultist_has_shot(const dg_room *r,const dg_foe *e,int px,int py){
    int dx=px-(int)e->x,dy=py-(int)e->y;
    int dist=abs_int(dx)+abs_int(dy);
    if(dist<2||dist>3||(dx!=0&&dy!=0))return false;
    int sx=(dx>0)-(dx<0),sy=(dy>0)-(dy<0);
    for(int n=1;n<dist;n++){
        int x=(int)e->x+sx*n,y=(int)e->y+sy*n;
        if(r->tiles[y][x]=='#'||r->tiles[y][x]=='O'||foe_at(r,x,y)>=0)return false;
    }
    return true;
}

/* R2 tactical movement: deterministic, bounded, no new saved fields. */
static bool tactical_step(const dg_room *r,const dg_game *g,const dg_foe *e,
                          bool retreat,int *outx,int *outy){
    int best=retreat?-1:INT_MAX,found=0;
    /* Rotate tie-breaking by turn and enemy kind without touching game RNG. */
    unsigned start=(unsigned)((g->turns+(unsigned)e->kind)&3u);
    for(unsigned k=0;k<4;k++){
        unsigned d=(start+k)&3u;
        int x=(int)e->x+dir_dx[d],y=(int)e->y+dir_dy[d];
        if(blocked(r,x,y)||foe_at(r,x,y)>=0||(x==g->x&&y==g->y))continue;
        int dist=abs_int(x-(int)g->x)+abs_int(y-(int)g->y);
        if((retreat&&dist>best)||(!retreat&&dist<best)){
            best=dist;*outx=x;*outy=y;found=1;
        }
    }
    return found!=0;
}

static void enemies_turn(dg_game *g){
#ifndef DG_DISABLE_FAILED_BFS_CACHE
    failed_bfs_cache.valid=false;
#endif
    dg_room *r=&g->rooms[g->current];
    for(int i=0;i<DG_FOES;i++){
        dg_foe *e=&r->foes[i];if(!e->active||!g->alive)continue;
        if(e->stunned){e->stunned=0;e->windup=0;continue;}
        if(e->windup){
            bool hit=(r->kind==DG_BOSS&&i==0)?boss_area(r,e,g->x,g->y):e->tx==g->x&&e->ty==g->y;
            e->windup=0;
            if(hit){
                /* Sibrell's bells silence one mana on a successful unguarded
                 * attack. Existing telegraph remains the warning. */
                if(r->kind!=DG_BOSS && dg_world_house(g)==2 &&
                   e->kind==DG_CULTIST && !g->guard && g->mana)
                    g->mana--;
                int damage=(r->kind==DG_BOSS&&i==0)?(r->boss_phase>=3?3:2):
                    (e->kind==DG_STONE||e->kind==DG_BEETLE)?2:1;
                if(g->relics&DG_RELIC_STONE)damage=damage>0?damage-1:0;
                if(g->guard){damage=0;g->guard=0;
                    if(has_pair(g,DG_RELIC_MIRROR|DG_RELIC_STONE)&&g->mana<5)g->mana++;
                    if(g->relics&DG_RELIC_MIRROR){if(e->hp>0)e->hp--;if(!e->hp)kill_enemy(g,r,e);}
                }
                if(damage){
                    g->hp=(uint8_t)(g->hp>damage?g->hp-damage:0);
                    unsigned stolen=0;
                    if(e->kind==DG_MIMIC){
                        stolen=g->gold<3u?g->gold:3u;
                        g->gold-=stolen;
                    }
                    note(g,stolen?DG_EVT_STOLEN:DG_EVT_HURT,
                         g->x,g->y,stolen?(int)stolen:damage);
                }
                else note(g,DG_EVT_GUARD,g->x,g->y,0);
                if(!g->hp){if((g->relics&DG_RELIC_GHOST)&&g->ghost_charge){
                        g->ghost_charge=0;g->hp=1;
                        if(has_pair(g,DG_RELIC_MOON|DG_RELIC_GHOST)){
                            g->mana=3;g->focus=2;
                        }
                        note(g,DG_EVT_GUARD,g->x,g->y,0);}
                    else{g->alive=0;g->event=DG_EVT_DEAD;return;}}
            }continue;
        }
        /* R4 institutional combat: a Tallyman pursues the uncounted with a
         * second step every fourth turn. The extra move obeys normal collision. */
        unsigned house=dg_world_house(g);
        if(r->kind!=DG_BOSS && house==0 && e->kind==DG_SKEL &&
           (g->turns%4u)==0u){
            int cx=0,cy=0;
            if(pursuit_step(r,e,g->x,g->y,&cx,&cy) &&
               (cx!=g->x||cy!=g->y)){
                e->x=(uint8_t)cx;e->y=(uint8_t)cy;
            }
        }
        /* Masons alter traversable floor into visibly scarred masonry.
         * Never create a blocking wall: no route or save can be softlocked. */
        if(r->kind!=DG_BOSS && house==1 && e->kind==DG_STONE &&
           (g->turns%3u)==0u && r->tiles[e->y][e->x]=='.')
            r->tiles[e->y][e->x]=':';
        bool boss=r->kind==DG_BOSS&&i==0;
        /* Cultists seek breathing room before firing. Their adjacent attacks
         * still work when boxed in, so pursuit cannot deadlock combat. */
        if(!boss && e->kind==DG_CULTIST &&
           abs_int((int)e->x-g->x)+abs_int((int)e->y-g->y)==1 &&
           (g->turns&1u)==0u){
            int rx=0,ry=0;
            if(tactical_step(r,g,e,true,&rx,&ry)){
                e->x=(uint8_t)rx;e->y=(uint8_t)ry;continue;
            }
        }
        /* Ghosts phase sideways on alternating turns when approached; their
         * old immunity and telegraph behavior remains intact. */
        if(!boss && e->kind==DG_GHOST && (g->turns&3u)==0u &&
           abs_int((int)e->x-g->x)+abs_int((int)e->y-g->y)==2){
            int gx=0,gy=0;
            if(tactical_step(r,g,e,true,&gx,&gy)){
                e->x=(uint8_t)gx;e->y=(uint8_t)gy;continue;
            }
        }
        /* Three different boss rhythms, using only existing state.  The
         * Bone King closes distance before swinging; his hammer is lethal
         * only if the player remains within its marked reach.  The Warden
         * moves to line up a prison column before sealing it.  The Bishop
         * must recover after every third ritual beat, giving a counterattack
         * window.  No RNG and no additional serialized fields. */
        if(boss){
            int dx=abs_int((int)e->x-g->x),dy=abs_int((int)e->y-g->y);
            int nx=0,ny=0;
            if(r->boss_variant==0 && dx+dy>2){
                if(pursuit_step(r,e,g->x,g->y,&nx,&ny) &&
                   (nx!=g->x||ny!=g->y)){e->x=(uint8_t)nx;e->y=(uint8_t)ny;}
                continue;
            }
            if(r->boss_variant==1 && e->x!=g->x && r->boss_phase==1){
                if(pursuit_step(r,e,g->x,g->y,&nx,&ny) &&
                   (nx!=g->x||ny!=g->y)){e->x=(uint8_t)nx;e->y=(uint8_t)ny;}
                continue;
            }
            if(r->boss_variant==2 && g->turns%3u==2u)continue;
        }
        /* Sibrell's war penitents ring before they attack: the bell
         * marks a longer line-of-sight threat, always telegraphed. */
        if(!boss && dg_hf_chapter(g)==2 && house==2 &&
           e->kind==DG_CULTIST && cultist_has_shot(r,e,g->x,g->y)){
            e->windup=1;e->tx=g->x;e->ty=g->y;continue;
        }
        if(boss || in_attack_range(e,g->x,g->y) ||
           (e->kind==DG_CULTIST && cultist_has_shot(r,e,g->x,g->y))){
            e->windup=1;e->tx=g->x;e->ty=g->y;continue;
        }
        if(e->kind==DG_STONE && (g->turns%2u)==1u)continue;
        int nx=0,ny=0;
        if(pursuit_step(r,e,g->x,g->y,&nx,&ny) &&
           (nx!=g->x||ny!=g->y)){
            e->x=(uint8_t)nx;e->y=(uint8_t)ny;
            /* Bats take a second shortest-path step every other turn. */
            if(e->kind==DG_BAT&&(g->turns%2u)==0u){
                int bx=0,by=0;
                if(pursuit_step(r,e,g->x,g->y,&bx,&by) &&
                   (bx!=g->x||by!=g->y)){
                    e->x=(uint8_t)bx;e->y=(uint8_t)by;
                }
            }
        }
    }
}
static bool spend_turn(dg_game *g){if(g->turns<UINT32_MAX)g->turns++;enemies_turn(g);return true;}
static bool apply_attack(dg_game *g,dg_room *r,int fi,int x,int y){
    dg_foe *e=&r->foes[fi];
    int damage=g->attack;
    if(g->weapon==DG_HAMMER){if(g->recovery){g->recovery=0;note(g,DG_EVT_BLOCKED,g->x,g->y,0);return spend_turn(g);}damage+=2;g->recovery=1;}
    if(g->weapon==DG_SPEAR)damage=min_int(damage,2);
    if(g->weapon==DG_DAGGER)damage=1+((g->turns%3u)==0u?3:1);
    if(g->weapon==DG_WAND){if(g->mana){g->mana--;damage=2+g->attack/2;}else if(abs_int((int)e->x-g->x)+abs_int((int)e->y-g->y)==1)damage=1;else return false;}
    if(g->weapon==DG_LANTERN){if(g->mana){g->mana--;damage=1+g->attack/2;}else if(abs_int((int)e->x-g->x)+abs_int((int)e->y-g->y)==1)damage=1;else return false;}
    if(g->weapon==DG_SWORD&&g->guard)damage++;
    if((g->relics&DG_RELIC_THORN)&&g->hp*2u<=g->max_hp)damage+=2;
    if(e->kind==DG_STONE&&g->weapon!=DG_HAMMER)damage=min_int(damage,1);
    if(e->kind==DG_GHOST&&(g->turns%2u)==0u&&!(g->relics&DG_RELIC_EMBER))damage=0;
    if(damage){e->hp=(uint8_t)(e->hp>damage?e->hp-damage:0);
        if(e->hp&&(g->relics&DG_RELIC_EMBER)&&g->turns%3u==0u)e->hp--;
        if(e->hp&&(g->relics&DG_RELIC_FROST)&&
           (g->turns%3u==0u||has_pair(g,DG_RELIC_EMBER|DG_RELIC_FROST)))e->stunned=1;
    }
    if(!e->hp){
        bool split=e->kind==DG_SLIME&&e->max_hp>1;
        kill_enemy(g,r,e);
        bool child=false;
        if(split)for(int i=0;i<DG_FOES;i++)if(r->foes[i].active&&
            r->foes[i].kind==DG_SLIME&&r->foes[i].max_hp==1){child=true;break;}
        note(g,child?DG_EVT_SLIME_SPLIT:DG_EVT_KILL,x,y,damage);
    }
    else{note(g,DG_EVT_HIT,x,y,damage);boss_health_phase(g,r);}
    if(g->weapon==DG_LANTERN&&damage>1){
        /* Short illuminated burst: one damage to nearby creatures, no extra
         * turns. Avoid double hitting primary target. */
        for(int i=0;i<DG_FOES;i++)if(i!=fi&&r->foes[i].active&&
            abs_int((int)r->foes[i].x-g->x)+abs_int((int)r->foes[i].y-g->y)<=2){
            if(--r->foes[i].hp==0)kill_enemy(g,r,&r->foes[i]);
        }
    }
    return spend_turn(g);
}
bool dg_action(dg_game *g,int direction){
    if(!g||!g->alive||g->pending_relic||direction<0||direction>3)return false;
    dg_room *r=&g->rooms[g->current];int nx=(int)g->x+dir_dx[direction],ny=(int)g->y+dir_dy[direction];
    g->event=DG_EVT_NONE;
    if(blocked(r,nx,ny)){note(g,DG_EVT_BLOCKED,g->x,g->y,0);return false;}
    if(r->tiles[ny][nx]=='D'){
        if(dg_enemies_alive(r)>0){note(g,DG_EVT_BLOCKED,nx,ny,0);return false;}
        int other=room_at(g,(int)r->gx+dir_dx[direction],(int)r->gy+dir_dy[direction]);
        if(other<0||!(r->exits&mask[direction]))return false;
        g->current=(uint8_t)other;g->rooms[other].visited=1;
        static const int entryx[4]={4,1,4,8},entryy[4]={6,3,1,3};
        g->x=(uint8_t)entryx[direction];g->y=(uint8_t)entryy[direction];
        if(g->mana<5)g->mana++;
        note(g,g->rooms[other].kind==DG_BOSS?DG_EVT_BOSS:DG_EVT_DOOR,g->x,g->y,0);return true;
    }
    if(r->tiles[ny][nx]=='>'&&g->current==g->exit_room){
        /* The Testament must be judged here: no infinite post-finale descent. */
        if(g->depth==9u){note(g,DG_EVT_BLOCKED,nx,ny,0);return false;}
        if(dg_enemies_alive(r)>0 || !dg_hf_can_descend(g)){note(g,DG_EVT_BLOCKED,nx,ny,0);return false;}
        return dg_next_floor(g);
    }
    int fi=foe_at(r,nx,ny);
    if(fi<0&&(g->weapon==DG_SPEAR||g->weapon==DG_WAND||g->weapon==DG_LANTERN)){
        int reach=g->weapon==DG_WAND?3:g->weapon==DG_SPEAR?2:2;
        for(int k=2;k<=reach;k++){
            int tx=g->x+dir_dx[direction]*k,ty=g->y+dir_dy[direction]*k;
            if(blocked(r,tx,ty))break;
            fi=foe_at(r,tx,ty);if(fi>=0){nx=tx;ny=ty;break;}
        }
    }
    if(fi>=0 && (g->weapon==DG_WAND||g->weapon==DG_LANTERN)&&!g->mana&&
        abs_int(nx-(int)g->x)+abs_int(ny-(int)g->y)>1){
        /* Empty magic reserve must not pin the hero in place. */
        fi=-1;nx=(int)g->x+dir_dx[direction];ny=(int)g->y+dir_dy[direction];
    }
    if(fi>=0)return apply_attack(g,r,fi,nx,ny);
    bool pushed_crate=false;
    if(r->kind==DG_PUZZLE&&!r->puzzle_solved&&nx==r->puzzle_x&&ny==r->puzzle_y){
        int tx=nx+dir_dx[direction],ty=ny+dir_dy[direction];
        if(blocked(r,tx,ty)||r->tiles[ty][tx]=='D'||r->tiles[ty][tx]=='>'||(tx==r->puzzle_x&&ty==r->puzzle_y)){
            note(g,DG_EVT_BLOCKED,nx,ny,0);return false;
        }
        pushed_crate=true;r->puzzle_x=(uint8_t)tx;r->puzzle_y=(uint8_t)ty;
        if(r->puzzle_moves<UINT8_MAX)r->puzzle_moves++;
        if(tx==r->puzzle_goal_x&&ty==r->puzzle_goal_y){
            r->puzzle_solved=1;r->claimed=1;r->cleared=1;
            earn_gold(g,18);note(g,DG_EVT_PUZZLE,tx,ty,18);
        } else note(g,DG_EVT_MOVE,tx,ty,0);
    }
    g->x=(uint8_t)nx;g->y=(uint8_t)ny;if(!pushed_crate)note(g,DG_EVT_MOVE,nx,ny,0);
    uint8_t t=r->tiles[ny][nx];
    if(t=='?'&&!r->claimed){
        if(dg_is_fate_vault(g)){g->pending_relic=3;note(g,DG_EVT_FATE_VAULT,nx,ny,0);}
        else{r->claimed=1;earn_gold(g,12);note(g,DG_EVT_TREASURE,nx,ny,12);}
    }
    else if(t=='+'&&!r->claimed){r->claimed=1;g->hp=(uint8_t)min_int(g->max_hp,g->hp+4);note(g,DG_EVT_HEAL,nx,ny,4);}
    else if(t=='!'&&!r->claimed){r->claimed=1;g->pending_relic=1;note(g,DG_EVT_SHRINE,nx,ny,0);}
    else if(t=='$'&&!r->claimed){dg_interact(g);}
    else if(t=='L'){note(g,DG_EVT_BLOCKED,nx,ny,0);}
    else if(t=='B'||t=='E'||t=='Z'){
        /* Historical objectives are persistent through the room tile map. */
        r->tiles[ny][nx]='.';
        if(t=='B'){if(g->mana<5)g->mana++;note(g,DG_EVT_HF_BELL,nx,ny,0);}
        else if(t=='E'){earn_gold(g,8);note(g,DG_EVT_HF_EVIDENCE,nx,ny,8);}
        else {if(g->guard<3)g->guard++;note(g,DG_EVT_HF_BREACH,nx,ny,0);}
    }
    else if(t=='A'){
        r->tiles[ny][nx]='.';
        note(g,DG_EVT_HOUSE_ARCHIVE,nx,ny,(int)dg_world_house(g));
    }
    else if(t=='H'){
        unsigned effect=dg_world_hazard(g,dg_world_house(g));
        /* A ward discharges after use, and can never soft-lock a route. */
        r->tiles[ny][nx]='.';
        note(g,DG_EVT_HOUSE_HAZARD,nx,ny,(int)effect);
    }
    return spend_turn(g);
}
/* Lantern Nova: a defensive area skill for the 240x320 touch interface.
 * Costs two mana and one combat turn, hits all nearby foes for one damage,
 * cancels telegraphs and staggers survivors.  No dynamic memory or timers. */
bool dg_lantern_nova(dg_game *g){
    if(!g||!g->alive||g->pending_relic||g->current>=g->count||g->mana<2)return false;
    dg_room *r=&g->rooms[g->current];
    g->mana-=2;
    int affected=0;
    for(int i=0;i<DG_FOES;i++){
        dg_foe *e=&r->foes[i];
        if(!e->active)continue;
        int distance=abs_int((int)e->x-g->x)+abs_int((int)e->y-g->y);
        if(distance>2)continue;
        affected++;
        e->windup=0;
        e->hp--;
        if(!e->hp)kill_enemy(g,r,e);
        else e->stunned=1;
    }
    boss_health_phase(g,r);
    note(g,DG_EVT_SKILL,g->x,g->y,affected);
    return spend_turn(g);
}

/* Flame Step: a deliberate, readable two-tile dodge.  Unlike ordinary
 * movement it never crosses rooms, opens loot or passes through creatures;
 * the enemy party then takes exactly one turn. */
bool dg_flame_step(dg_game *g,int direction){
    if(!g || !g->alive || g->pending_relic || g->current>=g->count ||
       direction<0 || direction>3 || g->mana<1)return false;
    const dg_room *r=&g->rooms[g->current];
    int nx=g->x,ny=g->y;
    for(int step=0;step<2;step++){
        nx+=dir_dx[direction];ny+=dir_dy[direction];
        if(blocked(r,nx,ny) || foe_at(r,nx,ny)>=0 ||
           r->tiles[ny][nx]=='D'||r->tiles[ny][nx]=='>' ||
           (r->kind==DG_PUZZLE && !r->puzzle_solved &&
            nx==r->puzzle_x && ny==r->puzzle_y))return false;
        if(r->tiles[ny][nx]=='?' || r->tiles[ny][nx]=='+' ||
           r->tiles[ny][nx]=='!' || r->tiles[ny][nx]=='$')return false;
    }
    g->mana--;
    g->x=(uint8_t)nx;g->y=(uint8_t)ny;
    note(g,DG_EVT_FLAME_STEP,nx,ny,2);
    return spend_turn(g);
}

bool dg_guard(dg_game *g){if(!g||!g->alive||g->pending_relic||g->current>=g->count||!g->focus)return false;g->focus--;g->guard=1;note(g,DG_EVT_GUARD,g->x,g->y,0);return spend_turn(g);}
bool dg_potion(dg_game *g){if(!g||!g->alive||g->pending_relic||g->current>=g->count||!g->potions||g->hp==g->max_hp)return false;g->potions--;g->hp=(uint8_t)min_int(g->max_hp,g->hp+4);note(g,DG_EVT_HEAL,g->x,g->y,4);return spend_turn(g);}
bool dg_reset_puzzle(dg_game *g){
    if(!g||!g->alive||g->current>=g->count)return false;
    dg_room *r=&g->rooms[g->current];
    if(r->kind!=DG_PUZZLE||r->puzzle_solved)return false;
    r->puzzle_x=r->puzzle_start_x;r->puzzle_y=r->puzzle_start_y;
    r->puzzle_moves=0;note(g,DG_EVT_PUZZLE,r->puzzle_x,r->puzzle_y,0);
    return true;
}
bool dg_cycle_weapon(dg_game *g){
    if(!g||!g->alive||g->pending_relic)return false;
    for(int i=1;i<6;i++){
        uint8_t w=(uint8_t)((g->weapon+i)%6);
        if(g->weapon_unlocked&(1u<<w)){g->weapon=w;note(g,DG_EVT_WEAPON,g->x,g->y,w);return true;}
    }return false;
}
bool dg_interact(dg_game *g){
    if(!g||!g->alive||g->pending_relic)return false;
    dg_room *r=&g->rooms[g->current];
    if(r->kind==DG_SHRINE&&!r->claimed){r->claimed=1;g->pending_relic=1;g->event=DG_EVT_SHRINE;return true;}
    if(r->kind==DG_TREASURE && !r->claimed && dg_is_fate_vault(g) &&
       r->tiles[g->y][g->x]=='?'){
        g->pending_relic=3;
        note(g,DG_EVT_FATE_VAULT,g->x,g->y,0);
        return true;
    }
    if(r->kind==DG_MERCHANT&&!r->claimed&&r->tiles[g->y][g->x]=='$'){
        /* Opening the forge does not charge the player.  No auto-purchases:
         * the two offers (or a no-cost departure) are explicit choices. */
        g->pending_relic=2;
        note(g,DG_EVT_BUY,g->x,g->y,0);
        return true;
    }
    return false;
}
/* Choice 0: safe gold. Choice 1: break the seal and fight for riches.
 * Choice 2: leave an unopened vault; re-entering the tile reopens it. */
bool dg_choose_vault(dg_game *g,int choice){
    if(!g || !g->alive || g->pending_relic!=3 ||
       !dg_is_fate_vault(g) || choice<0 || choice>2)return false;
    dg_room *r=&g->rooms[g->current];
    if(r->claimed || r->tiles[g->y][g->x]!='?')return false;
    if(choice==2){
        g->pending_relic=0;
        g->event=DG_EVT_NONE;
        return true;
    }
    r->claimed=1;
    g->pending_relic=0;
    if(choice==0){
        earn_gold(g,12);
        note(g,DG_EVT_TREASURE,g->x,g->y,12);
        return true;
    }
    /* Summon visible, initially staggered guardians away from the altar.
     * No RNG, collision, or spawning directly on the player. */
    const uint8_t sx[4]={2,6,2,6}, sy[4]={2,2,5,5};
    int summoned=0;
    for(int j=0;j<4 && summoned<2;j++){
        if(blocked(r,sx[j],sy[j]) || (g->x==sx[j]&&g->y==sy[j]) ||
           foe_at(r,sx[j],sy[j])>=0)continue;
        dg_foe *e=&r->foes[summoned];
        *e=(dg_foe){.x=sx[j],.y=sy[j],.tx=sx[j],.ty=sy[j],
            .kind=(uint8_t)(summoned==0?DG_BEETLE:DG_GHOST),
            .hp=4,.max_hp=4,.stunned=1,.active=1};
        summoned++;
    }
    /* If the map can't provide a safe ambush, pay without an unfair fight. */
    earn_gold(g,summoned?28u:12u);
    if(summoned){r->foe_count=(uint8_t)summoned;r->cleared=0;}
    note(g,DG_EVT_FATE_VAULT,g->x,g->y,summoned?28:12);
    return true;
}

uint8_t dg_merchant_weapon(const dg_game *g){
    if(!g)return DG_NONE;
    for(int i=1;i<6;i++){
        uint8_t w=(uint8_t)((g->depth+g->current+(uint32_t)i)%6u);
        if(!(g->weapon_unlocked&(1u<<w)))return w;
    }
    return DG_NONE;
}
bool dg_choose_merchant(dg_game *g,int index){
    if(!g||!g->alive||g->pending_relic!=2||g->current>=g->count||
       g->rooms[g->current].kind!=DG_MERCHANT||index<0||index>2)return false;
    if(index==2){
        /* Walk away, or return after finding more gold. */
        g->pending_relic=0;g->event=DG_EVT_NONE;return true;
    }
    uint8_t w=dg_merchant_weapon(g);
    if(index==0 && w!=DG_NONE){
        if(g->gold<16)return false;
        g->gold-=16;
        g->weapon_unlocked|=(uint8_t)(1u<<w);
        g->weapon=w;
        note(g,DG_EVT_WEAPON,g->x,g->y,w);
    }else if(index==0){
        /* Endgame merchant remains useful when every weapon is unlocked. */
        if(g->gold<12||(g->hp==g->max_hp&&g->mana==5))return false;
        g->gold-=12;
        g->hp=(uint8_t)min_int(g->max_hp,g->hp+4);
        g->mana=5;
        note(g,DG_EVT_HEAL,g->x,g->y,4);
    }else{
        if(g->gold<10||g->potions>=5)return false;
        g->gold-=10;
        g->potions++;
        note(g,DG_EVT_BUY,g->x,g->y,1);
    }
    g->rooms[g->current].claimed=1;
    g->pending_relic=0;
    return true;
}
uint8_t dg_relic_offer(const dg_game *g,int index){
    if(!g||index<0||index>1)return 0;
    unsigned base=(unsigned)((g->depth+g->current+g->seed)%8u);
    int found=0;
    for(unsigned k=0;k<8;k++){
        uint8_t bit=(uint8_t)(1u<<((base+k)%8u));
        if(g->relics&bit)continue;
        if(found++==index)return bit;
    }
    return 0;
}
const char *dg_relic_name(uint8_t relic){
    switch(relic){case DG_RELIC_BLOOD:return "Blood Idol";case DG_RELIC_MIRROR:return "Mirror Fang";
    case DG_RELIC_EMBER:return "Ember Torch";case DG_RELIC_GHOST:return "Ghost Bell";
    case DG_RELIC_FROST:return "Frost Heart";case DG_RELIC_THORN:return "Thorn Crown";
    case DG_RELIC_MOON:return "Moon Coin";case DG_RELIC_STONE:return "Stone Sigil";default:return "Empty";}
}
const char *dg_relic_detail(uint8_t relic){
    switch(relic){case DG_RELIC_BLOOD:return "Heal on kill; -1 max HP";case DG_RELIC_MIRROR:return "Counter after blocking";
    case DG_RELIC_EMBER:return "Periodic bonus damage";case DG_RELIC_GHOST:return "Block fatal blow / floor";
    case DG_RELIC_FROST:return "Freeze foes on strikes";case DG_RELIC_THORN:return "+2 attack below half HP";
    case DG_RELIC_MOON:return "+50% enemy gold";case DG_RELIC_STONE:return "Reduce incoming damage";default:return "Nothing available";}
}
bool dg_choose_relic(dg_game *g,int index){
    if(!g||g->pending_relic!=1||index<0||index>2)return false;
    uint8_t choice=dg_relic_offer(g,index);
    if(index==2){g->rooms[g->current].claimed=1;g->pending_relic=0;g->event=DG_EVT_NONE;return true;}
    int count=0;for(int i=0;i<8;i++)if(g->relics&(1u<<i))count++;
    if(count>=g->relic_slots||!choice)return false;
    if(choice==DG_RELIC_BLOOD){if(g->max_hp<=3)return false;g->max_hp--;if(g->hp>g->max_hp)g->hp=g->max_hp;}
    g->relics|=choice;if(choice==DG_RELIC_GHOST)g->ghost_charge=1;g->rooms[g->current].claimed=1;g->pending_relic=0;g->event=DG_EVT_RELIC;return true;
}
/* State validation deliberately uses bounded indices before any access.
 * This is not intended to accept arbitrary untrusted game data formats. */
bool dg_validate(const dg_game *g){
    if(!g||!g->seed||g->depth==0||g->count<4||g->count>DG_ROOMS||g->current>=g->count||g->exit_room>=g->count||g->x>=DG_W||g->y>=DG_H||g->max_hp>DG_MAX_HP||!g->max_hp||g->hp>g->max_hp||(g->alive&&g->hp==0)||g->weapon>DG_LANTERN||!(g->weapon_unlocked&(1u<<g->weapon))||g->alive>1||g->pending_relic>3||g->mana>5||g->ghost_charge>1)return false;
    /* A save may have a valid checksum yet encode a player inside a wall.
     * Reject it before loading a permanently trapped run. */
    if(blocked(&g->rooms[g->current],g->x,g->y))return false;
    if(g->pending_relic==2 && (g->rooms[g->current].kind!=DG_MERCHANT ||
       g->rooms[g->current].claimed || g->rooms[g->current].tiles[g->y][g->x]!='$'))return false;
    if(g->pending_relic==3 &&
       (g->rooms[g->current].kind!=DG_TREASURE ||
        g->rooms[g->current].claimed || !dg_is_fate_vault(g) ||
        g->rooms[g->current].tiles[g->y][g->x]!='?'))return false;
    int count=0;uint16_t occ=0;
    for(int y=0;y<DG_MAP_H;y++)for(int x=0;x<DG_MAP_W;x++){
        uint8_t id=g->room_positions[y][x];if(id==DG_NONE)continue;
        if(id>=g->count||g->rooms[id].gx!=x||g->rooms[id].gy!=y||occ&(1u<<id))return false;
        occ|=(uint16_t)(1u<<id);count++;
    }
    if(count!=g->count||g->rooms[0].kind!=DG_ENTRANCE)return false;
    for(int i=0;i<g->count;i++){
        const dg_room *r=&g->rooms[i];if(r->kind>DG_ENTRANCE||r->gx>=DG_MAP_W||r->gy>=DG_MAP_H||r->foe_count>DG_FOES||r->boss_phase>3||r->boss_variant>2)return false;
        for(int d=0;d<4;d++)if(r->exits&mask[d]){
            int k=room_at(g,r->gx+dir_dx[d],r->gy+dir_dy[d]);if(k<0||!(g->rooms[k].exits&reverse[d]))return false;
        }
        if(r->kind==DG_PUZZLE){
            if(r->puzzle_x<1||r->puzzle_x>=DG_W-1||r->puzzle_y<1||r->puzzle_y>=DG_H-1||
                r->puzzle_goal_x<1||r->puzzle_goal_x>=DG_W-1||r->puzzle_goal_y<1||r->puzzle_goal_y>=DG_H-1||
                r->tiles[r->puzzle_goal_y][r->puzzle_goal_x]!='*'||blocked(r,r->puzzle_x,r->puzzle_y)||
                (r->puzzle_solved&&(r->puzzle_x!=r->puzzle_goal_x||r->puzzle_y!=r->puzzle_goal_y||!r->claimed)))return false;
        }
        for(int j=0;j<DG_FOES;j++){const dg_foe *e=&r->foes[j];if(e->active && (e->x>=DG_W||e->y>=DG_H||e->kind>DG_MIMIC||e->tx>=DG_W||e->ty>=DG_H||!e->hp||r->tiles[e->y][e->x]=='O'||r->tiles[e->y][e->x]=='#'||!dg_map_reachable(r,4,4,e->x,e->y)))return false;}
        if(!dg_map_reachable(r,4,4,7,5))return false;
    }
    /* Graph flood from entrance: generator must never strand a room. */
    uint16_t seen=1u;int q[DG_ROOMS],h=0,t=0;q[t++]=0;
    while(h<t){int i=q[h++];const dg_room *r=&g->rooms[i];for(int d=0;d<4;d++)if(r->exits&mask[d]){
        int k=room_at(g,r->gx+dir_dx[d],r->gy+dir_dy[d]);if(k>=0&&!(seen&(1u<<k))){seen|=(uint16_t)(1u<<k);q[t++]=k;}
    }}
    return seen==((1u<<g->count)-1u);
}
/* Version 1 saves remain readable. Layout was native-ABI; migration only
 * supports identical-ABI builds, and old saves are left intact until the new
 * checkpoint is successfully written. The v2 container still needs a future
 * portable explicit-endian format for cross-architecture portability. */
typedef struct {uint8_t x,y,kind,hp,max_hp,windup,tx,ty,active;} old_foe;
typedef struct {uint8_t gx,gy,kind,exits,visited,cleared,claimed,foe_count;
    uint8_t tiles[DG_H][DG_W];old_foe foes[DG_FOES];} old_room;
typedef struct {uint32_t seed,depth,floor_seed,turns,total_kills,total_gold,gold;
    uint8_t count,current,exit_room,x,y;
    uint8_t hp,max_hp,attack,potions,guard,focus,weapon,relics,relic_slots,boss_count;
    uint8_t room_positions[DG_MAP_H][DG_MAP_W];
    uint8_t pending_relic,room_choice,alive;
    dg_event event;uint8_t event_x,event_y,event_amount;
    old_room rooms[DG_ROOMS];} old_game;
typedef struct {char magic[4];uint32_t version,bytes,crc;} save_head;
static uint32_t checksum(const unsigned char *data,size_t n){uint32_t c=0xffffffffu;for(size_t i=0;i<n;i++){c^=data[i];for(int b=0;b<8;b++)c=(c>>1)^((0u-(c&1u))&0xedb88320u);}return ~c;}
static bool migrate_v1(dg_game *g,const old_game *o){
    if(!o->seed||!o->depth||o->depth==UINT32_MAX||o->count<8||o->count>12||o->current>=o->count||o->exit_room>=o->count||o->x>=DG_W||o->y>=DG_H||!o->max_hp||o->max_hp>DG_MAX_HP||o->hp>o->max_hp||o->weapon>DG_LANTERN)return false;
    dg_new(g,o->seed);g->depth=o->depth;make_floor(g);
    if(g->count!=o->count||g->exit_room!=o->exit_room)return false;
    for(int i=0;i<o->count;i++){
        const old_room *a=&o->rooms[i];dg_room *b=&g->rooms[i];
        if(a->gx!=b->gx||a->gy!=b->gy||a->kind!=b->kind||a->exits!=b->exits)return false;
        b->visited=a->visited;b->claimed=a->claimed;b->cleared=a->cleared;
        if(b->kind==DG_PUZZLE && b->claimed){b->puzzle_solved=1;b->puzzle_x=b->puzzle_goal_x;b->puzzle_y=b->puzzle_goal_y;}
        for(int j=0;j<DG_FOES;j++){
            const old_foe *e=&a->foes[j];dg_foe *f=&b->foes[j];
            if(!e->active){f->active=0;continue;}
            if(e->kind>DG_MIMIC||e->x>=DG_W||e->y>=DG_H||!e->hp||blocked(b,e->x,e->y))return false;
            f->x=e->x;f->y=e->y;f->tx=e->tx;f->ty=e->ty;f->kind=e->kind;
            f->hp=e->hp;f->max_hp=e->max_hp;f->windup=e->windup;f->active=1;
        }
    }
    g->current=o->current;g->x=o->x;g->y=o->y;g->turns=o->turns;g->total_kills=o->total_kills;
    g->total_gold=o->total_gold;g->gold=o->gold;g->hp=o->hp;g->max_hp=o->max_hp;
    g->attack=o->attack;g->potions=o->potions;g->guard=o->guard;g->focus=o->focus;
    g->weapon=o->weapon;g->weapon_unlocked=(uint8_t)((1u<<DG_SWORD)|(1u<<g->weapon));
    g->relics=o->relics;g->relic_slots=o->relic_slots;g->boss_count=o->boss_count;
    g->ghost_charge=(g->relics&DG_RELIC_GHOST)?1u:0u;
    g->pending_relic=o->pending_relic;g->room_choice=o->room_choice;g->alive=o->alive;
    g->event=o->event;g->event_x=o->event_x;g->event_y=o->event_y;g->event_amount=o->event_amount;
    return dg_validate(g);
}
static int file_read(dg_game *g,const char *path){
    FILE *f=fopen(path,"rb");if(!f)return 0;
    save_head h;int good=0;
    if(fread(&h,1,sizeof(h),f)==sizeof(h)&&memcmp(h.magic,"DGL1",4)==0){
        if(h.version==2&&h.bytes==sizeof(*g)){
            dg_game s;
            if(fread(&s,1,sizeof(s),f)==sizeof(s)&&fgetc(f)==EOF&&checksum((const unsigned char*)&s,sizeof(s))==h.crc&&dg_validate(&s)){*g=s;good=1;}
        }else if(h.version==1&&h.bytes==sizeof(old_game)){
            old_game s;
            if(fread(&s,1,sizeof(s),f)==sizeof(s)&&fgetc(f)==EOF&&checksum((const unsigned char*)&s,sizeof(s))==h.crc){
                dg_game upgraded;
                if(migrate_v1(&upgraded,&s)){*g=upgraded;good=1;}
            }
        }
    }fclose(f);return good;
}
bool dg_load(dg_game *g,const char *path){if(!g||!path)return false;if(file_read(g,path))return true;size_t n=strlen(path);if(n>PATH_MAX-8)return false;char bak[PATH_MAX];snprintf(bak,sizeof(bak),"%s.bak",path);return file_read(g,bak);}
bool dg_save(const dg_game *g,const char *path){
    if(!dg_validate(g)||!path)return false;
    size_t n=strlen(path);if(n>PATH_MAX-8)return false;
    char tmp[PATH_MAX],bak[PATH_MAX];snprintf(tmp,sizeof(tmp),"%s.tmp",path);snprintf(bak,sizeof(bak),"%s.bak",path);
    FILE *f=fopen(tmp,"wb");if(!f)return false;
    save_head h={{'D','G','L','1'},2,sizeof(*g),checksum((const unsigned char*)g,sizeof(*g))};
    bool ok=fwrite(&h,1,sizeof(h),f)==sizeof(h)&&fwrite(g,1,sizeof(*g),f)==sizeof(*g)&&fflush(f)==0&&fsync(fileno(f))==0;
    if(fclose(f)!=0)ok=false;
    if(!ok){remove(tmp);return false;}
    /* Do not destroy a verified backup when the primary is already corrupt. */
    dg_game check;int old_good=file_read(&check,path);
    if(old_good){remove(bak);if(rename(path,bak)!=0){remove(tmp);return false;}}
    if(rename(tmp,path)!=0){if(old_good)rename(bak,path);remove(tmp);return false;}
    return true;
}

/* R9: the ninth depth is the Testament's final chamber. Its verdict is
 * recorded in the existing exit tile and therefore in the native checkpoint.
 * No new RNG, struct members, or save ABI. */
unsigned dg_final_state(const dg_game *g){
 if(!g||g->depth!=9u||g->exit_room>=g->count)return 0;
 unsigned char t=g->rooms[g->exit_room].tiles[5][8];
 return t=='r'?1u:t=='u'?2u:t=='t'?3u:0u;
}
bool dg_final_choose(dg_game *g,unsigned ending){
 if(!g||!g->alive||g->depth!=9u||ending<1u||ending>3u||
    g->current!=g->exit_room||g->pending_relic||dg_final_state(g)||
    dg_enemies_alive(&g->rooms[g->current])||
    g->rooms[g->current].tiles[5][8]!='>')return false;
 int dx=(int)g->x-8,dy=(int)g->y-5;
 if(dx<0)dx=-dx;
 if(dy<0)dy=-dy;
 if(dx+dy>1)return false;
 g->rooms[g->current].tiles[5][8]=(uint8_t)(ending==1?'r':ending==2?'u':'t');
 g->event=DG_EVT_WIN;g->event_amount=(uint8_t)ending;
 return true;
}
