/* Seven bonus Pocket Arcade games, shared LVGL screen; GPL-3.0-or-later.
 * No ALSA/decoder/player transport commands. All periodic work is paused
 * on page exit AND through Sonix's power_pause_in_standby registration. */
#include "arcade_extra_ui.h"
#include "pocket_arcade.h"
#include "src/gui/nowplaying/player.h"
#include "arcade_extra7.h"
#include "arcade_quiz.h"
#include "arcade_text.h"
#include "arcade_art.h"
#include "arcade_stage_art.h"
#include "arcade_scenery.h"
#include "arcade_motion.h"
#include "arcade_fx.h"
#include "beat_stage.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "lvgl/lvgl.h"
#include "src/gui/fonts/fonts.h"
#include "src/gui/shell/settingsrow.h"
#include "src/gui/shell/switcher.h"
#include "src/gui/shell/theme.h"
#include "src/system/core/config.h"
#include "src/system/audio/audio.h"
#include "src/system/audio/arcade_music.h"
#include "src/system/device/power.h"
#include "src/system/playback/device_state.h"
#include "src/system/library/library.h"

#define EXTRA_COUNT 7
#define UI_CELLS 81
static gui_config_t *acfg;
static lv_obj_t *screen, *status, *help, *board, *cells[UI_CELLS], *labels[UI_CELLS], *sprites[UI_CELLS];
static const lv_image_dsc_t *sprite_cache[UI_CELLS];
/* Renderer state mirrors the style properties we own. Avoid invalidating 81
 * LVGL children on every Orbit/Beat tick when their appearance is unchanged. */
static uint32_t cell_color_cache[UI_CELLS], cell_text_color_cache[UI_CELLS];
static int16_t cell_opa_cache[UI_CELLS];
static lv_obj_t *title_label, *controls[6], *page_heading, *scene_badge;
static lv_timer_t *tick_timer, *actor_timer;
static lv_obj_t *scene_backdrop, *scene_fx;
static char animated_actor[UI_CELLS];
static unsigned anim_frame;
static int cached_beat_points=-1, cached_dungeon_hp=-1;
static lv_point_t swipe_origin;
static bool swipe_armed;
/* A swipe must travel far enough to be intentional. Keep this independent
 * of render cadence and of LVGL's touch sampling frequency. */
#define ARCADE_SWIPE_MIN_PX 15
#define ARCADE_SWIPE_AXIS_RATIO 2
static int current=-1, columns=-1, rows=-1, melody_show_index=-1, melody_show_step=0;
static int melody_feedback_pad=-1;
static bool melody_retry_pending;
static char quiz_last_answered[PA_QUIZ_TITLE];
static char quiz_last_artist[PA_QUIZ_TITLE];
/* Store the question identity separately from the previous answered track.
 * Metadata may change while the quiz screen remains open. */
static char quiz_question_title[PA_QUIZ_TITLE];
static char quiz_question_artist[PA_QUIZ_TITLE];
static pa_boxes_t boxes;
static pa_golf_t golf;
static pa_orbit_t orbit;
static pa_melody_t melody;
static pa_melody_demo_t melody_demo;
static pa_beat_t beat;
static uint32_t beat_last_onset;
static bool beat_fire_held;
static pa_dungeon_t dungeon;
static uint32_t best[EXTRA_COUNT];
static pa_quiz_t quiz;
static char song_status[96];
static const char *const names[EXTRA_COUNT]={"Box Pusher","Tiny Golf","Orbit Hop","Melody Memory","Guess That Song","Beat Raiders","Pocket Dungeon"};
static const char *const config_names[EXTRA_COUNT]={"boxes","golf","orbit","melody","quiz","beat","dungeon"};

/* Game-specific cabinets. Shared mechanics, not shared visual personality.
 * Static flat colors: no animated full-screen texture that could starve audio. */
