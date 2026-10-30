#include "dungeon_world_play.h"
#include "dungeon_campaign_presentation.h"
/* Relics of the Lantern: dedicated 240x320 LVGL cabinet. GPL-3.0-or-later.
 * Only GUI-thread work; no mixer, ALSA, codec, network, or music transport calls.
 * Real Sonix simulator and V1 power/audio coexistence remain to be validated. */
#include "dungeon_deluxe_ui.h"
#include "dungeon_deluxe.h"
#include "dungeon_chronicle.h"
#include "dungeon_heavensfall.h"
#include "dungeon_world_r1.h"
#include "dungeon_lore.h"
#include "dungeon_deluxe_art.h"
#include "arcade_fx.h"
#include "pocket_arcade.h"
#include "lvgl/lvgl.h"
#include "src/gui/fonts/fonts.h"
#include "src/gui/shell/switcher.h"
#include "src/gui/nowplaying/player.h"
#include "src/gui/shell/topbar.h"
#include "src/system/device/power.h"
#include "src/system/core/config.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static gui_config_t *cfg;
static dg_game game;
static lv_obj_t *screen,*arena,*fx,*tiles[DG_W*DG_H];
static lv_obj_t *actors[DG_FOES+2], *warnings[DG_H*DG_W];
static lv_obj_t *hud_top, *hud_bottom, *hud_gold, *hud_hp_track, *hud_hp_fill, *event_label;
static int last_hp_width=-1;
static uint8_t last_hp_tone=255;
static uint32_t last_top_ink=0xffffffffu, last_event_ink=0xffffffffu;
static lv_obj_t *dialog, *dialog_title, *dialog_text_view, *dialog_body;
static lv_obj_t *dialog_yes, *dialog_no, *dialog_exit;
static lv_obj_t *control[8];
static lv_timer_t *animation_timer;
/* R18: one reusable, nonblocking chapter-title card. No new save state. */
static lv_obj_t *chapter_card,*chapter_card_text,*chapter_card_subtitle;
static unsigned chapter_card_ticks;
static unsigned animation_frame;
static unsigned hero_pose,pose_ticks; /* 0 idle, 1 strike, 2 guard, 3 hurt, 4 victory */
static bool ready,menu_open,saved_known,save_dirty;
static bool lore_mode;
static dg_chronicle chronicle;
static char chronicle_path[700];
static bool chronicle_dirty;
static unsigned chronicle_page; /* 0..11 original, 12..27 World Bible, 28 Candlewake, 29 mirror */
static bool chronicle_mode;
static bool mirror_confirm;
static unsigned epilogue_page;
static unsigned presented_depth;
static unsigned lore_page;
static bool flame_step_held[4];
static int last_room=-1;
static const lv_image_dsc_t *last_tile_image[DG_W*DG_H];
static const lv_image_dsc_t *last_actor_image[DG_FOES+2];
static int16_t actor_x_cache[DG_FOES+2],actor_y_cache[DG_FOES+2];
static void actor_source(int i,const lv_image_dsc_t *image){
    if(last_actor_image[i]!=image){lv_image_set_src(actors[i],image);last_actor_image[i]=image;}
}
static void actor_pos(int i,int x,int y){
    if(actor_x_cache[i]!=x || actor_y_cache[i]!=y){
        lv_obj_set_pos(actors[i],x,y);actor_x_cache[i]=(int16_t)x;actor_y_cache[i]=(int16_t)y;
    }
}
static void actor_show(int i,bool shown){
    if(lv_obj_is_hidden(actors[i])==shown)lv_obj_set_hidden(actors[i],!shown);
}
static char save_location[640];
static uint32_t biome(void){return ((game.depth-1u)/2u)%6u;}
static lv_obj_t *label(lv_obj_t *parent,const char *text,int x,int y,int width,int font_size){
    lv_obj_t *o=lv_label_create(parent);lv_label_set_text(o,text);
    lv_obj_set_pos(o,x,y);lv_obj_set_width(o,width);
    lv_obj_set_style_text_color(o,lv_color_hex(0xF4E3C5),0);
    lv_obj_set_style_text_font(o,font_size==18?&font_ui_18:&font_ui_14,0);
    lv_label_set_long_mode(o,LV_LABEL_LONG_DOT);
    return o;
}
static void set_text(lv_obj_t *o,const char *text){const char *s=lv_label_get_text(o);if(!s||strcmp(s,text)!=0)lv_label_set_text(o,text);}
static lv_obj_t *make_button(lv_obj_t *parent,const char *name,int x,int y,int width,int height,lv_event_cb_t cb,int id){
    lv_obj_t *o=lv_btn_create(parent);lv_obj_set_pos(o,x,y);lv_obj_set_size(o,width,height);
    lv_obj_set_style_bg_color(o,lv_color_hex(0x354353),0);lv_obj_set_style_bg_opa(o,LV_OPA_COVER,0);
    lv_obj_set_style_border_color(o,lv_color_hex(0xA88C64),0);lv_obj_set_style_border_width(o,1,0);
    lv_obj_set_style_radius(o,5,0);lv_obj_set_style_pad_all(o,0,0);
    /* Visible tactile response even when the action cannot be taken. */
    lv_obj_set_style_bg_color(o,lv_color_hex(0x5D7188),LV_STATE_PRESSED);
    lv_obj_set_style_border_color(o,lv_color_hex(0xF2D39C),LV_STATE_PRESSED);
    /* MENU short-click avoids triggering the normal action after a Chronicle long-press. */
    lv_obj_add_event_cb(o,cb,id==6?LV_EVENT_SHORT_CLICKED:LV_EVENT_CLICKED,
                        (void*)(intptr_t)id);
    lv_obj_t *t=label(o,name,0,0,width,14);lv_obj_set_style_text_align(t,LV_TEXT_ALIGN_CENTER,0);lv_obj_center(t);
    return o;
}
static bool save_file_path(void){
    const char *path=config_path();if(!path||!*path)return false;
    size_t len=strlen(path);if(len>=sizeof(save_location)-32)return false;
    const char *slash=strrchr(path,'/');size_t length=slash?(size_t)(slash-path+1):0;
    if(length>=sizeof(save_location)-32)return false;
    memcpy(save_location,path,length);save_location[length]=0;
    strcat(save_location,"pocket_dungeon_deluxe.sav");return true;
}
/* Coalesce checkpoints: an unchanged menu exit used to fsync the same state
 * again and again. Failed writes retain dirty state for a later retry. */
