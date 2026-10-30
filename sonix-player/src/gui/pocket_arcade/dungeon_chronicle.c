#define _POSIX_C_SOURCE 200809L
#include "dungeon_chronicle.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

void dg_chronicle_init(dg_chronicle *c) { if(c) memset(c,0,sizeof(*c)); }
bool dg_chronicle_record_last_bell(dg_chronicle *c,unsigned outcome){
 if(!c || (outcome!=2 && outcome!=3) || (c->last_bell_evidence && c->last_bell_evidence!=outcome))return false;
 if(c->last_bell_evidence==outcome)return false;
 c->last_bell_evidence=(uint8_t)outcome;return true;
}
uint16_t dg_chronicle_observe(dg_chronicle *c,const dg_game *g) {
 if(!c || !g) return 0;
 uint16_t new_bits=(uint16_t)(dg_testimony_available(g) & (uint16_t)~c->discovered);
 c->discovered |= new_bits;
 return new_bits;
}
bool dg_chronicle_knows(const dg_chronicle *c,unsigned id) {
 return c && id<DG_TESTIMONY_COUNT && (c->discovered & (1u<<id))!=0;
}
bool dg_chronicle_decide(dg_chronicle *c,unsigned obligation,bool accept) {
 if(!c || obligation>=2) return false;
 unsigned shift=obligation*2u;
 if((c->face_decisions >> shift)&3u) return false;
 c->face_decisions |= (uint8_t)((accept?1u:2u)<<shift);
 if(accept) c->burden_on_player |= (uint8_t)(1u<<obligation);
 else c->burden_on_forge |= (uint8_t)(1u<<obligation);
 return true;
}
const char *dg_chronicle_forge_dialogue(const dg_chronicle *c) {
 if(!c) return "The forge is cold.";
 if(c->burden_on_forge & 2u) return "They call me oathbreaker. I never swore an oath.";
 if(c->burden_on_forge & 1u) return "People look at me as though I have done something terrible.";
 return "I'll keep the fire lit until you return.";
}
const char *dg_chronicle_candlewake_dialogue(const dg_chronicle *c,const dg_game *g) {
 if(!c || !g) return "The chapel has no record of you.";
 if(c->burden_on_forge) return "Melira: Someone here has inherited a debt they do not understand.";
 if(g->boss_count>=3) return "Melira: There are rooms here that I cannot remember building.";
 if(g->boss_count>=1 || g->depth>=3) return "Melira: The second cup was always here. Wasn't it?";
 return "Melira: You're welcome here. I'll put the kettle on.";
}
/* Fixed 20-byte little-endian disk format. Bytes 0..15 payload, 16..19 FNV-1a checksum. */
static uint32_t hash_bytes(const unsigned char *p,size_t n) {
 uint32_t h=2166136261u;
 for(size_t i=0;i<n;i++) {h^=p[i];h*=16777619u;}
 return h;
}
static void put32(unsigned char *p,uint32_t v) {
 for(unsigned i=0;i<4;i++) p[i]=(unsigned char)(v>>(i*8));
}
static uint32_t get32(const unsigned char *p) {
 return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);
}
static void encode(const dg_chronicle *c,unsigned char b[20]) {
 memset(b,0,20); put32(b,DG_CHRONICLE_MAGIC); b[4]=DG_CHRONICLE_VERSION;
 b[6]=(unsigned char)c->discovered;b[7]=(unsigned char)(c->discovered>>8);
 b[8]=c->face_decisions;b[9]=c->burden_on_player;b[10]=c->burden_on_forge;
 b[11]=c->candlewake_visits;b[12]=c->last_bell_evidence;b[13]=c->final_ending;put32(b+16,hash_bytes(b,16));
}
static bool decode(dg_chronicle *c,const unsigned char b[20]) {
 if(get32(b)!=DG_CHRONICLE_MAGIC || b[4]!=DG_CHRONICLE_VERSION || get32(b+16)!=hash_bytes(b,16)) return false;
 if(b[5] || (b[12]!=0 && b[12]!=2 && b[12]!=3) || b[13]>3 || b[14] || b[15] || (b[8]&0xf0u)) return false;
 dg_chronicle t;dg_chronicle_init(&t);
 t.discovered=(uint16_t)((uint16_t)b[6]|((uint16_t)b[7]<<8));
 if(t.discovered & (uint16_t)~((1u<<DG_TESTIMONY_COUNT)-1u)) return false;
 t.face_decisions=b[8];t.burden_on_player=b[9];t.burden_on_forge=b[10];t.candlewake_visits=b[11];t.last_bell_evidence=b[12];t.final_ending=b[13];
 if((t.burden_on_player|t.burden_on_forge)&~3u) return false;
 for(unsigned i=0;i<2;i++) {
  unsigned decision=(t.face_decisions>>(2*i))&3u;
  if(decision==3 || ((t.burden_on_player>>i)&1u)!=(decision==1) || ((t.burden_on_forge>>i)&1u)!=(decision==2)) return false;
 }
 *c=t;return true;
}
/* Atomic sidecar replacement with last-known-good backup. The backup is
 * preserved if the current file is corrupt; never overwrite a good backup
 * with invalid bytes. A failed write leaves the original file untouched. */
static bool read_file(dg_chronicle *c,const char *path) {
 unsigned char b[20];FILE *f=fopen(path,"rb");if(!f)return false;
 bool ok=fread(b,1,sizeof(b),f)==sizeof(b) && fgetc(f)==EOF;
 if(fclose(f)!=0)ok=false;
 return ok && decode(c,b);
}
bool dg_chronicle_save(const dg_chronicle *c,const char *path) {
 if(!c || !path || !*path)return false;
 size_t n=strlen(path);if(n>1000)return false;
 char temp[1024],bak[1024];snprintf(temp,sizeof(temp),"%s.tmp",path);
 snprintf(bak,sizeof(bak),"%s.bak",path);
 unsigned char b[20];encode(c,b);
 FILE *f=fopen(temp,"wb");if(!f)return false;
 bool ok=fwrite(b,1,sizeof(b),f)==sizeof(b) && fflush(f)==0;
 if(ok && fsync(fileno(f))!=0)ok=false;
 if(fclose(f)!=0)ok=false;
 if(!ok){remove(temp);return false;}
 dg_chronicle previous;
 if(read_file(&previous,path)){
  /* Keep a valid recovery copy; failure to create it aborts replacement. */
  unsigned char old[20];encode(&previous,old);
  FILE *bf=fopen(bak,"wb");if(!bf){remove(temp);return false;}
  bool bok=fwrite(old,1,sizeof(old),bf)==sizeof(old) && fflush(bf)==0;
  if(bok && fsync(fileno(bf))!=0)bok=false;
  if(fclose(bf)!=0)bok=false;
  if(!bok){remove(temp);return false;}
 }
 if(rename(temp,path)!=0){remove(temp);return false;}
 return true;
}
bool dg_chronicle_load(dg_chronicle *c,const char *path) {
 if(!c || !path || !*path)return false;
 if(read_file(c,path))return true;
 size_t n=strlen(path);if(n>1000)return false;
 char bak[1024];snprintf(bak,sizeof(bak),"%s.bak",path);
 return read_file(c,bak);
}

bool dg_chronicle_record_ending(dg_chronicle *c,unsigned ending){
 if(!c||ending<1||ending>3||(c->final_ending&&c->final_ending!=ending))return false;
 if(c->final_ending==ending)return false;
 c->final_ending=(uint8_t)ending;return true;
}