static const uint32_t skin_bg[EXTRA_COUNT]={0x30222B,0x173925,0x101736,0x24153C,0x221733,0x0C1530,0x251E2A};
static const uint32_t skin_stage[EXTRA_COUNT]={0x725542,0x2C6749,0x272656,0x392254,0x2F254A,0x182449,0x544256};
static const uint32_t skin_accent[EXTRA_COUNT]={0xF7B765,0xBAEF83,0xC5A8FF,0xF898D8,0x7CE5C3,0xFF82AB,0xF2C079};
static const char *const skin_tagline[EXTRA_COUNT]={
    "WAREHOUSE QUEST   /   8", "GARDEN GOLF   /   PAR THREE",
    "DEEP SPACE   /   PERFECT ORBIT", "SYNTH STUDIO   /   MEMORY",
    "VINYL QUIZ   /   NAME THE SONG", "STAR RAID   /   BOSS WAVE",
    "CRYPT OF RELICS   /   ROGUELIKE"
};
static void layout_game_controls(int id) {
    /* Keep the physical control widgets, but hide keys that have no function
     * in this particular game. This removes the dead six-key toolbar from
     * one-tap games, and gives visible actions proper touch targets. */
    static const uint8_t visible[EXTRA_COUNT][6]={
        {1,1,1,1,1,1}, /* Box Pusher: accessible directional keys + undo */
        {1,1,1,1,0,1}, /* Golf: directional shot, restart */
        {0,0,0,0,1,1}, /* Orbit: big tap-to-jump + restart */
        {0,0,0,0,1,1}, /* Melody: four instrument pads are on board */
        {0,0,0,0,0,1}, /* Quiz: answers are the four full-width cards */
        {1,1,0,1,1,1}, /* Beat: LEFT, hold FIRE, BURST, RIGHT, NEW */
        {1,1,1,1,1,1}  /* Dungeon: accessible directions, shield, potion */
    };
    int active_keys[6], count=0;
    for(int i=0;i<6;i++) {
        bool show=visible[id][i]!=0;
        lv_obj_set_hidden(controls[i],!show);
        if(show)active_keys[count++]=i;
    }
    /* Physical ordering must match movement. The engine's enum order is
     * UP, RIGHT, DOWN, LEFT; Beat only displays LEFT and RIGHT. */
    if(id==5) {
        active_keys[0]=3; /* LEFT */
        active_keys[1]=0; /* FIRE (UP button repurposed) */
        active_keys[2]=4; /* BURST */
        active_keys[3]=1; /* RIGHT */
        active_keys[4]=5; /* NEW */
    }
    int pad=acfg->padding, total=acfg->screen_width-2*pad;
    int gap=id==0?3:4, bw=(total-(count-1)*gap)/count;
    int x=pad;
    /* Box Pusher uses six controls. Reserve readable, wider action keys
     * instead of dividing the 240px screen into six identical tiny buttons. */
    int box_action_width=id==0?total*23/100:0;
    int box_direction_width=id==0?(total-5*gap-2*box_action_width)/4:0;
    for(int n=0;n<count;n++) {
        lv_obj_t *b=controls[active_keys[n]];
        int w=id==0?(n<4?box_direction_width:box_action_width):
              (n==count-1?pad+total-x:bw);
        if(id==0 && n==count-1)w=pad+total-x;
        lv_obj_set_pos(b,x,acfg->screen_height-pad-34);
        lv_obj_set_size(b,w,31);
        lv_obj_set_style_radius(b,4,0);
        lv_obj_set_style_bg_color(b,lv_color_hex(skin_stage[id]),0);
        lv_obj_set_style_bg_opa(b,LV_OPA_COVER,0);
        lv_obj_set_style_border_color(b,lv_color_hex(skin_accent[id]),0);
        lv_obj_set_style_border_width(b,1,0);
        lv_obj_set_style_border_color(b,lv_color_hex(skin_accent[id]),LV_STATE_PRESSED);
        x+=w+gap;
    }
    static const char *const key5[EXTRA_COUNT]={"RETRY","RESTART","RESTART","NEW","REFRESH","RESTART","POTION"};
    static const char *const key4[EXTRA_COUNT]={"UNDO","","JUMP","REPLAY","","BURST","GUARD"};
    lv_label_set_text(lv_obj_get_child(controls[4],0),key4[id]);
    lv_label_set_text(lv_obj_get_child(controls[5],0),key5[id]);
    if(id==5) {
        lv_label_set_text(lv_obj_get_child(controls[0],0),"FIRE");
        lv_label_set_text(lv_obj_get_child(controls[1],0),">");
        lv_label_set_text(lv_obj_get_child(controls[3],0),"<");
        lv_label_set_text(lv_obj_get_child(controls[5],0),"NEW");
    } else {
        /* The same six LVGL controls are reused across Arcade games. Beat
         * Raiders temporarily turns the UP key into FIRE; restore UP when
         * opening every other game, including Golf and Box Pusher. */
        lv_label_set_text(lv_obj_get_child(controls[0],0),"^");
        lv_label_set_text(lv_obj_get_child(controls[1],0),">");
        lv_label_set_text(lv_obj_get_child(controls[3],0),"<");
    }
}
static void apply_game_skin(int id) {
    lv_obj_set_style_bg_color(screen,lv_color_hex(skin_bg[id]),0);
    lv_obj_set_style_bg_opa(screen,LV_OPA_COVER,0);
    lv_obj_set_style_bg_color(board,lv_color_hex(skin_stage[id]),0);
    lv_obj_set_style_bg_opa(board,LV_OPA_COVER,0);
    lv_obj_set_style_border_width(board,2,0);
    lv_obj_set_style_border_color(board,lv_color_hex(skin_accent[id]),0);
    lv_obj_set_style_radius(board,(id==0||id==1||id==6)?0:6,0);
    lv_obj_set_style_text_color(title_label,lv_color_hex(skin_accent[id]),0);
    lv_obj_set_style_text_color(status,lv_color_hex(0xFFF0DD),0);
    lv_obj_set_style_bg_color(status,lv_color_hex(skin_bg[id]),0);
    lv_obj_set_style_text_color(help,lv_color_hex(0xD2C8BC),0);
    lv_label_set_text(page_heading,names[id]);
    lv_obj_set_style_text_font(page_heading,&font_ui_18,0);
    /* Sonix's generic settings heading reserves an unused right-hand action
     * slot. Arcade has none; reclaim it so longer translated names fit. */
    lv_obj_set_width(page_heading,acfg->screen_width-acfg->padding-54);
    lv_image_set_src(scene_badge,arcade_art_badge((unsigned)id+5));
    layout_game_controls(id);
}


static lv_obj_t *small_button(lv_obj_t *parent,const char *txt,int x,int y,int w,int h,lv_event_cb_t cb,void *user) {
    lv_obj_t *b=lv_btn_create(parent);
    lv_obj_set_pos(b,x,y);lv_obj_set_size(b,w,h);
    lv_obj_add_style(b,&theme_style_card,0);
    lv_obj_add_style(b,&theme_style_card_pressed,LV_STATE_PRESSED);
    lv_obj_set_style_opa(b,LV_OPA_80,LV_STATE_PRESSED);
    lv_obj_set_style_border_width(b,2,LV_STATE_PRESSED);
    lv_obj_set_style_pad_all(b,0,0);
    if(cb)lv_obj_add_event_cb(b,cb,LV_EVENT_CLICKED,user);
    lv_obj_t *lab=lv_label_create(b);lv_label_set_text(lab,txt);
    lv_obj_set_style_text_font(lab,&font_ui_14,0);
    lv_obj_add_style(lab,&theme_style_text,0);lv_obj_center(lab);
    return b;
}
static uint32_t seed_now(void) {return (uint32_t)time(NULL)^lv_tick_get();}
/* These 9x9 boards can redraw hundreds of labels a second. Reassigning an
 * unchanged LVGL label needlessly invalidates layout and may allocate glyph
 * buffers on the UI thread while FLAC/Qobuz playback needs CPU time. */