static void save_game(void){
    if(save_dirty && save_location[0] && dg_save(&game,save_location))save_dirty=false;
}
static void save_chronicle(void){
    if(chronicle_dirty && chronicle_path[0] && dg_chronicle_save(&chronicle,chronicle_path))
        chronicle_dirty=false;
}
static void chronicle_observe(void){
    if(game.event==DG_EVT_HOUSE_ARCHIVE){
        unsigned id=dg_world_archive_testimony(game.event_amount);
        if(id<DG_TESTIMONY_COUNT && !(chronicle.discovered&(1u<<id))){
            chronicle.discovered|=(uint16_t)(1u<<id);
            chronicle_dirty=true;
        }
    }
    if(dg_chronicle_observe(&chronicle,&game) || chronicle_dirty){
        chronicle_dirty=true;
        save_chronicle();
    }
}
static unsigned mirror_pending(void){
    for(unsigned i=0;i<2;i++)if(((chronicle.face_decisions>>(2*i))&3u)==0)return i;
    return 2;
}
static void show_chronicle(void){
    chronicle_mode=true; lore_mode=false; menu_open=true;
    if(animation_timer)lv_timer_pause(animation_timer);
    lv_obj_set_hidden(dialog,false);
    if(chronicle_page<12){
        const dg_testimony *t=dg_testimony_at(chronicle_page);
        set_text(dialog_title,t?t->title:"UNKNOWN");
        set_text(dialog_body,t && dg_chronicle_knows(&chronicle,chronicle_page)?t->text:
                 "This testimony has not yet been recovered.");
    }else if(chronicle_page<12+DG_WORLD_PAGES){
        const dg_world_page *p=dg_world_page_at(chronicle_page-12);
        set_text(dialog_title,p?p->title:"UNKNOWN");
        set_text(dialog_body,p && dg_chronicle_knows(&chronicle,p->requires_testimony)?p->text:"This testimony has not yet been recovered.");
    }else if(chronicle_page==12+DG_WORLD_PAGES){
        set_text(dialog_title,"CANDLEWAKE");
        char message[350];
        snprintf(message,sizeof(message),"%s\n\nFORGE: %s",
                 dg_chronicle_candlewake_dialogue(&chronicle,&game),
                 dg_chronicle_forge_dialogue(&chronicle));
        if(chronicle.last_bell_evidence){
            size_t used=strlen(message);
            snprintf(message+used,sizeof(message)-used,"\n\n%s",dg_hf_last_bell_testimony(chronicle.last_bell_evidence));
        }
        { size_t used=strlen(message); snprintf(message+used,sizeof(message)-used,"\n\n%s",dg_world_npc_knowledge(&game,&chronicle)); }
        set_text(dialog_body,message);
    }else{
        unsigned pending=mirror_pending();
        set_text(dialog_title,"THE BORROWED FACE");
        if(pending==2){
            set_text(dialog_body,"The mirror is still. Both burdens have found a bearer. Your choices cannot be undone.");
        }else if(mirror_confirm){
            set_text(dialog_body,"REFUSE THIS FACE? Its unfinished debt will pass to the forge keeper. This cannot be undone. Confirm below.");
        }else if(pending==0){
            set_text(dialog_body,"THE NAMELESS: A murder without a recorded culprit. Carry its accusation yourself, or let the forge keeper inherit it.");
        }else{
            set_text(dialog_body,"THE OATHBEARER: A promise broken in another history. Carry the shame, or let the forge keeper be called oathbreaker.");
        }
    }
    lv_obj_scroll_to_y(dialog_text_view,0,LV_ANIM_OFF);
    set_text(lv_obj_get_child(dialog_yes,0),chronicle_page==13+DG_WORLD_PAGES?
             (mirror_confirm?"CANCEL":"ACCEPT BURDEN"):"PREVIOUS");
    set_text(lv_obj_get_child(dialog_no,0),chronicle_page==13+DG_WORLD_PAGES?
             (mirror_pending()==2?"NO DEBT":mirror_confirm?"CONFIRM REFUSAL":"REFUSE BURDEN"):"NEXT");
    set_text(lv_obj_get_child(dialog_exit,0),"CLOSE CHRONICLE");
}
static void update_actor_positions(void){
    const dg_room *r=&game.rooms[game.current];
    actor_pos(0,game.x*24+2,game.y*24+2);
    actor_source(0,hero_pose?dg_art_pose(hero_pose-1,animation_frame):dg_art_actor(0,animation_frame));
    actor_show(0,true);
    for(int i=0;i<DG_FOES;i++){
        const dg_foe *e=&r->foes[i];
        actor_show(i+1,e->active);
        if(!e->active)continue;
        if(r->kind==DG_BOSS&&i==0){
            actor_source(i+1,dg_art_boss(r->boss_variant,animation_frame));
            actor_pos(i+1,e->x*24-4,e->y*24-4);
        }else{
            actor_source(i+1,(dg_hf_chapter(&game) &&
                ((dg_world_house(&game)==0 && e->kind==DG_SKEL)||
                 (dg_world_house(&game)==1 && e->kind==DG_STONE)||
                 (dg_world_house(&game)==2 && e->kind==DG_CULTIST)||
                 (dg_hf_chapter(&game)==1 && e->kind==DG_GHOST)))?
                dg_art_heavensfall(e->kind,dg_world_house(&game)):
                dg_art_actor(1+e->kind,animation_frame));
            actor_pos(i+1,e->x*24+2,e->y*24+2);
        }
    }
    if(r->kind==DG_PUZZLE&&!r->puzzle_solved){
        actor_show(DG_FOES+1,true);
        actor_source(DG_FOES+1,dg_art_crate(animation_frame));
        actor_pos(DG_FOES+1,r->puzzle_x*24+2,r->puzzle_y*24+2);
    }else actor_show(DG_FOES+1,false);
}
static void update_warnings(void){
    for(int y=0;y<DG_H;y++)for(int x=0;x<DG_W;x++){
        bool hidden=!dg_danger_at(&game,x,y);
        if(lv_obj_is_hidden(warnings[y*DG_W+x])!=hidden)
            lv_obj_set_hidden(warnings[y*DG_W+x],hidden);
    }
}
static void redraw_room(void){
    const dg_room *r=&game.rooms[game.current];unsigned b=biome();
    bool fate_vault=dg_is_fate_vault(&game);
    for(int y=0;y<DG_H;y++)for(int x=0;x<DG_W;x++){
        lv_obj_t *t=tiles[y*DG_W+x];char v=(char)r->tiles[y][x];
        unsigned id=(v=='#')?2:(v=='O')?3:(v=='D')?4:(v=='>')?5:
                    (v=='!')?6:(v=='?')?7:(v=='+')?8:(v=='$')?9:(v==':')?10:(v=='t')?11:(v=='v')?12:(v=='r')?13:(v=='*')?6:(v=='A')?6:(v=='H')?11:(v=='B')?11:(v=='E')?7:(v=='Z')?13:((x*13+y*7)&1);
        if(fate_vault && v=='?' && !r->claimed)id=6; /* sealed rune */
        if(r->claimed && (v=='!'||v=='?'||v=='+'||v=='$'))id=(x+y)&1;
        const lv_image_dsc_t *image=dg_art_tile(b,id);
        if(last_tile_image[y*DG_W+x]!=image){
            lv_image_set_src(t,image);
            last_tile_image[y*DG_W+x]=image;
        }
    }
    last_room=game.current;
}
static const char *last_event(void){
    switch(game.event){
    case DG_EVT_HIT:return "Steel meets bone!";
    case DG_EVT_KILL:return "Enemy defeated  +gold";
    case DG_EVT_HURT:return "You were struck!";
    case DG_EVT_DODGE:return "Attack avoided";
    case DG_EVT_TREASURE:return "Treasure recovered!";
    case DG_EVT_HEAL:return "Lantern restored";
    case DG_EVT_SHRINE:return "A relic offers a bargain";
    case DG_EVT_BUY:return "Potion purchased";
    case DG_EVT_DOOR:return dg_lore_room_whisper(&game);
    case DG_EVT_DESCEND:return "Descending deeper...";
    case DG_EVT_GUARD:return "Shield raised";
    case DG_EVT_DEAD:return "The lantern goes dark";
    case DG_EVT_BOSS:return "A sovereign awakens";
    case DG_EVT_RELIC:return strcmp(dg_resonance_name(&game),"None")?
        "RELIC RESONANCE AWAKENED!":"A relic joins your journey";
    case DG_EVT_PUZZLE:return game.rooms[game.current].puzzle_solved?"Rune awakened! +18 gold":"Push the chest onto the rune";
    case DG_EVT_BOSS_PHASE:return "The sovereign grows furious!";
    case DG_EVT_WEAPON:return "A new weapon is ready";
    case DG_EVT_SKILL:return "LANTERN NOVA! Foes stagger";
    case DG_EVT_SLIME_SPLIT:return "THE SLIME DIVIDES!";
    case DG_EVT_STOLEN:return "MIMIC! Your gold was stolen";
    case DG_EVT_FLAME_STEP:return "FLAME STEP! Evade the warning";
    case DG_EVT_FATE_VAULT:return game.pending_relic==3?
        "THE SEAL IS YOUR CHOICE": "THE FATE VAULT SHATTERS!";
    case DG_EVT_HOUSE_ARCHIVE:return "House evidence entered in Chronicle";
    case DG_EVT_HOUSE_HAZARD:return "Ancient House ward discharged";
    case DG_EVT_HF_BELL:return "Sibrell bell silenced. The Choir can speak.";
    case DG_EVT_HF_EVIDENCE:return "Heavensfall dispatch recovered.";
    case DG_EVT_HF_BREACH:return "Ashen breach sealed. The vault remembers.";
    case DG_EVT_HF_CHOIR:return "The Choir speaks. Its guardian has awakened.";
    case DG_EVT_HF_SILENCE:return "The Last Bell is silent. The record is lost.";
    case DG_EVT_BLOCKED:return "The way is sealed";
    default:return "Move   /   Strike   /   Explore";
    }
}
static void resume_animation_if_playing(void){
    if(animation_timer && !menu_open && power_screen_is_on())
        lv_timer_resume(animation_timer);
}
static void dismiss_dialog(void){
    lv_obj_set_hidden(dialog,true);
    menu_open=false;
    resume_animation_if_playing();
}
static void present_ending_page(void){
    unsigned ending=dg_final_state(&game);
    const dg_campaign_page *page=dg_campaign_ending(ending,epilogue_page);
    if(!page)return;
    set_text(dialog_title,page->title);
    set_text(dialog_body,page->body);
    lv_obj_scroll_to_y(dialog_text_view,0,LV_ANIM_OFF);
}
static void show_dialog(void){
    if(chapter_card)lv_obj_set_hidden(chapter_card,true);
    chapter_card_ticks=0;
    menu_open=true;
    if(animation_timer)lv_timer_pause(animation_timer);
    lv_obj_set_hidden(dialog,false);
    if(!game.alive){
        char recap[180];
        snprintf(recap,sizeof(recap),
            "FLOOR %lu  |  %lu foes slain\n%lu gold found\nThe darkness remembers your name.\nRise again, Lantern Knight.",
            (unsigned long)game.depth,(unsigned long)game.total_kills,
            (unsigned long)game.total_gold);
        set_text(dialog_title,"THE LANTERN FADES");set_text(dialog_body,recap);
    }
    else if(dg_final_state(&game)){
        present_ending_page();
    }
    else if(game.depth==9u && game.current==game.exit_room && dg_enemies_alive(&game.rooms[game.current])==0){
        set_text(dialog_title,"MELIRA / THE TESTAMENT");
        set_text(dialog_body,"The Witness offers the erased a voice. Melira offers the living another morning.\nRESET: preserve the cycle.\nUNMAKE: break the ledger.\nWITNESS: require the Choir's testimony and both carried obligations.");
    }
    else if(dg_hf_last_bell_state(&game)==1 && game.current==game.exit_room && dg_enemies_alive(&game.rooms[game.current])==0){
        set_text(dialog_title,"THE LAST BELL");
        set_text(dialog_body,"SILENCE: spend mana/guard; erase the Choir's words.\nSPEAK: awaken a defender; preserve its testimony.\nStand beside the bell to decide.");
    }
    else if(game.pending_relic==3){
        set_text(dialog_title,"THE FATE VAULT");
        set_text(dialog_body,
            "TAKE: 12 gold, no fight.\n"
            "SHATTER: 28 gold and\n"
            "two summoned guardians.\n"
            "LEAVE: return later.");
    }
    else if(game.pending_relic==2){
        char body[180];
        uint8_t weapon=dg_merchant_weapon(&game);
        snprintf(body,sizeof(body),
            "The Wandering Forge\nYou carry %lu gold.\n%s\nTake supplies, or leave and return.",
            (unsigned long)game.gold,
            weapon==DG_NONE?"All arms collected: restore HP/MP for 12G.":
            "A legendary arm awaits for 16G.");
        set_text(dialog_title,"THE WANDERING FORGE");set_text(dialog_body,body);
    }
    else if(game.pending_relic==1){
        char offer[180];uint8_t a=dg_relic_offer(&game,0);
        snprintf(offer,sizeof(offer),"A %s: %s\nB %s: %s",dg_relic_name(a),dg_world_relic_provenance(a),dg_relic_name(dg_relic_offer(&game,1)),dg_world_relic_provenance(dg_relic_offer(&game,1)));
        set_text(dialog_title,"CHOOSE A RELIC");set_text(dialog_body,offer);
    }
    else if(game.event==DG_EVT_BOSS && game.rooms[game.current].kind==DG_BOSS){
        set_text(dialog_title,dg_boss_name(&game));
        set_text(dialog_body,dg_lore_boss_omen(&game));
    }
    else {
        char guidance[180];
        const char *resonance=dg_resonance_name(&game);
        if(game.rooms[game.current].kind==DG_PUZZLE&&
           !game.rooms[game.current].puzzle_solved){
            snprintf(guidance,sizeof(guidance),
                "Push the golden crate onto the rune. RESET retries.\nRelic resonance: %s",resonance);
        }else{
            snprintf(guidance,sizeof(guidance),
                "NOVA: MENU, 2 MP.\nHOLD ARROW: Flame Step.\nHOLD MENU: Black Testament.\nResonance: %s",
                resonance);
        }
        set_text(dialog_title,"RELICS OF THE LANTERN");set_text(dialog_body,guidance);
    }
    bool finale_choice=game.alive && game.depth==9u && !dg_final_state(&game) && game.current==game.exit_room && dg_enemies_alive(&game.rooms[game.current])==0;
    bool finale_done=dg_final_state(&game)!=0;
    bool last_bell_choice=game.alive && dg_hf_last_bell_state(&game)==1 && game.current==game.exit_room && dg_enemies_alive(&game.rooms[game.current])==0;
    set_text(lv_obj_get_child(dialog_yes,0),finale_done?"PREVIOUS PAGE":finale_choice?"RESET":last_bell_choice?"SILENCE BELL":game.pending_relic==3?"TAKE 12 GOLD":
        game.pending_relic==2?
        (dg_merchant_weapon(&game)==DG_NONE?"RESTORE HP/MP 12G":dg_weapon_name(dg_merchant_weapon(&game))):
        game.pending_relic==1?dg_relic_name(dg_relic_offer(&game,0)):
        game.alive?"CONTINUE":"NEW RUN");
    set_text(lv_obj_get_child(dialog_no,0),finale_done?"NEXT PAGE":finale_choice?"UNMAKE":last_bell_choice?"LET CHOIR SPEAK":game.pending_relic==3?"BREAK SEAL: 28 GOLD":
        game.pending_relic==2?"BUY POTION 10G":
        game.pending_relic==1?dg_relic_name(dg_relic_offer(&game,1)):
        !game.alive?"NEW RUN":
        (game.rooms[game.current].kind==DG_PUZZLE&&!game.rooms[game.current].puzzle_solved)?
        "RESET PUZZLE":game.mana>=2?"LANTERN NOVA":"NOVA NEEDS 2 MP");
    set_text(lv_obj_get_child(dialog_exit,0),game.pending_relic==3?"LEAVE SEALED":
        game.pending_relic?"WALK AWAY":finale_done?"RETURN TO ARCADE":finale_choice?"TENTH WITNESS":"EXIT TO ARCADE");
    /* Every dialog starts at its first line; longer text can be scrolled. */
    lv_obj_scroll_to_y(dialog_text_view,0,LV_ANIM_OFF);
}
/* The gold readout is bounded on a 240px screen; purchases still use exact
 * values in the game core. No gameplay state is modified by this formatting. */
