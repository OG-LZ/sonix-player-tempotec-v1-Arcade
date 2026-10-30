#include "dungeon_heavensfall.h"
unsigned dg_hf_chapter(const dg_game *g){
 if(!g)return 0;
 if(g->depth>=6 && g->depth<=8)return (unsigned)(g->depth-5);
 return 0;
}
const char *dg_hf_title(unsigned c){
 static const char *const names[]={"","I: The Falling Choir","II: The Thousand Quiet Hours","III: The Ashen Breach"};
 return c<=3?names[c]:"";
}
const char *dg_hf_evidence(unsigned c,unsigned r){
 static const char *const records[3][3]={
 {"ILSK DISPATCH: The sky has acquired a second horizon.",
  "KETHIS ORDER: Do not open the lanterns to the descending.",
  "SOLDIER'S NOTE: The angels called us by our old names."},
 {"SIBRELL LOG: All bells to ring until the enemy is deaf.",
  "SURGEON'S REPORT: The children cannot hear their mothers.",
  "UNSENT LETTER: I signed your name so you could hear me."},
 {"VHARN BREACH: The eastern vault has begun to remember.",
  "DUNHOLLOW ROLL: The dead exceed the listed population.",
  "LAST ORDER: Leave one bell unstruck. Let them speak."}
 };
 if(c<1||c>3)return "";
 return records[c-1][r%3u];
}
void dg_hf_train(dg_foe *e,unsigned c){
 if(!e||!e->active||c<1||c>3)return;
 /* Bounded war training; no new state and no random calls. */
 if((c==1&&e->kind==DG_GHOST)||(c==2&&e->kind==DG_CULTIST)||
    (c==3&&e->kind==DG_STONE)){
  if(e->hp<254&&e->max_hp<254){e->hp++;e->max_hp++;}
 }
}
void dg_hf_scar(dg_room *r,unsigned c){
 if(!r||!c||c>3||r->kind==DG_BOSS||r->kind==DG_PUZZLE)return;
 unsigned x=2u+c,y=2u+c%3u;
 if(r->tiles[y][x]!='.')return;
 for(unsigned i=0;i<DG_FOES;i++)if(r->foes[i].active&&r->foes[i].x==x&&r->foes[i].y==y)return;
 r->tiles[y][x]=':';
}

/* Chapter rooms are staged along the existing graph; one objective per
 * chapter in the entrance, and another in the exit. Tiles are consumed in
 * place so old saves and checkpoint recovery require no migration. */
void dg_hf_install(dg_game *g,dg_room *r,unsigned index){
 unsigned chapter=dg_hf_chapter(g);
 if(!chapter||r->kind==DG_PUZZLE||r->kind==DG_BOSS)return;
 if(index!=0 && index!=g->exit_room)return;
 unsigned x=index==0?3u:6u,y=index==0?2u:5u;
 /* Seek an empty, reachable cell rather than silently omitting an
  * objective when procedural furnishing occupies the preferred position. */
 unsigned found=0;
 for(unsigned k=0;k<36 && !found;k++){
  unsigned tx=1u+((x-1u+k)%8u),ty=1u+((y-1u+k/8u)%6u);
  if(r->tiles[ty][tx]!='.' && r->tiles[ty][tx]!=':')continue;
  if(!dg_map_reachable(r,4,4,(int)tx,(int)ty))continue;
  unsigned occupied=0;
  for(unsigned j=0;j<DG_FOES;j++)if(r->foes[j].active&&r->foes[j].x==tx&&r->foes[j].y==ty)occupied=1;
  if(occupied)continue;
  x=tx;y=ty;found=1;
 }
 if(!found)return;
 r->tiles[y][x]=chapter==2?'B':chapter==1?'E':'Z';
 if(chapter==3&&index==g->exit_room){
  for(unsigned yy=1;yy<DG_H-1;yy++){
   unsigned placed=0;
   for(unsigned xx=1;xx<DG_W-1;xx++){
    if(r->tiles[yy][xx]!='.'||!dg_map_reachable(r,4,4,xx,yy))continue;
    unsigned occupied=0;
    for(unsigned j=0;j<DG_FOES;j++)if(r->foes[j].active&&r->foes[j].x==xx&&r->foes[j].y==yy)occupied=1;
    if(!occupied){r->tiles[yy][xx]='L';placed=1;break;}
   }
   if(placed)break;
  }
 }
}
unsigned dg_hf_remaining(const dg_game *g){
 if(!g||!dg_hf_chapter(g))return 0;
 unsigned count=0;
 for(unsigned i=0;i<g->count;i++)for(unsigned y=1;y<DG_H-1;y++)for(unsigned x=1;x<DG_W-1;x++){
  unsigned char t=g->rooms[i].tiles[y][x];
  if(t=='B'||t=='E'||t=='Z'||t=='L')count++;
 }
 return count;
}
bool dg_hf_can_descend(const dg_game *g){
 if(!g || dg_hf_remaining(g)!=0)return false;
 if(dg_hf_last_bell_state(g)==3 && dg_enemies_alive(&g->rooms[g->exit_room])>0)return false;
 return true;
}
const char *dg_hf_objective(unsigned chapter){
 switch(chapter){
 case 1:return "Recover the Choir dispatches";
 case 2:return "Silence the Sibrell bell towers";
 case 3:return "Seal the Ashen Breach";
 default:return "Explore the forgotten Houses";
 }
}