static void label_text_changed(lv_obj_t *label, const char *value) {
    const char *now=lv_label_get_text(label);
    const char *next=value?value:"";
    if(!now||strcmp(now,next)!=0)lv_label_set_text(label,next);
}
static void cell_text_color(int i, uint32_t rgb) {
    if(cell_text_color_cache[i]!=rgb) {
        lv_obj_set_style_text_color(labels[i],lv_color_hex(rgb),0);
        cell_text_color_cache[i]=rgb;
    }
}
static void setcell_core(int i,const char *text,uint32_t color,bool sprite_follows) {
    if(lv_obj_is_hidden(cells[i]))lv_obj_set_hidden(cells[i],false);
    if(cell_color_cache[i]!=color) {
        lv_obj_set_style_bg_color(cells[i],lv_color_hex(color),0);
        cell_color_cache[i]=color;
    }
    /* World art shines through free space; choice cards remain opaque. */
    int opa=(current==3||current==4)?LV_OPA_COVER:
            (text&&*text?LV_OPA_80:LV_OPA_50);
    if(cell_opa_cache[i]!=opa) {
        lv_obj_set_style_bg_opa(cells[i],opa,0);
        cell_opa_cache[i]=(int16_t)opa;
    }
    /* Do not hide then reveal a sprite every frame: that made the whole
     * character dirty twice even when it had not changed. */
    if(!sprite_follows && sprites[i] && !lv_obj_is_hidden(sprites[i]))
        lv_obj_set_hidden(sprites[i],true);
    label_text_changed(labels[i],text);
    if(current!=3)cell_text_color(i,0xFFFFFF);
}
static void setcell(int i,const char *text,uint32_t color) {
    animated_actor[i]=0;
    setcell_core(i,text,color,false);
}
static void setcell_char(int i,char c,uint32_t color) {
    const lv_image_dsc_t *motion=arcade_motion_frame(current,c,anim_frame);
    const lv_image_dsc_t *icon=motion?motion:arcade_art_glyph(current,c);
    if(!icon && c!=' ')icon=arcade_stage_tile(current,c);
    animated_actor[i]=motion?c:0;
    char buf[2]={c,0};
    setcell_core(i,icon?"":(c==' '||c=='#')?"":c?buf:"",color,true);
    if(!icon) {
        if(sprites[i]&&!lv_obj_is_hidden(sprites[i]))lv_obj_set_hidden(sprites[i],true);
        return;
    }
    bool created=false, changed=false;
    if(!sprites[i]) {
        sprites[i]=lv_image_create(cells[i]);created=true;
        lv_obj_set_clickable(sprites[i],false);
        lv_image_set_scale(sprites[i],(current==2||current==5)?512:256);
    }
    if(sprite_cache[i]!=icon) {
        lv_image_set_src(sprites[i],icon);
        sprite_cache[i]=icon;changed=true;
    }
    if(lv_obj_is_hidden(sprites[i]))lv_obj_set_hidden(sprites[i],false);
    if(created||changed)lv_obj_center(sprites[i]);
}
static void set_board_size(int w,int h) {
    if(columns==w && rows==h)return;
    columns=w;rows=h;
    int bw=lv_obj_get_width(board),bh=lv_obj_get_height(board);
    int cellw=bw/w,cellh=bh/h;
    for(int i=0;i<UI_CELLS;i++){
        if(i>=w*h){lv_obj_set_hidden(cells[i],true);continue;}
        int x=i%w,y=i/w;
        lv_obj_set_pos(cells[i],x*cellw+1,y*cellh+1);
        lv_obj_set_size(cells[i],cellw-2,cellh-2);
        lv_obj_set_hidden(cells[i],false);
        lv_obj_set_style_radius(cells[i],current==3?10:current==4?5:current==2?10:(current==5?2:0),0);
        if(sprites[i])lv_image_set_scale(sprites[i],(current==2||current==5)?512:256);
        lv_obj_set_style_border_width(cells[i],current==3||current==4?2:1,0);
        lv_obj_set_style_border_color(cells[i],lv_color_hex(skin_stage[current]),0);
        lv_obj_set_style_pad_all(cells[i],0,0);
        lv_obj_set_style_bg_opa(cells[i],LV_OPA_COVER,0);
        lv_obj_set_style_border_width(cells[i],0,0);
        lv_obj_set_style_text_align(labels[i],LV_TEXT_ALIGN_CENTER,0);
        lv_obj_set_style_text_font(labels[i],current==3?&font_ui_18:&font_ui_14,0);
        /* The font is selected once per game-layout change, not per render.
         * Song choices can be very long UTF-8 strings. Bound BOTH axes so
         * LVGL ellipsizes rather than drawing text over the next answer. */
        lv_obj_set_width(labels[i],cellw-6);
        if(current==4){
            lv_obj_set_height(labels[i],cellh-4);
            lv_label_set_long_mode(labels[i],LV_LABEL_LONG_DOT);
        }else{
            lv_obj_set_height(labels[i],LV_SIZE_CONTENT);
            lv_label_set_long_mode(labels[i],LV_LABEL_LONG_WRAP);
        }
        lv_obj_center(labels[i]);
    }
}
static void show_status(const char *txt,const char *guide) {
    label_text_changed(status,txt);
    label_text_changed(help,guide);
}
static int extra_score(void) {
    switch(current){case 0:return boxes.level+(boxes.won?1:0);
    case 1:return golf.won?1000-golf.strokes:0;
    case 2:return orbit.score;
    case 3:return melody.completed;
    case 4:return (int)quiz.points;
    case 5:return beat.score;
    case 6:return dungeon.gold+dungeon.floor*10;default:return 0;}
}
/* Place impact feedback at the actual player's world coordinate, not the
 * center of every game. Clamped so the 24px burst remains on the panel. */