static void format_gold(char *out,size_t capacity,uint32_t gold){
    if(gold < 10000u)snprintf(out,capacity,"G%lu",(unsigned long)gold);
    else if(gold < 1000000u)
        snprintf(out,capacity,"G%luK",(unsigned long)(gold/1000u));
    else snprintf(out,capacity,"G%luM",(unsigned long)(gold/1000000u));
}
static void refresh_hp_bar(void){
    uint32_t hp=game.hp, max_hp=game.max_hp?game.max_hp:1u;
    int width=(int)((hp*230u)/max_hp);
    if(width>230)width=230;
    if(hp && width<2)width=2;
    if(width!=last_hp_width){
        lv_obj_set_width(hud_hp_fill,width);
        last_hp_width=width;
    }
    uint8_t tone=(hp*4u<=max_hp)?0u:(hp*2u<=max_hp)?1u:2u;
    if(tone!=last_hp_tone){
        static const uint32_t tones[]={0xFF696B,0xE4AB5D,0x81C7A4};
        lv_obj_set_style_bg_color(hud_hp_fill,lv_color_hex(tones[tone]),0);
        last_hp_tone=tone;
    }
}
/* R11: a single visual language for danger, discovery, and the three war chapters.
 * Palette changes are deterministic, allocation-free, and never touch save data. */
