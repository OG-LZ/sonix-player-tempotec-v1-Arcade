#include "dungeon_heavensfall.h"
#include "dungeon_world_play.h"
static const char *const houses[8]={"Oressa","Vharn","Sibrell","Kethis","Corvane","Marroth","Ilsk","Dunhollow"};
unsigned dg_world_house(const dg_game *g){
 if(!g||g->current>=g->count)return 0;
 return dg_hash(g->floor_seed ^ (0x7c4a9e13u + (uint32_t)g->current*0x9e3779b9u))%8u;
}
const char *dg_world_house_name(unsigned h){return h<8?houses[h]:"Unrecorded";}
const char *dg_world_room_legend(const dg_game *g){
 unsigned chapter=dg_hf_chapter(g);
 if(chapter)return dg_hf_evidence(chapter,g->current);
 static const char *const legends[]={
 "Oressa: the census continues after its province vanished.",
 "Vharn: these walls were built to hold a forgotten history.",
 "Sibrell: the bell towers were silenced after Heavensfall.",
 "Kethis: lanterns once stored the names of prisoners.",
 "Corvane: the healing wards passed suffering onward.",
 "Marroth: the dead were indexed before they were buried.",
 "Ilsk: the maps mark a road that has never existed.",
 "Dunhollow: the sextons carried out the erasures."};
 return legends[dg_world_house(g)];
}
const char *dg_world_enemy_variant(const dg_game *g,unsigned kind){
 unsigned h=dg_world_house(g);
 if(kind==DG_SKEL)return h==0?"Tallyman":h==5?"Nameroll Remnant":"Unburied";
 if(kind==DG_CULTIST)return h==4?"Antiphon":h==2?"Bell Penitent":"Ash Devotee";
 if(kind==DG_GHOST)return h==3?"Lantern Prisoner":h==5?"Unwritten Shade":"Weeper";
 if(kind==DG_STONE)return h==1?"Vharnic Mason":"Vault Sentinel";
 if(kind==DG_MIMIC)return h==7?"Erasure Chest":"False Reliquary";
 if(kind==DG_BAT)return "Vault Moth";
 if(kind==DG_SLIME)return "Memory Dregs";
 if(kind==DG_BEETLE)return "Grave Scarab";
 return "Unknown";
}
const char *dg_world_relic_provenance(unsigned bit){
 switch(bit){
 case DG_RELIC_BLOOD:return "Corvane's mercy: every wound was paid by another.";
 case DG_RELIC_MIRROR:return "Marroth indexed the face; its owner was omitted.";
 case DG_RELIC_EMBER:return "A Heavensfall torch, still warm after the Sixth Age.";
 case DG_RELIC_GHOST:return "Sibrell rang it for six weeks. A generation went deaf.";
 case DG_RELIC_FROST:return "Kethis preserved the prisoner's last winter.";
 case DG_RELIC_THORN:return "A penitent crown, grown from a transferred oath.";
 case DG_RELIC_MOON:return "Oressa counted forty-one citizens who never existed.";
 case DG_RELIC_STONE:return "Vharn's cornerstone remembers a different floorplan.";
 default:return "No surviving provenance.";
 }
}
const char *dg_world_npc_knowledge(const dg_game *g,const dg_chronicle *c){
 if(!g||!c)return "Melira: There are gaps in every ledger.";
 if(c->burden_on_forge)return "FORGE: I am accused of a debt I cannot remember. Why?";
 if(((c->discovered & (1u<<9)) != 0))return "MELIRA: You have read what the Houses left out. Be careful whom you count.";
 if(((c->discovered & (1u<<4)) != 0))return "MELIRA: Those bells were not rung in celebration.";
 if(g->depth>=4)return "MELIRA: The vaults have more than one history. Some are still inhabited.";
 return "MELIRA: The old Houses each kept a different kind of record.";
}
void dg_world_mark_foe(dg_foe *foe,unsigned house){
 if(!foe||!foe->active)return;
 /* Real combat consequences, bounded to the preexisting serialized fields. */
 if(house==0 && foe->kind==DG_SKEL && foe->hp<254){foe->hp++;foe->max_hp++;}
 if(house==1 && foe->kind==DG_STONE && foe->hp<254){foe->hp++;foe->max_hp++;}
 if(house==4 && foe->kind==DG_CULTIST && foe->hp<254){foe->hp++;foe->max_hp++;}
}

/* Each House leaves a distinct functional trace. The two glyphs occupy only
 * otherwise empty interior tiles; never overwrite doors, puzzles, stairs or foes.
 * The archive is one-time collectible (tile becomes floor). */
void dg_world_furnish(dg_room *r,unsigned house){
 if(!r || house>=8 || r->kind==DG_PUZZLE || r->kind==DG_BOSS || r->kind==DG_ENTRANCE)return;
 const int ax=2+(int)(house%3),ay=2+(int)(house/3);
 const int hx=6+(int)(house%2),hy=2+(int)(house%3);
 if(r->tiles[ay][ax]=='.' || r->tiles[ay][ax]==':'){
  int occupied=0;for(int i=0;i<DG_FOES;i++)if(r->foes[i].active && r->foes[i].x==ax && r->foes[i].y==ay)occupied=1;
  if(!occupied)r->tiles[ay][ax]='A';
 }
 if(r->tiles[hy][hx]=='.' || r->tiles[hy][hx]==':'){
  int occupied=0;for(int i=0;i<DG_FOES;i++)if(r->foes[i].active && r->foes[i].x==hx && r->foes[i].y==hy)occupied=1;
  if(!occupied)r->tiles[hy][hx]='H';
 }
}
unsigned dg_world_archive_testimony(unsigned house){return house<8?house:0;}
const char *dg_world_hazard_name(unsigned h){
 static const char *const names[]={"Census seal","Shifting masonry","Bell pulse","Soul lantern","Pain conduit","Name-ward","Lost path","Erasure ash"};
 return h<8?names[h]:"Unknown ward";
}
unsigned dg_world_hazard(dg_game *g,unsigned h){
 if(!g||h>=8)return 0;
 /* No random draws. These effects alter real resources; cap all increases. */
 switch(h){
 case 0: if(g->gold){g->gold--;return 1;}break; /* census tax */
 case 1: if(g->guard<3){g->guard++;return 2;}break; /* shelter */
 case 2: if(g->mana){g->mana--;return 3;}break; /* bell disruption */
 case 3: if(g->mana<5){g->mana++;return 4;}break; /* stored light */
 case 4: if(g->hp>1){g->hp--;return 5;}break; /* pain conduit */
 case 5: if(g->focus<3){g->focus++;return 6;}break; /* recovered name */
 case 6: if(g->gold<UINT32_MAX-2){g->gold+=2;return 7;}break; /* hidden cache */
 case 7: if(g->guard){g->guard--;return 8;}break; /* erasure */
 }
 return 0;
}