static void scene_burst(void) {
    if(!scene_fx || current<0)return;
    int w=columns>0?columns:9,h=rows>0?rows:9;
    int gx=w/2,gy=h/2;
    switch(current){
    case 0:gx=boxes.x;gy=boxes.y;break;
    case 1:gx=golf.x;gy=golf.y;break;
    case 2:gx=orbit.phase%4;gy=orbit.phase/4;break;
    case 5:
        gx=beat.flash>0&&beat.flash_lane>=0?beat.flash_lane:beat.enemy_lane;
        gy=beat.flash>0&&beat.flash_row>=0?beat.flash_row:beat.enemy_row;
        break;
    case 6:gx=dungeon.x;gy=dungeon.y;break;
    default:break;
    }
    int pw=lv_obj_get_width(board),ph=lv_obj_get_height(board);
    int x=(2*gx+1)*pw/(2*w),y=(2*gy+1)*ph/(2*h);
    if(x<12)x=12;else if(x>pw-12)x=pw-12;
    if(y<12)y=12;else if(y>ph-12)y=ph-12;
    arcade_fx_burst(scene_fx,x,y,lv_color_hex(skin_accent[current]));
}
static void render(void) {
    char line[128];
    if(current!=5)label_text_changed(title_label,skin_tagline[current]);
    if(current==5){
        label_text_changed(lv_obj_get_child(controls[4],0),beat.burst_charge==100?"BLAST":"BURST");
        lv_obj_set_style_bg_color(controls[4],lv_color_hex(beat.burst_charge==100?0x9A6540:0x253C58),0);
    }
    if(current==6){
        lv_label_set_text(lv_obj_get_child(controls[4],0),(dungeon.pending_upgrade||dungeon.pending_relic)?"Pick":"Guard");
        lv_label_set_text(lv_obj_get_child(controls[5],0),dungeon.over?"New":"POT");
    } else if(current==0) {
        /* A single context-sensitive action: UNDO during play, GO on win. */
        label_text_changed(lv_obj_get_child(controls[4],0),boxes.won?"GO":"UNDO");
    }

    if(current==5){
        char sector[48];
        snprintf(sector,sizeof(sector),"BEAT RAIDERS  /  SECTOR %02d",beat.wave);
        label_text_changed(title_label,sector);
        beat_stage_render(&beat);
        return;
    }
    int w=9,h=9;
    if(current==2)w=h=4;
    if(current==3)w=h=2;
    /* Four long track titles need the full screen width, not 2x2 tiny tiles. */
    if(current==4){w=1;h=4;}
    if(current==5){w=3;h=6;}
    set_board_size(w,h);
    switch(current){
    case 0:
        snprintf(line,sizeof(line),"Stage %d/%d  Moves %d%s",boxes.level+1,PA_BOX_LEVELS,
                 boxes.moves,boxes.won?"  CLEARED!":"");
        show_status(line,boxes.won?(boxes.level+1==PA_BOX_LEVELS?"ALL CLEAR! GO to replay":"Tap GO for next stage"):
                    boxes.deadlocked?"CORNER TRAP! Tap UNDO":"Push crates onto the marked spots");
        for(int y=0;y<9;y++)for(int x=0;x<9;x++){
            char c=boxes.tiles[y][x];uint32_t col=((x+y)&1)?0xAD805F:0xBC8A67;
            if(c=='#')col=((x*3+y)&1)?0x655368:0x755C6A;
            else if(c=='.')col=0x2E9275;
            else if(c=='$'||c=='*')col=boxes.deadlocked && c=='$'?0xD9565D:0xD5A56C;
            if(x==boxes.x&&y==boxes.y){c='@';col=0x62BFC5;}
            if(c=='.')c='o';
            if(c=='*')c='O';
            setcell_char(y*9+x,c,col);
        } break;
    case 1:
        snprintf(line,sizeof(line),"Hole %d/3  Strokes %d  Stars %d",golf.course+1,golf.strokes,golf.stars);
        show_status(line,golf.won?"Course complete! 9 stars = perfect":"Ghost dots show shot landing spots");
        /* Four shot previews depend only on the current ball position.
         * Compute them once, rather than 9*9*4 times per redraw. */
        int preview_x[4],preview_y[4];
        bool preview_valid[4];
        for(int d=0;d<4;d++)
            preview_valid[d]=pa_golf_preview(&golf,(pa_dir_t)d,&preview_x[d],&preview_y[d]);
        for(int y=0;y<9;y++)for(int x=0;x<9;x++){
            char c=pa_golf_tile(&golf,x,y);uint32_t col= c=='#'?0x34513E:((x+y)&1?0x73A06B:0x66965D);
            if(c=='O')col=0xb09b52;
            for(int d=0;d<4;d++){
                if(preview_valid[d]&&preview_x[d]==x&&preview_y[d]==y&&c==' '){
                    c='.';col=0x448eb0;
                }
            }
            if(x==golf.x&&y==golf.y){c='o';col=0xe2e2ed;}
            setcell_char(y*9+x,c,col);
        } break;
    case 2:
        snprintf(line,sizeof(line),"Orbit %d HP%d  x%d  %d pts",orbit.level+1,orbit.lives,orbit.perfect_streak,orbit.score);
        show_status(line,orbit.over?"Game over! New retries":"Center hit = PERFECT combo +8!");
        for(int i=0;i<16;i++){
            uint32_t col= i==orbit.window?0xE2A66C:((i%4+i/4)&1?0x332C69:0x282359);
            if(i==orbit.phase)col=0x54c9eb;
            setcell_char(i,i==orbit.phase?'O':i==orbit.window?'+':' ',col);
        } break;
    case 3: {
        if(melody.over)snprintf(line,sizeof(line),"Round %d  No hearts left",melody.length);
        else if(melody.won)snprintf(line,sizeof(line),"ALL %d ROUNDS CLEARED!",PA_MELODY_MAX);
        else if(melody_show_step)snprintf(line,sizeof(line),"WATCH %d/%d  Hearts %d",melody_show_step,melody.length,melody.hearts);
        else snprintf(line,sizeof(line),"REPEAT %d/%d  Hearts %d",melody.position+1,melody.length,melody.hearts);
        show_status(line,melody.over?"New retries. Repeat the lights":
                    melody.won?"Memory champion!":"Repeat the lights. GO replays");
        static const char *nt[]={"1 C","2 D","3 E","4 G"};
        static const uint32_t colors[]={0x3CBBD8,0xE7AD62,0x59C69C,0xAD81E2};
        for(int i=0;i<4;i++){
            bool lit=(melody_show_index==i||melody_feedback_pad==i);
            setcell(i,nt[i],lit?0xffe29a:colors[i]);
            cell_text_color(i,lit?0x162236:0xFFFFFF);
        }
        break; }
    case 4: {
        char quiz_line[112];
        snprintf(quiz_line,sizeof(quiz_line),"%s  %u pts x%u",song_status,(unsigned)quiz.points,
                 (unsigned)quiz.streak);
        show_status(quiz_line,quiz.correct<0?"Play a NEW track to unlock":"Choose title; new track = new quiz");
        for(int i=0;i<4;i++){
            uint32_t color=quiz.selected<0?0x344d66:i==quiz.correct?0x3c846c:i==quiz.selected?0x965461:0x344d66;
            char short_title[36];
            /* 24 glyphs stay within two rows even for full-width CJK titles. */
            pa_utf8_preview(short_title,sizeof(short_title),quiz.options[i][0]?quiz.options[i]:"--",24);
            setcell(i,short_title,color);
        } break; }
    case 6: {
        snprintf(line,sizeof(line),"F%d HP%d/%d W%d Focus%d %s",dungeon.floor,dungeon.hp,
                 dungeon.max_hp,dungeon.weapon,dungeon.focus,dungeon.relic==1?"FANG":dungeon.relic==2?"WARD":"");
        const char *guide="Swipe into foe; GO shields";
        if(dungeon.pending_relic)
            guide=dungeon.relic_choice==0?"< FANG heal/2 kills  > WARD  GO":"< FANG  > WARD shield/floor  GO";
        else if(dungeon.pending_upgrade)
            guide=dungeon.upgrade_choice==0?"< HEART +HP  > BLADE  GO":"< HEART  > BLADE +ATK  GO";
        else if(dungeon.over)guide="Run over. New starts again";
        else if(dungeon.last_event==PA_DUNGEON_RELIC)
            guide="Relic acquired! Paid 1 max HP";
        else if(dungeon.last_event==PA_DUNGEON_ALTAR)
            guide="Choose relic; costs 1 max HP";
        else if(dungeon.last_event==PA_DUNGEON_HURT)
            guide="Hit! GO guard, POT heals";
        else if(dungeon.last_event==PA_DUNGEON_KILL)
            guide="Defeated! Focus restored";
        else if(dungeon.last_event==PA_DUNGEON_LOOT)
            guide="Treasure found! Gold +8";
        else if(dungeon.last_event==PA_DUNGEON_STAIRS)
            guide="Choose an upgrade: < or >";
        else if(dungeon.last_event==PA_DUNGEON_SEALED)
            guide="Stairs sealed! Beat a foe";
        else if(dungeon.last_event==PA_DUNGEON_HEAL)
            guide="Healed. Save potions for later";
        if(cached_dungeon_hp>=0 && dungeon.hp<cached_dungeon_hp)arcade_art_pulse(status);
        cached_dungeon_hp=dungeon.hp;
        show_status(line,guide);
        for(int y=0;y<9;y++)for(int x=0;x<9;x++){
            char c=dungeon.tiles[y][x];uint32_t col=((x+y)&1)?0x665465:0x594A5C;
            if(c=='#')col=((x*7+y)&1)?0x38384E:0x44405A;
            else if(c=='r')col=0x9c5760;
            else if(c=='G')col=0x886dc6;
            else if(c=='B')col=0xb8753c;
            else if(c=='W')col=0xad5ca4;
            else if(c=='+')col=0x3d876b;
            else if(c=='!')col=0xe9b55a;
            else if(c=='$')col=0x9b8942;
            else if(c=='>')col=0xc49b51;
            if(x==dungeon.x&&y==dungeon.y){c=dungeon.guard?'S':'@';col=0x5dbbdc;}
            setcell_char(y*9+x,c,col);
        }
        break; }
    }
}
/* Song trivia samples a bounded set of titles from the actual local library.
 * No playback control: the audible question is the track ALREADY playing. */