static uint32_t chapter_ink(void){
    if(game.depth==6)return 0x9DC5D3; /* falling choir: cold ash */
    if(game.depth==7)return 0xD6AC73; /* bells: tarnished brass */
    if(game.depth==8)return 0xCE847E; /* breach: blood and stone */
    if(game.depth>=9)return 0xC7ACDF; /* judgment: bruised violet */
    return 0xE2C99D;                  /* ordinary lantern light */
}
static uint32_t event_ink(dg_event event){
    switch(event){
    case DG_EVT_HURT:case DG_EVT_DEAD:case DG_EVT_STOLEN:return 0xFF817C;
    case DG_EVT_HIT:case DG_EVT_KILL:case DG_EVT_GUARD:return 0xF6D39A;
    case DG_EVT_HOUSE_ARCHIVE:case DG_EVT_HF_EVIDENCE:case DG_EVT_HF_CHOIR:return 0xA9E1DB;
    case DG_EVT_HF_BELL:case DG_EVT_HF_BREACH:case DG_EVT_HF_SILENCE:return 0xE8B46F;
    case DG_EVT_SKILL:case DG_EVT_FLAME_STEP:return 0x9AD7FF;
    default:return 0xF4E3C5;
    }
}
static void refresh(bool room_changed,bool new_action){
    if(room_changed||last_room!=game.current)redraw_room();
    bool chapter_arrival=(presented_depth!=game.depth);
    presented_depth=game.depth;
    if(chapter_arrival && chapter_card && dg_campaign_arrival(game.depth)){
        set_text(chapter_card_text,dg_campaign_arrival(game.depth)->title);
        set_text(chapter_card_subtitle,dg_campaign_arrival(game.depth)->body);
        lv_obj_set_hidden(chapter_card,false);
        lv_obj_move_foreground(chapter_card);
        chapter_card_ticks=7; /* 7 * 260 ms; no blocking or allocation */
    }
    /* Short-lived action poses settle back to idle automatically. */
    if(new_action){
        if(game.event==DG_EVT_HIT||game.event==DG_EVT_KILL){hero_pose=1;pose_ticks=2;}
        else if(game.event==DG_EVT_GUARD){hero_pose=2;pose_ticks=2;}
        else if(game.event==DG_EVT_HURT){hero_pose=3;pose_ticks=2;}
        else if(game.event==DG_EVT_BOSS_PHASE||game.event==DG_EVT_DESCEND||
                game.event==DG_EVT_FLAME_STEP){hero_pose=4;pose_ticks=2;}
    }
    update_actor_positions();
    update_warnings();
    char buf[128];
    if(game.rooms[game.current].kind==DG_BOSS)
        snprintf(buf,sizeof(buf),"F%lu  %.19s  P%u",(unsigned long)game.depth,
                 dg_boss_name(&game),game.rooms[game.current].boss_phase);
    else snprintf(buf,sizeof(buf),"F%lu  %s",(unsigned long)game.depth,dg_world_house_name(dg_world_house(&game)));
    set_text(hud_top,buf);
    uint32_t top_ink=chapter_ink();
    if(top_ink!=last_top_ink){lv_obj_set_style_text_color(hud_top,lv_color_hex(top_ink),0);last_top_ink=top_ink;}
    snprintf(buf,sizeof(buf),"HP%u/%u  MP%u  P%u",game.hp,game.max_hp,
             game.mana,game.potions);
    set_text(hud_bottom,buf);
    format_gold(buf,sizeof(buf),game.gold);
    set_text(hud_gold,buf);
    refresh_hp_bar();
    /* Don't advertise a usable healing action at full health or 0 potions. */
    bool heal_available=game.alive && game.potions && game.hp<game.max_hp;
    bool disabled=(lv_obj_get_state(control[5]) & LV_STATE_DISABLED)!=0;
    if(heal_available && disabled)lv_obj_remove_state(control[5],LV_STATE_DISABLED);
    else if(!heal_available && !disabled)lv_obj_add_state(control[5],LV_STATE_DISABLED);
    if(game.event==DG_EVT_WEAPON){
        snprintf(buf,sizeof(buf),"Equipped: %s",dg_weapon_name(game.weapon));
        set_text(event_label,buf);
    }else if(chapter_arrival && dg_campaign_arrival(game.depth))
        set_text(event_label,dg_campaign_arrival(game.depth)->title);
    else set_text(event_label,last_event());
    uint32_t event_color=event_ink(game.event);
    if(event_color!=last_event_ink){lv_obj_set_style_text_color(event_label,lv_color_hex(event_color),0);last_event_ink=event_color;}
    if(new_action && (game.event==DG_EVT_HF_BELL||game.event==DG_EVT_HF_BREACH||game.event==DG_EVT_HF_CHOIR||game.event==DG_EVT_HF_SILENCE||game.event==DG_EVT_HOUSE_ARCHIVE||game.event==DG_EVT_HURT||game.event==DG_EVT_HIT||game.event==DG_EVT_KILL||
        game.event==DG_EVT_SKILL||game.event==DG_EVT_FLAME_STEP||
        game.event==DG_EVT_FATE_VAULT)&&fx)
        arcade_fx_burst(fx,game.event_x*24+12,game.event_y*24+12,
            lv_color_hex(game.event==DG_EVT_HURT?0xF57268:
                         (game.event==DG_EVT_SKILL||game.event==DG_EVT_FLAME_STEP)?0x92DFFF:
                         game.event==DG_EVT_FATE_VAULT?0xE8A14B:event_ink(game.event)));
    if(game.pending_relic||!game.alive||dg_final_state(&game))show_dialog();
}
/* A short, four-line book page fits the existing 240x320 dialog without
 * allocating widgets or replacing the gameplay input controls. */