/* R7: one deterministic final-bell encounter on the chapter III exit.
 * The choice is persistent and mutually exclusive; no new RNG or save fields.
 * The encounter's tactical cost is immediate: speaking awakens a Choir
 * defender; silence sacrifices mana and a measure of guard. */
unsigned dg_hf_last_bell_state(const dg_game *g){
 if(!g||g->depth!=8||g->exit_room>=g->count)return 0;
 const dg_room *r=&g->rooms[g->exit_room];
 for(unsigned y=1;y<DG_H-1;y++)for(unsigned x=1;x<DG_W-1;x++){
  unsigned char t=r->tiles[y][x];
  if(t=='L')return 1;
  if(t=='q')return 2;
  if(t=='w')return 3;
 }
 return 0;
}
bool dg_hf_last_bell_choose(dg_game *g,bool let_speak){
 if(!g||!g->alive||g->depth!=8||g->current!=g->exit_room)return false;
 dg_room *r=&g->rooms[g->current];
 if(dg_enemies_alive(r)>0)return false;
 for(unsigned y=1;y<DG_H-1;y++)for(unsigned x=1;x<DG_W-1;x++)if(r->tiles[y][x]=='L'){
  /* Choice must occur at the bell, not remotely from another tile. */
  int dx=(int)g->x-(int)x,dy=(int)g->y-(int)y;
  if(dx<0)dx=-dx;
  if(dy<0)dy=-dy;
  if(dx+dy>1)return false;
  if(let_speak){
   /* The Choir speaks: a surviving ghost is drawn to the testimony. */
   unsigned slot=DG_FOES;
   for(unsigned i=0;i<DG_FOES;i++)if(!r->foes[i].active){slot=i;break;}
   if(slot==DG_FOES)return false;
   dg_foe *f=&r->foes[slot];
   unsigned fx=1,fy=1,found=0;
   for(unsigned yy=1;yy<DG_H-1&&!found;yy++)for(unsigned xx=1;xx<DG_W-1&&!found;xx++){
    int dist=(int)xx-(int)g->x,dy2=(int)yy-(int)g->y;
    if(dist<0)dist=-dist;
    if(dy2<0)dy2=-dy2;
    if(dist+dy2<3||r->tiles[yy][xx]!='.'||!dg_map_reachable(r,g->x,g->y,xx,yy))continue;
    fx=xx;fy=yy;found=1;
   }
   if(!found)return false;
   *f=(dg_foe){0};f->active=1;f->kind=DG_GHOST;f->x=fx;f->y=fy;f->hp=8;f->max_hp=8;
   r->tiles[y][x]='w';g->event=DG_EVT_HF_CHOIR;
  }else{
   r->tiles[y][x]='q';
   if(g->mana>0)g->mana--;
   if(g->guard>0)g->guard--;
   g->event=DG_EVT_HF_SILENCE;
  }
  g->event_x=(uint8_t)x;g->event_y=(uint8_t)y;g->event_amount=0;
  return true;
 }
 return false;
}
const char *dg_hf_last_bell_testimony(unsigned state){
 if(state==2)return "SIBRELL FINAL ORDER: The last bell was silenced. No surviving voice was entered into the register.";
 if(state==3)return "CHOIR TESTIMONY: We did not fall from heaven. We returned from a history you burned.";
 return "THE LAST BELL: No account has yet survived its sounding.";
}