typedef struct { char title[128][256];int n; } quiz_pool_t;
static bool quiz_cb(const char *name,const char *path,const char *artist,void *arg) {
    (void)path;(void)artist;
    quiz_pool_t *p=(quiz_pool_t*)arg;
    /* Scan until 128 DISTINCT titles, not 128 files. Compilation albums and
     * duplicate tracks otherwise starve the multiple-choice question. */
    return pa_quiz_pool_add(p->title,&p->n,128,name);
}
static void quiz_new(void){
    device_state_t state;device_state_get(&state);
    quiz_question_title[0]=0;
    quiz_question_artist[0]=0;
    if(!state.metadata.title[0]){
        quiz.correct=-1;quiz.selected=-1;
        for(int i=0;i<4;i++)quiz.options[i][0]=0;
        snprintf(song_status,sizeof(song_status),"Play a song first");return;
    }
    if(quiz_last_answered[0] && strcmp(quiz_last_answered,state.metadata.title)==0 &&
       strcmp(quiz_last_artist,state.metadata.artist)==0){
        quiz.correct=-1;quiz.selected=-1;
        for(int i=0;i<4;i++)quiz.options[i][0]=0;
        snprintf(song_status,sizeof(song_status),"Already played: skip to a new song");return;
    }
    /* 128 * 256 titles = 32 KiB: never put this on the UI thread stack.
     * Use checked temporary heap memory and free immediately after sampling. */
    quiz_pool_t *p=(quiz_pool_t*)calloc(1,sizeof(*p));
    if(!p){
        quiz.correct=-1;quiz.selected=-1;
        for(int i=0;i<4;i++)quiz.options[i][0]=0;
        snprintf(song_status,sizeof(song_status),"Not enough memory for quiz");return;
    }
    if(library_is_open())library_for_each(LIBRARY_LIST_TRACKS,LIBRARY_FILTER_NONE,NULL,quiz_cb,p);
    bool built=pa_quiz_next(&quiz,state.metadata.title,
                      (const char (*)[PA_QUIZ_TITLE])p->title,p->n,seed_now());
    free(p);
    if(!built){
        quiz.correct=-1;quiz.selected=-1;
        for(int i=0;i<4;i++)quiz.options[i][0]=0;
        snprintf(song_status,sizeof(song_status),"Need 4 unique library titles");return;
    }
    snprintf(quiz_question_title,sizeof(quiz_question_title),"%s",state.metadata.title);
    snprintf(quiz_question_artist,sizeof(quiz_question_artist),"%s",state.metadata.artist);
    snprintf(song_status,sizeof(song_status),"Which song is playing?");
}
static void play_melody(void);
/* Slower demonstration than R22: sufficient time to recognize each pad
 * without changing the deterministic sequence, scoring or game engine. */
static uint32_t melody_flash_ms(void){return melody.length<=8?580u:melody.length<=16?490u:410u;}
static uint32_t melody_gap_ms(void){return melody.length<=8?220u:melody.length<=16?200u:180u;}
static void reset_current(void) {
    if(tick_timer)lv_timer_pause(tick_timer);
    melody_show_step=0;melody_show_index=-1;melody_feedback_pad=-1;melody_retry_pending=false;quiz.selected=-1;
    memset(&melody_demo,0,sizeof(melody_demo));
    switch(current){
    case 0:pa_boxes_start_level(&boxes, (int)best[0]);break;
    case 1:pa_golf_reset_seed(&golf,seed_now());break;
    case 2:pa_orbit_reset(&orbit);break;
    case 3:pa_melody_reset(&melody,seed_now());break;
    case 4:quiz_new();break;
    case 5:pa_beat_reset(&beat,seed_now());beat_last_onset=0;beat_fire_held=false;break;
    case 6:pa_dungeon_reset(&dungeon,seed_now());break;
    }
    if(tick_timer&&(current==2||current==5)&&power_screen_is_on()){
        lv_timer_set_period(tick_timer,current==2?pa_orbit_period_ms(&orbit):pa_beat_period_ms(&beat));
        lv_timer_resume(tick_timer);
    }
    render();
    if(current==3)play_melody(); /* first round starts with an automatic demo */
}
static void play_melody(void) {
    if(current!=3||melody.over||melody.won||!power_screen_is_on())return;
    melody_feedback_pad=-1;
    pa_melody_demo_start(&melody_demo,&melody);
    melody_show_step=melody_demo.shown;melody_show_index=melody_demo.index;
    if(tick_timer){
        /* LVGL timers are reused between games and rounds. Reset last_run
         * so the opening note receives its FULL interval, not the remainder
         * of a previous 180ms feedback timer or paused countdown. */
        lv_timer_set_period(tick_timer,melody_flash_ms());
        lv_timer_reset(tick_timer);
        lv_timer_resume(tick_timer);
    }
    render();
}
static void input_direction(pa_dir_t dir) {
    int before=extra_score();
    int old_hp=dungeon.hp;
    int old_moves=boxes.moves,old_strokes=golf.strokes;
    if(current==0)pa_boxes_move(&boxes,dir);
    else if(current==1)pa_golf_shot(&golf,dir);
    else if(current==5){
        if(dir==PA_UP)pa_beat_fire(&beat);
        else if(dir==PA_LEFT||dir==PA_RIGHT)pa_beat_steer(&beat,dir==PA_LEFT?-1:1);
    }
    else if(current==6){
        if(dungeon.pending_relic){
            if(dir==PA_LEFT)dungeon.relic_choice=0;
            else if(dir==PA_RIGHT)dungeon.relic_choice=1;
        }else if(dungeon.pending_upgrade){
            if(dir==PA_LEFT)dungeon.upgrade_choice=0;
            else if(dir==PA_RIGHT)dungeon.upgrade_choice=1;
        }else pa_dungeon_move(&dungeon,dir);
    }
    render();
    if(extra_score()>before || (current==6 && dungeon.hp<old_hp) ||
       (current==0&&boxes.moves>old_moves) || (current==1&&golf.strokes>old_strokes))
        scene_burst();
}
static void input_action(void) {
    int before=extra_score();
    if(current==0) {
        if(boxes.won){
            if(!pa_boxes_next(&boxes)){
                /* GO after the eighth victory starts a fresh campaign.
                 * Save the completion before resetting the visible score. */
                if(best[0]<PA_BOX_LEVELS){
                    best[0]=PA_BOX_LEVELS;
                    config_set_int("pocket_arcade",config_names[0],best[0]);
                    config_save();
                }
                pa_boxes_start_level(&boxes,0);
            }
        }else pa_boxes_undo(&boxes);
    }
    else if(current==2)pa_orbit_jump(&orbit);
    else if(current==3)play_melody();
    else if(current==5)pa_beat_burst(&beat);
    else if(current==6){
        if(dungeon.pending_relic)pa_dungeon_take_relic(&dungeon,dungeon.relic_choice);
        else if(dungeon.pending_upgrade)pa_dungeon_upgrade(&dungeon,dungeon.upgrade_choice);
        else pa_dungeon_guard(&dungeon);
    }
    render();
    if(extra_score()>before)scene_burst();
}
/* A held FIRE key shoots when the cooldown permits. Tap still works,
 * hardware music playback remains independent, and releasing stops firing. */