static void show_lore_dialog(void){
    if(chapter_card)lv_obj_set_hidden(chapter_card,true);
    chapter_card_ticks=0;
    dg_lore_fragment fragment;
    unsigned count=dg_lore_count(&game);
    if(count==0)return;
    if(lore_page>=count)lore_page=count-1;
    if(!dg_lore_at(&game,lore_page,&fragment))return;
    lore_mode=true;
    menu_open=true;
    if(animation_timer)lv_timer_pause(animation_timer);
    lv_obj_set_hidden(dialog,false);
    set_text(dialog_title,fragment.title);
    set_text(dialog_body,fragment.text);
    lv_obj_scroll_to_y(dialog_text_view,0,LV_ANIM_OFF);
    set_text(lv_obj_get_child(dialog_yes,0),"PREVIOUS PAGE");
    char progress[48];
    snprintf(progress,sizeof(progress),"NEXT  %u / %u",lore_page+1,count);
    set_text(lv_obj_get_child(dialog_no,0),progress);
    set_text(lv_obj_get_child(dialog_exit,0),"CLOSE CHRONICLE");
}

static void on_menu_longpress(lv_event_t *e){
    (void)e;
    if(menu_open || !game.alive)return;
    chronicle_page=0;mirror_confirm=false;
    chronicle_observe();show_chronicle();
}
static void dialog_cb(lv_event_t *e){
    int key=(int)(intptr_t)lv_event_get_user_data(e);
    bool actual_action=false;
    if(chronicle_mode){
        if(key==2){chronicle_mode=false;mirror_confirm=false;dismiss_dialog();return;}
        if(chronicle_page==13+DG_WORLD_PAGES){
            unsigned pending=mirror_pending();
            if(pending<2){
                if(key==0 && mirror_confirm){mirror_confirm=false;}
                else if(key==0){
                    if(dg_chronicle_decide(&chronicle,pending,true)){
                        chronicle_dirty=true;save_chronicle();
                    }
                }else if(key==1 && !mirror_confirm){mirror_confirm=true;}
                else if(key==1 && mirror_confirm){
                    if(dg_chronicle_decide(&chronicle,pending,false)){
                        chronicle_dirty=true;save_chronicle();
                    }
                    mirror_confirm=false;
                }
            }
        }else{
            if(key==0 && chronicle_page>0)chronicle_page--;
            if(key==1 && chronicle_page<13+DG_WORLD_PAGES)chronicle_page++;
        }
        show_chronicle();return;
    }
    if(lore_mode){
        if(key==2){
            lore_mode=false;
            dismiss_dialog();
            return;
        }
        if(key==0 && lore_page>0)lore_page--;
        if(key==1 && lore_page+1<dg_lore_count(&game))lore_page++;
        show_lore_dialog();
        return;
    }
    if(game.alive && game.depth==9u && game.current==game.exit_room &&
       dg_enemies_alive(&game.rooms[game.current])==0){
        if(dg_final_state(&game)){
            if(key==2){save_game();dismiss_dialog();switch_screen(pocket_arcade_screen);}
            else if(key==0 && epilogue_page>0)epilogue_page--;
            else if(key==1 && epilogue_page+1<DG_CAMPAIGN_EPILOGUE_PAGES)epilogue_page++;
            show_dialog();return;
        }
        unsigned ending=(unsigned)key+1u;
        if(ending==3u && (chronicle.last_bell_evidence!=3 ||
           (chronicle.face_decisions&15u)!=5u)){
            set_text(dialog_body,"The Tenth Witness requires the Choir's testimony and both Borrowed Faces carried willingly. Choose RESET or UNMAKE, or return to seek consent.");
            return;
        }
        if(!dg_final_choose(&game,ending)){
            set_text(dialog_body,"Clear the chamber and stand beside the Testament at the exit to decide.");
            return;
        }
        if(dg_chronicle_record_ending(&chronicle,ending)){chronicle_dirty=true;save_chronicle();}
        epilogue_page=0;save_dirty=true;save_game();show_dialog();return;
    }
    /* The Last Bell uses the existing two choice buttons; cancel is safe. */
    if(game.alive && dg_hf_last_bell_state(&game)==1 &&
       game.current==game.exit_room && dg_enemies_alive(&game.rooms[game.current])==0){
        if(key==2){dismiss_dialog();return;}
        if(!dg_hf_last_bell_choose(&game,key==1)){
            set_text(dialog_body,"Stand beside the Last Bell. Clear all enemies first; the Choir requires an open landing tile.");
            return;
        }
        unsigned outcome=dg_hf_last_bell_state(&game);
        if(dg_chronicle_record_last_bell(&chronicle,outcome)){
            chronicle_dirty=true;save_chronicle();
        }
        save_dirty=true;save_game();dismiss_dialog();refresh(true,true);return;
    }
    if(key==2 && game.pending_relic){
        if(game.pending_relic==3)dg_choose_vault(&game,2);
        else if(game.pending_relic==2)dg_choose_merchant(&game,2);
        else dg_choose_relic(&game,2);
        save_dirty=true;save_game();dismiss_dialog();refresh(true,true);return;
    }
    if(key==2){save_game();dismiss_dialog();switch_screen(pocket_arcade_screen);return;}
    if(game.pending_relic){
        bool ok=game.pending_relic==3?dg_choose_vault(&game,key):
                game.pending_relic==2?dg_choose_merchant(&game,key):
                dg_choose_relic(&game,key);
        if(!ok)return;
        actual_action=true;
        save_dirty=true;save_game();
    }
    else if(key==1&&game.alive&&game.rooms[game.current].kind==DG_PUZZLE&&
            !game.rooms[game.current].puzzle_solved){
        if(dg_reset_puzzle(&game)){
            actual_action=true;
            save_dirty=true;save_game();
        }
    }
    else if(key==1&&game.alive){
        if(!dg_lantern_nova(&game))return;
        actual_action=true;
        save_dirty=true;
        if(!game.alive){show_dialog();return;}
    }
    else if(!game.alive){
        dg_new(&game,(uint32_t)time(NULL)^lv_tick_get());
        last_room=-1;save_dirty=true;save_game();
        actual_action=true;
    }
    /* Clearing help doesn't repeat the last combat flash or strike pose. */
    dismiss_dialog();refresh(true,actual_action);
}
/* The direction buttons expose a second, distinct tactical action without
 * stealing any screen area from the existing 240x320 control layout. */
static void on_direction_pressed(lv_event_t *e){
    int key=(int)(intptr_t)lv_event_get_user_data(e);
    if(key>=0 && key<4)flame_step_held[key]=false;
}
static void on_direction_held(lv_event_t *e){
    int key=(int)(intptr_t)lv_event_get_user_data(e);
    if(key<0||key>=4||menu_open)return;
    flame_step_held[key]=true;
    if(dg_flame_step(&game,key)){
        save_dirty=true;
        refresh(false,true);
        if(!game.alive)show_dialog();
    }else{
        /* An unsuccessful hold still needs feedback, but costs no turn. */
        set_text(event_label,game.mana==0?"Flame Step needs 1 MP":"No safe landing tile");
    }
}
static void on_control(lv_event_t *e){
    if(menu_open)return;
    int key=(int)(intptr_t)lv_event_get_user_data(e);
    if(key>=0 && key<4 && flame_step_held[key]){
        flame_step_held[key]=false;
        return; /* do not move again after a long press */
    }
    int previous=game.current;
    uint32_t floor=game.depth;
    uint8_t old_claimed=game.rooms[game.current].claimed;
    dg_event old_event=game.event;
    bool changed=false;
    if(key>=0&&key<4)changed=dg_action(&game,key);
    else if(key==4)changed=dg_guard(&game);
    else if(key==5)changed=dg_potion(&game);
    else if(key==6){show_dialog();return;}
    else if(key==7)changed=dg_cycle_weapon(&game);
    if(changed){save_dirty=true;chronicle_observe();}
    if(game.pending_relic){show_dialog();return;}
    if(changed&&(previous!=game.current||floor!=game.depth))save_game();
    /* No-op guard/potion/gear taps need not wake the render tree or replay
     * the previous combat flash. A newly blocked move still changes the hint. */
    if(!changed && game.event==old_event)return;
    refresh(previous!=game.current||floor!=game.depth||game.rooms[game.current].claimed!=old_claimed,
            changed || game.event!=old_event);
    if(changed && previous!=game.current && game.rooms[game.current].kind==DG_BOSS)
        show_dialog();
}
static void on_unload(lv_event_t *e){
    if(chapter_card)lv_obj_set_hidden(chapter_card,true);
    chapter_card_ticks=0;
    (void)e;save_game();save_chronicle();lore_mode=false;chronicle_mode=false;if(animation_timer)lv_timer_pause(animation_timer);
    topbar_set_hidden(false);back_btn_force_hidden(false);
    /* Global music playback keys have never been intercepted. */
}
static void on_loaded(lv_event_t *e){
    (void)e;topbar_set_hidden(true);back_btn_force_hidden(true);
    resume_animation_if_playing();
}
static void anim_tick(lv_timer_t *timer){(void)timer;if(!power_screen_is_on()||menu_open||player_sheet_is_open()||player_sheet_drag_active())return;
    if(chapter_card_ticks && --chapter_card_ticks==0 && chapter_card)
        lv_obj_set_hidden(chapter_card,true);
    animation_frame^=1u;if(pose_ticks && --pose_ticks==0)hero_pose=0;update_actor_positions();}