static void beat_fire_hold_cb(lv_event_t *e){
    if(current!=5)return;
    lv_event_code_t code=lv_event_get_code(e);
    if(code==LV_EVENT_PRESSED)beat_fire_held=true;
    else if(code==LV_EVENT_RELEASED||code==LV_EVENT_PRESS_LOST)beat_fire_held=false;
}
static void control_cb(lv_event_t *e) {
    if (player_sheet_drag_active()) return;
    int action=(int)(intptr_t)lv_event_get_user_data(e);
    if(action>=0&&action<4)input_direction((pa_dir_t)action);
    else if(action==4)input_action();
    else if(action==5) {
        if(current==0){pa_boxes_restart_level(&boxes);render();}
        else if(current==6&&!dungeon.over){pa_dungeon_use_potion(&dungeon);render();}
        else reset_current();
    }
}
static void cell_cb(lv_event_t *e) {
    if (player_sheet_drag_active()) return;
    int i=(int)(intptr_t)lv_event_get_user_data(e);
    if(current==3&&i<4&&!melody_show_step){
        int before=melody.completed;
        if(pa_melody_press(&melody,i)){
            if(melody.completed>(int)best[3])best[3]=melody.completed;
            if(melody.completed>before&&!melody.won){scene_burst();play_melody();return;}
            melody_feedback_pad=i;
        }else {
            melody_feedback_pad=i;
            if(!melody.over)melody_retry_pending=true;
        }
        if(tick_timer&&!melody.over&&!melody.won){
            lv_timer_set_period(tick_timer,180);
            lv_timer_reset(tick_timer);
            lv_timer_resume(tick_timer);
        }
    } else if(current==4&&i<4&&quiz.correct>=0&&quiz.selected<0){
        device_state_t ds;device_state_get(&ds);
        /* Never score a question about a track that is no longer playing. */
        if(strcmp(ds.metadata.title,quiz_question_title)!=0 ||
           strcmp(ds.metadata.artist,quiz_question_artist)!=0){
            quiz_new();
            render();
            return;
        }
        bool right=pa_quiz_select(&quiz,i);
        snprintf(quiz_last_answered,sizeof(quiz_last_answered),"%s",ds.metadata.title);
        snprintf(quiz_last_artist,sizeof(quiz_last_artist),"%s",ds.metadata.artist);
        if(quiz.points>best[4])best[4]=quiz.points;
        snprintf(song_status,sizeof(song_status),right?"Great ears!":"Green is correct");
        arcade_art_pulse(status);
    } else if(current==2)input_action();
    else return;
    render();
    if(current==3||current==4)scene_burst();
}
static void board_touch_cb(lv_event_t *e) {
    lv_event_code_t code=lv_event_get_code(e);
    lv_indev_t *indev=lv_indev_active();if(!indev)return;
    lv_point_t p;lv_indev_get_point(indev,&p);
    if(code==LV_EVENT_PRESSED){swipe_origin=p;swipe_armed=true;}
    else if(code==LV_EVENT_PRESS_LOST)swipe_armed=false;
    else if(code==LV_EVENT_RELEASED&&swipe_armed){
        swipe_armed=false;
        if (player_sheet_drag_active()) return;
        int dx=p.x-swipe_origin.x,dy=p.y-swipe_origin.y;
        int ax=abs(dx), ay=abs(dy);
        if(ax<ARCADE_SWIPE_MIN_PX && ay<ARCADE_SWIPE_MIN_PX){
            if(current==5){
                lv_area_t area;lv_obj_get_coords(board,&area);
                int relative=p.x-area.x1;
                int width=lv_obj_get_width(board);
                if(relative>=0&&relative<width){
                    int lane=relative*3/width;
                    beat.player_lane=lane;
                    pa_beat_fire(&beat);
                    render();
                }
            }
            return;
        }
        /* Reject diagonal gestures that can accidentally move a player
         * when dismissing a card or scrolling a page. */
        if(ax<ay*ARCADE_SWIPE_AXIS_RATIO && ay<ax*ARCADE_SWIPE_AXIS_RATIO)return;
        pa_dir_t d=ax>ay?(dx>0?PA_RIGHT:PA_LEFT):(dy>0?PA_DOWN:PA_UP);
        input_direction(d);
    }
}
static void tick_cb(lv_timer_t *timer) {
    if(!power_screen_is_on() || player_sheet_is_open() || player_sheet_drag_active())return;
    int before=extra_score();
    int old_lives=beat.lives;
    if(current==2){
        pa_orbit_tick(&orbit);
        if(orbit.over)lv_timer_pause(timer);
        else lv_timer_set_period(timer,pa_orbit_period_ms(&orbit));
    }
    else if(current==5){
        arcade_music_snapshot_t meter=arcade_music_snapshot();
        bool music=(audio_get_status()==AUDIO_STATUS_PLAYING && meter.valid);
        bool onset=music && meter.onset!=beat_last_onset;
        beat_last_onset=meter.onset;
        pa_beat_music(&beat,music,meter.level,meter.bass,onset);
        if(beat_fire_held)pa_beat_fire(&beat);
        pa_beat_step(&beat);
        if(beat.over)lv_timer_pause(timer);
        else{
            /* Engine-owned pace curve; verified by native fairness tests. */
            lv_timer_set_period(timer,pa_beat_period_ms(&beat));
        }
    }
    else if(current==3){
        if(melody_retry_pending){
            melody_retry_pending=false;
            play_melody();return;
        }
        if(melody_show_step){
            pa_melody_demo_tick(&melody_demo,&melody);
            melody_show_step=melody_demo.shown;melody_show_index=melody_demo.index;
            if(melody_demo.finished)lv_timer_pause(timer);
            else if(melody_show_index<0)lv_timer_set_period(timer,melody_gap_ms());
            else lv_timer_set_period(timer,melody_flash_ms());
        }else{
            melody_feedback_pad=-1;
            lv_timer_pause(timer);
        }
    }
    render();
    if(current==5&&beat.lives<old_lives)
        arcade_fx_burst(scene_fx,lv_obj_get_width(board)/2,lv_obj_get_height(board)-18,lv_color_hex(0xFF627F));
    else if(extra_score()>before)scene_burst();
}
/* Animation is deliberately selective: only occupied actor sprites update.
 * No full-board paint and no work while the display is dark. */