static void build(void){
    if(!cfg)return;
    screen=lv_obj_create(NULL);
    lv_obj_set_style_bg_color(screen,lv_color_hex(0x151C29),0);
    lv_obj_set_style_bg_opa(screen,LV_OPA_COVER,0);
    lv_obj_set_scrollable(screen,false);
    /* Two-line HUD: persistent survival information never disappears under
     * a long weapon name or chamber title. Gold has its own fixed-width lane. */
    hud_top=label(screen,"RELICS OF THE LANTERN",5,2,230,14);
    hud_bottom=label(screen,"",5,22,157,14);
    hud_gold=label(screen,"",165,22,70,14);
    lv_obj_set_style_text_align(hud_gold,LV_TEXT_ALIGN_RIGHT,0);
    lv_obj_set_style_text_color(hud_gold,lv_color_hex(0xE6C68D),0);
    /* Thin survival meter below the numerical HUD, above the world border. */
    hud_hp_track=lv_obj_create(screen);
    lv_obj_set_pos(hud_hp_track,5,37);
    lv_obj_set_size(hud_hp_track,230,2);
    lv_obj_set_style_bg_color(hud_hp_track,lv_color_hex(0x323E4C),0);
    lv_obj_set_style_bg_opa(hud_hp_track,LV_OPA_COVER,0);
    lv_obj_set_style_border_width(hud_hp_track,0,0);
    lv_obj_set_style_pad_all(hud_hp_track,0,0);
    lv_obj_set_style_radius(hud_hp_track,0,0);
    lv_obj_set_clickable(hud_hp_track,false);
    hud_hp_fill=lv_obj_create(screen);
    lv_obj_set_pos(hud_hp_fill,5,37);
    lv_obj_set_size(hud_hp_fill,230,2);
    lv_obj_set_style_bg_opa(hud_hp_fill,LV_OPA_COVER,0);
    lv_obj_set_style_border_width(hud_hp_fill,0,0);
    lv_obj_set_style_pad_all(hud_hp_fill,0,0);
    lv_obj_set_style_radius(hud_hp_fill,0,0);
    lv_obj_set_clickable(hud_hp_fill,false);
    arena=lv_obj_create(screen);lv_obj_set_pos(arena,0,40);lv_obj_set_size(arena,240,192);
    lv_obj_set_style_pad_all(arena,0,0);lv_obj_set_style_border_width(arena,0,0);
    lv_obj_set_scrollable(arena,false);lv_obj_set_style_bg_color(arena,lv_color_hex(0x1C2632),0);

    for(int y=0;y<DG_H;y++)for(int x=0;x<DG_W;x++){
        int k=y*DG_W+x;tiles[k]=lv_image_create(arena);
        lv_obj_set_pos(tiles[k],x*24,y*24);lv_obj_set_size(tiles[k],24,24);
        lv_obj_set_clickable(tiles[k],false);
    }
    for(int y=0;y<DG_H;y++)for(int x=0;x<DG_W;x++){
        lv_obj_t *o=lv_obj_create(arena);warnings[y*DG_W+x]=o;
        lv_obj_set_pos(o,x*24+2,y*24+2);lv_obj_set_size(o,20,20);
        lv_obj_set_style_pad_all(o,0,0);lv_obj_set_style_radius(o,2,0);
        lv_obj_set_style_border_width(o,2,0);lv_obj_set_style_border_color(o,lv_color_hex(0xFF7972),0);
        lv_obj_set_style_bg_color(o,lv_color_hex(0xFF5E52),0);
        lv_obj_set_style_bg_opa(o,LV_OPA_50,0);lv_obj_set_clickable(o,false);
        lv_obj_set_hidden(o,true);
    }
    for(int i=0;i<DG_FOES+2;i++){
        actors[i]=lv_image_create(arena);lv_obj_set_clickable(actors[i],false);
        lv_image_set_src(actors[i],dg_art_actor(0,0));
        last_actor_image[i]=dg_art_actor(0,0);
        actor_x_cache[i]=INT16_MIN;actor_y_cache[i]=INT16_MIN;
        lv_obj_set_hidden(actors[i],true);
    }
    /* Danger belongs above actors: an occupied tile must remain telegraphed.
     * FX is created afterward so brief hit sparkles remain on top. */
    for(int i=0;i<DG_W*DG_H;i++)lv_obj_move_foreground(warnings[i]);
    fx=arcade_fx_create(arena);
    event_label=label(screen,"MOVE / STRIKE / EXPLORE",5,231,230,14);
    /* The original word labels collided at 240x320. A tactile diamond
     * D-pad uses the four actual arrow glyphs shipped in MiSans OTF. Its
     * center is decorative, not clickable; callbacks and held Flame Step
     * remain assigned to their original direction IDs. */
    lv_obj_t *dpad_core=lv_obj_create(screen);
    lv_obj_set_pos(dpad_core,40,270);
    lv_obj_set_size(dpad_core,43,24);
    lv_obj_set_style_pad_all(dpad_core,0,0);
    lv_obj_set_style_radius(dpad_core,1,0);
    lv_obj_set_style_border_width(dpad_core,0,0);
    lv_obj_set_style_bg_color(dpad_core,lv_color_hex(0x354353),0);
    lv_obj_set_style_bg_opa(dpad_core,LV_OPA_COVER,0);
    lv_obj_set_clickable(dpad_core,false);
    control[0]=make_button(screen,"↑",44,245,35,31,on_control,0);
    control[1]=make_button(screen,"→",83,267,35,32,on_control,1);
    control[2]=make_button(screen,"↓",44,284,35,31,on_control,2);
    control[3]=make_button(screen,"←",5,267,35,32,on_control,3);
    for(int i=0;i<4;i++){
        lv_obj_set_style_text_font(lv_obj_get_child(control[i],0),&font_ui_18,0);
    }
    for(int i=0;i<4;i++){
        lv_obj_add_event_cb(control[i],on_direction_pressed,LV_EVENT_PRESSED,
                            (void*)(intptr_t)i);
        lv_obj_add_event_cb(control[i],on_direction_held,LV_EVENT_LONG_PRESSED,
                            (void*)(intptr_t)i);
    }
    control[4]=make_button(screen,"GUARD",125,247,52,31,on_control,4);
    control[5]=make_button(screen,"POTION",182,247,54,31,on_control,5);
    control[6]=make_button(screen,"MENU",125,284,52,31,on_control,6);
    lv_obj_add_event_cb(control[6],on_menu_longpress,LV_EVENT_LONG_PRESSED,NULL);
    control[7]=make_button(screen,"GEAR",182,284,54,31,on_control,7);
    dialog=lv_obj_create(screen);lv_obj_set_pos(dialog,13,33);lv_obj_set_size(dialog,214,255);
    lv_obj_set_scrollable(dialog,false); /* only its text pane scrolls */
    lv_obj_set_style_bg_color(dialog,lv_color_hex(0x242B38),0);
    lv_obj_set_style_bg_opa(dialog,LV_OPA_COVER,0);lv_obj_set_style_border_color(dialog,lv_color_hex(0xC09A5E),0);
    lv_obj_set_style_border_width(dialog,2,0);lv_obj_set_style_pad_all(dialog,0,0);
    /* R22.3: a framed title, scrollable narrative, and an actual bottom inset.
     * The previous EXIT button ended 4px from the gold rim at y=251. */
    dialog_title=label(dialog,"RELICS OF THE LANTERN",8,10,198,18);
    lv_obj_t *dialog_rule=lv_obj_create(dialog);
    lv_obj_set_pos(dialog_rule,12,34);
    lv_obj_set_size(dialog_rule,188,1);
    lv_obj_set_style_pad_all(dialog_rule,0,0);
    lv_obj_set_style_border_width(dialog_rule,0,0);
    lv_obj_set_style_radius(dialog_rule,0,0);
    lv_obj_set_style_bg_color(dialog_rule,lv_color_hex(0x866B4A),0);
    lv_obj_set_style_bg_opa(dialog_rule,LV_OPA_COVER,0);
    lv_obj_set_scrollable(dialog_rule,false);
    lv_obj_set_clickable(dialog_rule,false);
    dialog_text_view=lv_obj_create(dialog);
    lv_obj_set_pos(dialog_text_view,11,42);
    lv_obj_set_size(dialog_text_view,192,93);
    lv_obj_set_style_pad_all(dialog_text_view,0,0);
    lv_obj_set_style_border_width(dialog_text_view,0,0);
    lv_obj_set_style_radius(dialog_text_view,0,0);
    lv_obj_set_style_bg_opa(dialog_text_view,LV_OPA_TRANSP,0);
    lv_obj_set_scrollable(dialog_text_view,true);
    lv_obj_set_scroll_dir(dialog_text_view,LV_DIR_VER);
    lv_obj_set_scrollbar_mode(dialog_text_view,LV_SCROLLBAR_MODE_AUTO);
    dialog_body=label(dialog_text_view,"Every room is different.",0,0,188,14);
    lv_label_set_long_mode(dialog_body,LV_LABEL_LONG_WRAP);
    /* Touch targets remain 29px high, with 7px gutters and 12px of real
     * bottom breathing room inside the 255px framed window. */
    dialog_yes=make_button(dialog,"CONTINUE",14,143,185,29,dialog_cb,0);
    dialog_no=make_button(dialog,"NEW RUN",14,179,185,29,dialog_cb,1);
    dialog_exit=make_button(dialog,"EXIT TO ARCADE",14,215,185,29,dialog_cb,2);
    lv_obj_set_style_bg_color(dialog_exit,lv_color_hex(0x30313C),0);
    lv_obj_set_style_border_color(dialog_exit,lv_color_hex(0x8B7567),0);
    lv_obj_set_hidden(dialog,true);
    chapter_card=lv_obj_create(screen);
    lv_obj_set_pos(chapter_card,10,100);lv_obj_set_size(chapter_card,220,66);
    lv_obj_set_scrollable(chapter_card,false);
    lv_obj_set_style_bg_color(chapter_card,lv_color_hex(0x171D2B),0);
    lv_obj_set_style_bg_opa(chapter_card,LV_OPA_COVER,0);
    lv_obj_set_style_border_color(chapter_card,lv_color_hex(0xC09A5E),0);
    lv_obj_set_style_border_width(chapter_card,1,0);
    lv_obj_set_style_pad_all(chapter_card,0,0);
    chapter_card_text=label(chapter_card,"",6,7,208,22);
    lv_obj_set_style_text_align(chapter_card_text,LV_TEXT_ALIGN_CENTER,0);
    chapter_card_subtitle=label(chapter_card,"",8,31,204,30);
    lv_label_set_long_mode(chapter_card_subtitle,LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(chapter_card_subtitle,LV_TEXT_ALIGN_CENTER,0);
    lv_obj_set_hidden(chapter_card,true);
    animation_timer=lv_timer_create(anim_tick,260,NULL);
    lv_timer_pause(animation_timer);power_pause_in_standby(animation_timer);
    lv_obj_add_event_cb(screen,on_loaded,LV_EVENT_SCREEN_LOADED,NULL);
    pocket_arcade_enable_nowplaying_swipe(screen);
    lv_obj_add_event_cb(screen,on_unload,LV_EVENT_SCREEN_UNLOADED,NULL);
}
void dungeon_deluxe_init(gui_config_t *c){cfg=c;}
void dungeon_deluxe_open(void){
    if(!cfg)return;
    if(!screen)build();
    bool new_journey=false;
    if(!ready){
        saved_known=save_file_path()&&dg_load(&game,save_location);
        if(!saved_known)dg_new(&game,(uint32_t)time(NULL)^lv_tick_get());
        new_journey=!saved_known;
        save_dirty=!saved_known;
        dg_chronicle_init(&chronicle);
        chronicle_path[0]=0;
        if(save_location[0] && strlen(save_location)+4<sizeof(chronicle_path)){
            snprintf(chronicle_path,sizeof(chronicle_path),"%s.btc",save_location);
            /* On missing/corrupt files retain the original for diagnosis; never overwrite on load. */
            (void)dg_chronicle_load(&chronicle,chronicle_path);
        }
        chronicle_dirty=false;
        chronicle_observe();
        ready=true;
    }
    memset(flame_step_held,0,sizeof(flame_step_held));
    lore_mode=false;chronicle_mode=false;lore_page=0;
    chapter_card_ticks=0;if(chapter_card)lv_obj_set_hidden(chapter_card,true);
    last_room=-1;last_top_ink=last_event_ink=0xffffffffu;animation_frame=0;hero_pose=0;pose_ticks=0;refresh(true,false);
    if(new_journey){
        /* First light: a four-line prologue. Never repeats on saved resume. */
        lore_page=1;
        show_lore_dialog();
    }else show_dialog();
    switch_screen(screen);
}