static void actors_tick_cb(lv_timer_t *timer){
    (void)timer;
    if(current<0 || !power_screen_is_on() || player_sheet_is_open() || player_sheet_drag_active())return;
    anim_frame^=1u;
    int count=columns*rows;
    if(count<0||count>UI_CELLS)count=UI_CELLS;
    for(int i=0;i<count;i++){
        if(!animated_actor[i]||!sprites[i]||lv_obj_is_hidden(sprites[i]))continue;
        const lv_image_dsc_t *next=arcade_motion_frame(current,animated_actor[i],anim_frame);
        if(next && sprite_cache[i]!=next){
            lv_image_set_src(sprites[i],next);sprite_cache[i]=next;
        }
    }
}
static void on_unload(lv_event_t *e){
    (void)e;swipe_armed=false;beat_fire_held=false;
    arcade_music_set_active(false);
    if(tick_timer)lv_timer_pause(tick_timer);
    if(actor_timer)lv_timer_pause(actor_timer);
    /* No persistent write for an unchanged score. Leaving a cabinet is
     * common while listening to music, and config_save may synchronously
     * flush data to storage on the GUI thread. */
    int score=extra_score();
    if(score>=0 && (uint32_t)score>best[current]){
        best[current]=(uint32_t)score;
        config_set_int("pocket_arcade",config_names[current],best[current]);
        config_save();
    }
}
static void on_loaded(lv_event_t *e){
    (void)e;
    arcade_music_set_active(current==5);
    /* Melody and Song Quiz have no animated board actors. Leave the shared
     * 210ms sprite timer asleep while their UI is visible. */
    if(actor_timer&&power_screen_is_on()&&current!=3&&current!=4)lv_timer_resume(actor_timer);
    if(tick_timer&&power_screen_is_on()&&((current==2&&!orbit.over)||(current==5&&!beat.over)||
        (current==3&&melody_show_step))){
        /* When opening Melody's screen the first lit pad must remain visible
         * for a whole flash interval after the screen becomes visible. */
        if(current==3 && melody_show_step)lv_timer_reset(tick_timer);
        lv_timer_resume(tick_timer);
    }
}
void arcade_extra_init(gui_config_t *cfg){ acfg=cfg; }
static void build(void) {
    int pad=acfg->padding;
    int top=settingsrow_content_top(acfg);
    int width=acfg->screen_width-2*pad;
    /* Reserve a real footer: status, help and transport must not overlap. */
    /* Reserve a readable 22px second header line above the board. R22's
     * 18px spacing caused overlapping labels in the Box Pusher screenshot. */
    int header_room=6;
    int board_size=acfg->screen_height-top-105-header_room;
    if(board_size>width)board_size=width;
    if(board_size<96)board_size=96;
    screen=lv_obj_create(NULL);
    lv_obj_add_style(screen,&theme_style_screen,0);
    page_heading=settingsrow_title(screen,acfg,"Arcade");
    scene_badge=lv_image_create(screen);
    lv_obj_set_pos(scene_badge,acfg->screen_width-pad-27,top-43);
    lv_obj_set_clickable(scene_badge,false);
    title_label=lv_label_create(screen);
    lv_obj_add_style(title_label,&theme_style_text,0);
    lv_obj_set_style_text_font(title_label,&font_ui_14,0);
    lv_obj_set_pos(title_label,pad,top-3);
    lv_obj_set_width(title_label,width);
    lv_label_set_long_mode(title_label,LV_LABEL_LONG_DOT);
    status=lv_label_create(screen);
    lv_obj_add_style(status,&theme_style_text,0);
    lv_obj_set_style_text_font(status,&font_ui_14,0);
    lv_obj_set_pos(status,pad,top+19);
    lv_obj_set_width(status,width);
    lv_obj_set_style_bg_color(status,lv_color_hex(0x30222B),0);
    lv_obj_set_style_bg_opa(status,LV_OPA_COVER,0);
    lv_label_set_long_mode(status,LV_LABEL_LONG_DOT);
    board=lv_obj_create(screen);
    lv_obj_set_pos(board,(acfg->screen_width-board_size)/2,top+36+header_room);
    lv_obj_set_size(board,board_size,board_size);
    lv_obj_set_style_pad_all(board,0,0);
    lv_obj_set_style_border_width(board,0,0);
    lv_obj_set_style_bg_opa(board,LV_OPA_TRANSP,0);
    lv_obj_set_scrollable(board,false);
    lv_obj_set_clickable(board,true);
    lv_obj_add_event_cb(board,board_touch_cb,LV_EVENT_PRESSED,NULL);
    lv_obj_add_event_cb(board,board_touch_cb,LV_EVENT_RELEASED,NULL);
    lv_obj_add_event_cb(board,board_touch_cb,LV_EVENT_PRESS_LOST,NULL);
    scene_backdrop=arcade_scene_attach(board,5,board_size,board_size);
    for(int i=0;i<UI_CELLS;i++){
        cells[i]=lv_btn_create(board);
        lv_obj_set_clickable(cells[i],false);
        lv_obj_set_style_shadow_width(cells[i],0,0);
        lv_obj_set_style_opa(cells[i],LV_OPA_80,LV_STATE_PRESSED);
        lv_obj_set_style_pad_all(cells[i],0,0);
        lv_obj_set_style_bg_opa(cells[i],LV_OPA_COVER,0);
        labels[i]=lv_label_create(cells[i]);
        lv_obj_set_style_text_color(labels[i],lv_color_white(),0);
        lv_obj_set_style_text_font(labels[i],&font_ui_14,0);
        lv_obj_set_style_text_align(labels[i],LV_TEXT_ALIGN_CENTER,0);
        lv_obj_add_event_cb(cells[i],cell_cb,LV_EVENT_CLICKED,(void*)(intptr_t)i);
        /* Sprite objects are created lazily only for occupied symbol cells. */
    }
    help=lv_label_create(screen);
    lv_obj_add_style(help,&theme_style_text_dim,0);
    lv_obj_set_style_text_font(help,&font_ui_14,0);
    lv_obj_set_width(help,width);
    lv_obj_set_pos(help,pad,top+board_size+39+header_room);
    lv_label_set_long_mode(help,LV_LABEL_LONG_DOT);
    int footer=acfg->screen_height-pad-30;
    static const char *keys[]={"^",">","v","<","GO","New"};
    for(int i=0;i<6;i++)controls[i]=small_button(screen,keys[i],pad+i*(width/6),
                                    footer,width/6-2,25,control_cb,(void*)(intptr_t)i);
    tick_timer=lv_timer_create(tick_cb,160,NULL);
    lv_timer_pause(tick_timer);
    power_pause_in_standby(tick_timer);
    actor_timer=lv_timer_create(actors_tick_cb,210,NULL);
    lv_timer_pause(actor_timer);
    power_pause_in_standby(actor_timer);
    scene_fx=arcade_fx_create(board);
    beat_stage_create(board);
    lv_obj_add_event_cb(controls[0],beat_fire_hold_cb,LV_EVENT_PRESSED,NULL);
    lv_obj_add_event_cb(controls[0],beat_fire_hold_cb,LV_EVENT_RELEASED,NULL);
    lv_obj_add_event_cb(controls[0],beat_fire_hold_cb,LV_EVENT_PRESS_LOST,NULL);
    lv_obj_add_event_cb(screen,on_unload,LV_EVENT_SCREEN_UNLOADED,NULL);
    lv_obj_add_event_cb(screen,on_loaded,LV_EVENT_SCREEN_LOADED,NULL);
    pocket_arcade_enable_nowplaying_swipe(screen);
}
void arcade_extra_open(int id){
    if(!acfg||id<0||id>=EXTRA_COUNT)return;
    if(!screen)build();
    current=id;
    /* Diagnostic for WSL: makes it unambiguous whether the Neon stage
     * binary is running, rather than an older 3x6 tile renderer. */
    if(id==5)fprintf(stderr,"arcade: ARCADE_R22_6_1_STAGE neon Beat Raiders renderer selected\n");
    /* Only pay the audio sampling cost while Beat Raiders is visible. */
    arcade_music_set_active(id==5);
    apply_game_skin(id);
    /* Quiz and Melody need wide panels. The 9x9 games keep square pixel grids.
     * Force a fresh cell layout when their shape or width changes. */
    int square=acfg->screen_height-settingsrow_content_top(acfg)-111;
    int available_width=acfg->screen_width-2*acfg->padding;
    if(square>available_width)square=available_width;
    if(square<96)square=96;
    int board_y=id==5?settingsrow_content_top(acfg)+17:settingsrow_content_top(acfg)+42;
    int wide=id==5?available_width:(id==3||id==4?available_width:square);
    int tall=id==5?acfg->screen_height-(settingsrow_content_top(acfg)+17)-(acfg->padding+40):square;
    if(id==3) {
        /* The four Memory pads were 2x2 cells in a board taller than the
         * space between the subtitle and the help/footer. On 240x320 the
         * lower pair was clipped by the footer. Derive the height from the
         * ACTUAL help label instead of guessing an available square. */
        int clearance=lv_obj_get_y(help)-board_y-6;
        if(clearance<80)clearance=80; /* short displays: still usable */
        if(clearance>152)clearance=152;
        tall=clearance;
        wide=tall*4/3; /* compact landscape synth pads, 2 columns x 2 rows */
        if(wide>available_width)wide=available_width;
    } else if(tall<132) tall=132;
    lv_obj_set_size(board,wide,tall);
    lv_obj_set_x(board,(acfg->screen_width-wide)/2);
    lv_obj_set_y(board,board_y);
    arcade_scene_update(scene_backdrop,id+5,wide,tall);
    beat_stage_show(id==5);
    lv_obj_set_hidden(status,id==5);
    lv_obj_set_hidden(help,id==5);
    if(id==5){
        for(int i=0;i<UI_CELLS;i++)lv_obj_set_hidden(cells[i],true);
        lv_obj_set_hidden(scene_backdrop,true);
        lv_obj_set_hidden(scene_fx,true);
        beat_stage_resize(wide,tall);
    }else{
        lv_obj_set_hidden(scene_backdrop,false);
        lv_obj_set_hidden(scene_fx,false);
    }
    memset(animated_actor,0,sizeof(animated_actor));
    cached_beat_points=-1;cached_dungeon_hp=-1;
    columns=-1;rows=-1;
    /* Entering a new cabinet can reconfigure the same 81 persistent cells. */
    for(int i=0;i<UI_CELLS;i++) {
        cell_color_cache[i]=UINT32_MAX;
        cell_text_color_cache[i]=UINT32_MAX;
        cell_opa_cache[i]=-1;
    }
    best[id]=(uint32_t)config_get_int("pocket_arcade",config_names[id],0);
    lv_obj_t *go=lv_obj_get_child(controls[4],0);
    lv_obj_t *last=lv_obj_get_child(controls[5],0);
    lv_label_set_text(go,id==3?"REPLAY":id==6?"GUARD":id==2?"JUMP":id==0?"UNDO":"GO");
    lv_label_set_text(last,id==6?"POTION":id==0?"RETRY":"RESTART");
    reset_current();
    /* Layout uses control keys for ordinary games, but four large clickable
     * cells for Melody and Quiz. The others route swipes to the board. */
    for(int i=0;i<UI_CELLS;i++)lv_obj_set_clickable(cells[i],(id==3||id==4)&&i<4);
    switch_screen(screen);
}
