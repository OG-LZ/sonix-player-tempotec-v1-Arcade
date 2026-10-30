/* Pocket Arcade: 12 music-friendly mini-games for the TempoTec V1.
 * All LVGL state belongs to the GUI thread. Game timers must pause in standby;
 * input and display code must not acquire or alter Sonix audio playback.
 * GPL-3.0-or-later.
 */
#include "pocket_arcade.h"
#include "arcade2048.h"
#include "arcade_stack.h"
#include "arcade_racer.h"
#include "arcade_bricks.h"
#include "arcade_mines.h"
#include "arcade_extra_ui.h"
#include "dungeon_deluxe_ui.h"
#include "arcade_art.h"
#include "arcade_stage_art.h"
#include "arcade_scenery.h"
#include "arcade_fx.h"
#include <inttypes.h>
#include <stdint.h>
#include <time.h>
#include <stdio.h>

#include "src/gui/fonts/fonts.h"
#include "src/gui/shell/settingsrow.h"
#include "src/gui/shell/switcher.h"
#include "src/gui/nowplaying/player.h"
#include "src/gui/shell/theme.h"
#include "src/system/core/config.h"
#include "src/system/device/power.h"

lv_obj_t *pocket_arcade_screen;
lv_obj_t *arcade2048_screen;
static gui_config_t *arcade_cfg;

static arcade2048_t game;
static arcade2048_t undo_snapshot;
static bool can_undo;
static lv_obj_t *board, *fusion_fx;
static lv_obj_t *tiles[16];
static lv_obj_t *tile_labels[16];
static lv_obj_t *score_label;
static lv_obj_t *hint_label;
static uint32_t best_score;
static uint32_t saved_best_score;
static lv_point_t swipe_start;
static bool swipe_tracking;
static bool game_initialized;

/* R22.6.4: a touch may open Now Playing ONLY if it BEGINS in the
 * rightmost 6 physical pixels.  Do not attach the Sonix player-sheet handler
 * to screen or propagate all child events: that broad R22.6.2 listener was
 * still active alongside the R22.6.3 edge receiver and stole game gestures.
 *
 * LVGL retains the edge object as the pressed target as the finger moves
 * left, so the rest of the screen remains free for gameplay.  Use the same
 * native sheet handler to keep music transport/animations untouched. */
#define ARCADE_PLAYER_EDGE_PX 6

void pocket_arcade_enable_nowplaying_swipe(lv_obj_t *screen) {
    if (!screen || !arcade_cfg) return;
    const int width = arcade_cfg->screen_width;
    const int height = arcade_cfg->screen_height;
    const int top = 42;          /* below the header and status indicators */
    const int bottom = height - 44; /* clear of all bottom game controls */
    if (width < ARCADE_PLAYER_EDGE_PX || bottom <= top) return;

    /* Keep the launcher vertically scrollable; games have no scrolling root. */
    if (screen != pocket_arcade_screen) lv_obj_set_scrollable(screen, false);

    lv_obj_t *edge = lv_obj_create(screen);
    lv_obj_set_pos(edge, width - ARCADE_PLAYER_EDGE_PX, top);
    lv_obj_set_size(edge, ARCADE_PLAYER_EDGE_PX, bottom - top);
    lv_obj_set_style_bg_opa(edge, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(edge, 0, 0);
    lv_obj_set_style_outline_width(edge, 0, 0);
    lv_obj_set_style_shadow_width(edge, 0, 0);
    lv_obj_set_style_pad_all(edge, 0, 0);
    lv_obj_set_style_radius(edge, 0, 0);
    lv_obj_set_scrollable(edge, false);
    lv_obj_set_clickable(edge, true);
    lv_obj_remove_flag(edge, LV_OBJ_FLAG_EVENT_BUBBLE);
    player_sheet_attach_drag(edge, true);
    lv_obj_move_foreground(edge);
    fprintf(stderr, "arcade: ARCADE_R22_6_4_EDGE rightmost %dpx only\n", ARCADE_PLAYER_EDGE_PX);
}

static int minimum(int a, int b) { return a < b ? a : b; }

/* Original five game cabinets: the theme follows each game, not the other way
 * around. Style changes occur at construction, never in the music thread. */
static void cabinet_skin(lv_obj_t *screen, uint32_t bg, lv_obj_t *arena, uint32_t accent, uint32_t ground) {
    lv_obj_set_style_bg_color(screen,lv_color_hex(bg),0);
    lv_obj_set_style_bg_opa(screen,LV_OPA_COVER,0);
    if(arena) {
        lv_obj_set_style_bg_color(arena,lv_color_hex(ground),0);
        lv_obj_set_style_bg_opa(arena,LV_OPA_COVER,0);
        lv_obj_set_style_border_width(arena,2,0);
        lv_obj_set_style_border_color(arena,lv_color_hex(accent),0);
    }
}

static lv_color_t tile_color(uint32_t value) {
    switch (value) {
    case 0:    return lv_color_hex(0x392E46);
    case 2:    return lv_color_hex(0x627DA7);
    case 4:    return lv_color_hex(0x6A9CA7);
    case 8:    return lv_color_hex(0x6DAF9B);
    case 16:   return lv_color_hex(0xCC9B68);
    case 32:   return lv_color_hex(0xa38247);
    case 64:   return lv_color_hex(0xb77543);
    case 128:  return lv_color_hex(0xb45d67);
    case 256:  return lv_color_hex(0xa45490);
    case 512:  return lv_color_hex(0x875aa9);
    case 1024: return lv_color_hex(0x6a65bc);
    default:   return lv_color_hex(0x5c71d5);
    }
}

static void save_high_score(void) {
    if (best_score != saved_best_score) {
        config_set_int("arcade2048", "best", (long)best_score);
        if (config_save()) saved_best_score = best_score;
    }
}

static void refresh_board(void) {
    if (game.score > best_score) best_score = game.score;
    lv_label_set_text_fmt(score_label, "Score %u   Best %u",
                          (unsigned)game.score, (unsigned)best_score);
    for (int i = 0; i < 16; ++i) {
        lv_obj_set_style_bg_color(tiles[i], tile_color(game.cells[i]), 0);
        lv_obj_set_style_bg_opa(tiles[i],game.cells[i]?LV_OPA_COVER:LV_OPA_50,0);
        if (game.cells[i]) {
            lv_label_set_text_fmt(tile_labels[i], "%u", (unsigned)game.cells[i]);
        } else {
            lv_label_set_text(tile_labels[i], "");
        }
    }
    if (!arcade2048_has_moves(&game))
        lv_label_set_text(hint_label, "Game over! New Game to retry");
    else if (arcade2048_won(&game))
        lv_label_set_text(hint_label, "2048! Keep going");
    else if(game.merge_streak>=2)
        lv_label_set_text_fmt(hint_label,"FUSION x%u   +%u",(unsigned)game.merge_streak,(unsigned)game.last_gain);
    else
        lv_label_set_text(hint_label, "Swipe to move tiles");
}

static void start_new_game(void) {
    uint32_t seed = (uint32_t)time(NULL) ^ lv_tick_get();
    arcade2048_reset(&game, seed);
    game_initialized = true;
    can_undo = false;
    refresh_board();
}

static void new_game_cb(lv_event_t *event) {
    (void)event;
    save_high_score();
    start_new_game();
}

/* One undo per turn, intentionally kept entirely in RAM. No write per
 * swipe means less flash wear and no music-thread interference. */
static void undo_game_cb(lv_event_t *event) {
    (void)event;
    if (!can_undo) return;
    game = undo_snapshot;
    can_undo = false;
    refresh_board();
}

static void board_touch_cb(lv_event_t *event) {
    lv_event_code_t code = lv_event_get_code(event);
    lv_indev_t *indev = lv_indev_active();
    if (!indev) return;
    lv_point_t point;
    lv_indev_get_point(indev, &point);
    if (code == LV_EVENT_PRESSED) {
        swipe_start = point;
        swipe_tracking = true;
    } else if (code == LV_EVENT_PRESS_LOST) {
        swipe_tracking = false;
    } else if (code == LV_EVENT_RELEASED && swipe_tracking) {
        swipe_tracking = false;
        if (player_sheet_drag_active()) return;
        int dx = point.x - swipe_start.x;
        int dy = point.y - swipe_start.y;
        if (dx * dx + dy * dy < 18 * 18) return;
        arcade_dir_t direction;
        if (dx > dy && dx > -dy) direction = ARCADE_RIGHT;
        else if (-dx > dy && -dx > -dy) direction = ARCADE_LEFT;
        else direction = dy > 0 ? ARCADE_DOWN : ARCADE_UP;
        arcade2048_t previous = game;
        if (arcade2048_move(&game, direction)) {
            undo_snapshot = previous;
            can_undo = true;
            refresh_board();
            if (game.last_gain) {arcade_art_pulse(score_label);arcade_fx_burst(fusion_fx,lv_obj_get_width(board)/2,lv_obj_get_height(board)/2,lv_color_hex(0xFFDA9D));}
        }
    }
}

static void on_game_unloaded(lv_event_t *event) {
    (void)event;
    swipe_tracking = false;
    save_high_score();
}

static void open_2048_cb(lv_event_t *event) {
    (void)event;
    if (!game_initialized) start_new_game();
    switch_screen(arcade2048_screen);
}

static lv_obj_t *make_button(lv_obj_t *parent, const char *text,
                             int x, int y, int width, int height,
                             lv_event_cb_t callback) {
    lv_obj_t *button = lv_btn_create(parent);
    lv_obj_set_size(button, width, height);
    lv_obj_set_pos(button, x, y);
    lv_obj_add_style(button, &theme_style_card, 0);
    lv_obj_add_style(button, &theme_style_card_pressed, LV_STATE_PRESSED);
    lv_obj_set_style_opa(button, LV_OPA_80, LV_STATE_PRESSED);
    lv_obj_set_style_radius(button, 10, 0);
    lv_obj_set_style_shadow_width(button, 0, 0);
    lv_obj_set_style_pad_all(button, 0, 0);
    if (callback) lv_obj_add_event_cb(button, callback, LV_EVENT_CLICKED, NULL);
    lv_obj_t *label = lv_label_create(button);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, &font_ui_18, 0);
    lv_obj_add_style(label, &theme_style_text, 0);
    lv_obj_center(label);
    return button;
}

/* Launcher cards show both the game and its central mechanic. On a 240x320
 * panel this avoids entering twelve games just to learn the controls. All
 * assets are LVGL primitives, so there is no image-loading or ROM dependency. */
static void make_game_row(lv_obj_t *parent, unsigned art_id, const char *name, const char *detail,
                          int y, int width, lv_event_cb_t callback) {
    lv_obj_t *card = lv_btn_create(parent);
    lv_obj_set_pos(card, 0, y);
    lv_obj_set_size(card, width, 57);
    lv_obj_add_style(card, &theme_style_card, 0);
    lv_obj_add_style(card, &theme_style_card_pressed, LV_STATE_PRESSED);
    lv_obj_set_style_opa(card, LV_OPA_80, LV_STATE_PRESSED);
    lv_obj_set_style_shadow_width(card, 0, 0);
    lv_obj_set_style_radius(card, 9, 0);
    lv_obj_set_style_pad_all(card, 0, 0);
    lv_obj_add_event_cb(card, callback, LV_EVENT_CLICKED, NULL);
    /* Embedded, original 24x24 art; scaled at asset-generation time using
     * nearest-neighbor. No PNG decode or file I/O at runtime. */
    lv_obj_t *art = lv_image_create(card);
    lv_image_set_src(art, arcade_art_badge(art_id));
    lv_obj_set_pos(art, 9, 16);
    lv_obj_set_clickable(art, false);
    lv_obj_t *title = lv_label_create(card);
    lv_label_set_text(title, name);
    lv_obj_add_style(title, &theme_style_text, 0);
    lv_obj_set_style_text_font(title, &font_ui_18, 0);
    lv_obj_set_width(title, width - 50);
    lv_label_set_long_mode(title, LV_LABEL_LONG_DOT);
    lv_obj_set_pos(title, 42, 6);
    lv_obj_t *subtitle = lv_label_create(card);
    lv_label_set_text(subtitle, detail);
    lv_obj_add_style(subtitle, &theme_style_text_dim, 0);
    lv_obj_set_style_text_font(subtitle, &font_ui_14, 0);
    lv_obj_set_width(subtitle, width - 50);
    lv_label_set_long_mode(subtitle, LV_LABEL_LONG_DOT);
    lv_obj_set_pos(subtitle, 42, 31);
}

/* The two realtime games have no audio dependencies. Their timers are paused
 * while their pages aren't active, so background music gets CPU priority. */

static arcade_stack_t stack_game;
static arcade_racer_t racer_game;
static lv_obj_t *stack_screen, *racer_screen;
static lv_obj_t *stack_field, *stack_current, *stack_base, *stack_fx, *racer_fx, *brick_fx, *mines_fx;
static lv_obj_t *stack_blocks[STACK_HISTORY];
static lv_obj_t *stack_status, *racer_status, *racer_road, *racer_player;
static lv_obj_t *racer_car_image,*racer_traffic_image[RACER_ROWS];
static lv_obj_t *racer_lane_marks[RACER_ROWS*2];
static lv_timer_t *stack_timer, *racer_timer;
static uint32_t stack_best, racer_best;
static int stack_x, stack_w, stack_h;
static int racer_x, racer_y, racer_w, racer_h;
/* Only the opacity of the racer car tint changes on boost transitions. */
static int racer_recolor_cache=-1;

static void paint_rect(lv_obj_t *item, int x, int y, int w, int h, lv_color_t color) {
    lv_obj_set_pos(item, x, y);
    lv_obj_set_size(item, w > 1 ? w : 1, h);
    lv_obj_set_style_bg_color(item, color, 0);
    lv_obj_set_style_bg_opa(item, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(item, 0, 0);
    lv_obj_set_style_radius(item, 3, 0);
}

static lv_obj_t *new_rect(lv_obj_t *parent) {
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_set_style_pad_all(o, 0, 0);
    lv_obj_set_scrollable(o, false);
    lv_obj_set_clickable(o, false);
    return o;
}

static void stack_render(void) {
    int shown = stack_game.placed < STACK_HISTORY ? stack_game.placed : STACK_HISTORY;
    int xbase = stack_x;
    int base_y = stack_h - 18;
    int step = (stack_h - 34) / (STACK_HISTORY + 1);
    if (step < 10) step = 10;
    paint_rect(stack_base, xbase + stack_w / 5, base_y,
               stack_w * 3 / 5, step - 2, lv_color_hex(0x687991));
    for (int i = 0; i < STACK_HISTORY; ++i) {
        if (i >= shown) {
            lv_obj_set_hidden(stack_blocks[i], true);
        } else {
            lv_obj_set_hidden(stack_blocks[i], false);
            int x = xbase + stack_game.layer_x[i] * stack_w / STACK_FIELD;
            int w = stack_game.layer_width[i] * stack_w / STACK_FIELD;
            paint_rect(stack_blocks[i], x, base_y - step * (shown - i),
                       w, step - 2, lv_color_hex((i % 2) ? 0x53b9ce : 0x8ccbd8));
        }
    }
    lv_obj_set_hidden(stack_current, stack_game.over);
    if (!stack_game.over) {
        int x = xbase + stack_game.moving_x * stack_w / STACK_FIELD;
        int w = stack_game.width * stack_w / STACK_FIELD;
        paint_rect(stack_current, x, base_y - step * (shown + 1),
                   w, step - 2, lv_color_hex(0xe1a35f));
    }
    lv_label_set_text_fmt(stack_status, "Floors %d  Best %u%s",
        stack_game.placed, (unsigned)stack_best,
        stack_game.over ? "  OUT" : stack_game.perfect_streak>0 && stack_game.perfect_streak%3==0 ? " GOLDEN SNAP!" : "");
}

static void stack_reset_cb(lv_event_t *e) {
    (void)e;
    arcade_stack_reset(&stack_game, (uint32_t)time(NULL) ^ lv_tick_get());
    if (stack_timer) lv_timer_resume(stack_timer);
    stack_render();
}

static void stack_drop_cb(lv_event_t *e) {
    (void)e;
    if (stack_game.over) return;
    arcade_stack_drop(&stack_game);
    if ((uint32_t)stack_game.placed > stack_best) stack_best = (uint32_t)stack_game.placed;
    if (stack_game.over) lv_timer_pause(stack_timer);
    stack_render();
    arcade_fx_burst(stack_fx,stack_w/2,stack_h/2,lv_color_hex(0xFFF4A0));
}

static void stack_tick_cb(lv_timer_t *timer) {
    (void)timer;
    /* Keep the game paused while Sonix's now-playing sheet covers it. */
    if (!power_screen_is_on() || player_sheet_is_open() || player_sheet_drag_active()) return;
    arcade_stack_tick(&stack_game);
    /* All other geometry changes ONLY on a drop/reset, where stack_render()
     * runs. At 30 Hz update only the X position of the moving platform. */
    if (!stack_game.over) {
        int x = stack_x + stack_game.moving_x * stack_w / STACK_FIELD;
        if (lv_obj_get_x(stack_current) != x) lv_obj_set_x(stack_current, x);
    }
}

static void stack_loaded_cb(lv_event_t *e) {
    (void)e;
    if (!stack_game.over) lv_timer_resume(stack_timer);
}

static void stack_unloaded_cb(lv_event_t *e) {
    (void)e;
    lv_timer_pause(stack_timer);
    if ((uint32_t)config_get_int("arcade_stack", "best", 0) < stack_best) {
        config_set_int("arcade_stack", "best", (long)stack_best);
        config_save();
    }
}

static void stack_open_cb(lv_event_t *e) {
    (void)e;
    switch_screen(stack_screen);
}

static void racer_render(void) {
    lv_label_set_text_fmt(racer_status,"%u Best%u N%u%s",(unsigned)racer_game.score,
        (unsigned)racer_best,racer_game.nitro,racer_game.boost_ticks?" BOOST!":
        racer_game.over?" CRASH":"");
    int lane_w = racer_w / RACER_LANES;
    int row_h = racer_h / RACER_ROWS;
    /* Decor scrolls at the same fixed logic cadence as traffic. No extra
     * LVGL animation or per-frame image decode. */
    /* Road markings have fixed size/color. Scrolling only changes Y; avoid
     * writing six LVGL geometry/style properties for each mark every tick. */
    for(int i=0;i<RACER_ROWS;i++)for(int lane=1;lane<=2;lane++) {
        lv_obj_t *mark=racer_lane_marks[i*2+lane-1];
        int y=((i+(racer_game.score/10)%RACER_ROWS)%RACER_ROWS)*row_h+2;
        if(lv_obj_get_y(mark)!=y)lv_obj_set_y(mark,y);
    }
    for(int i=0;i<RACER_ROWS;i++) {
        lv_obj_t *car=racer_traffic_image[i];
        int lane=racer_game.traffic[i];
        if(lane<0){
            if(!lv_obj_is_hidden(car))lv_obj_set_hidden(car,true);
        }else{
            if(lv_obj_is_hidden(car))lv_obj_set_hidden(car,false);
            int x=racer_x+lane*lane_w+(lane_w-20)/2;
            int y=racer_y+i*row_h+(row_h-20)/2;
            if(lv_obj_get_x(car)!=x||lv_obj_get_y(car)!=y)lv_obj_set_pos(car,x,y);
        }
    }
    int player_x=racer_x+racer_game.player_lane*lane_w+(lane_w-20)/2;
    int player_y=racer_y+(RACER_ROWS-1)*row_h+(row_h-20)/2;
    if(lv_obj_get_x(racer_car_image)!=player_x||lv_obj_get_y(racer_car_image)!=player_y)
        lv_obj_set_pos(racer_car_image,player_x,player_y);
    int recolor=racer_game.boost_ticks?LV_OPA_50:LV_OPA_TRANSP;
    if(racer_recolor_cache!=recolor){
        lv_obj_set_style_image_recolor_opa(racer_car_image,recolor,0);
        racer_recolor_cache=recolor;
    }
}

static void racer_nitro_cb(lv_event_t *e){
    (void)e;
    bool was_boosting=racer_game.boost_ticks>0;
    arcade_racer_boost(&racer_game);
    if(!was_boosting && racer_game.boost_ticks)arcade_fx_burst(racer_fx,(2*racer_game.player_lane+1)*racer_w/(2*RACER_LANES),racer_h-22,lv_color_hex(0xFFDB94));
    racer_render();
}
static void racer_reset_cb(lv_event_t *e) {
    (void)e;
    arcade_racer_reset(&racer_game, (uint32_t)time(NULL) ^ lv_tick_get());
    if (racer_timer) {
        lv_timer_set_period(racer_timer,arcade_racer_period_ms(&racer_game));
        lv_timer_resume(racer_timer);
    }
    racer_render();
}

static void racer_steer(int direction) {
    arcade_racer_steer(&racer_game, direction);
    if (racer_game.over) lv_timer_pause(racer_timer);
    racer_render();
}

static void racer_steer_cb(lv_event_t *e) {
    racer_steer((int)(intptr_t)lv_event_get_user_data(e));
}

static void racer_road_cb(lv_event_t *e) {
    (void)e;
    lv_indev_t *indev = lv_indev_active();
    if (!indev) return;
    lv_point_t point;
    lv_area_t a;
    lv_indev_get_point(indev, &point);
    lv_obj_get_coords(racer_road, &a);
    racer_steer(point.x < (a.x1 + a.x2) / 2 ? -1 : 1);
}

static void racer_tick_cb(lv_timer_t *timer) {
    (void)timer;
    /* Keep the game paused while Sonix's now-playing sheet covers it. */
    if (!power_screen_is_on() || player_sheet_is_open() || player_sheet_drag_active()) return;
    bool previously_over=racer_game.over;
    arcade_racer_tick(&racer_game);
    if(!previously_over&&racer_game.over)arcade_fx_burst(racer_fx,(2*racer_game.player_lane+1)*racer_w/(2*RACER_LANES),racer_h-24,lv_color_hex(0xFF7390));
    if (racer_game.score > racer_best) racer_best = racer_game.score;
    if (racer_game.over) lv_timer_pause(racer_timer);
    else lv_timer_set_period(racer_timer,arcade_racer_period_ms(&racer_game));
    racer_render();
}

static void racer_loaded_cb(lv_event_t *e) {
    (void)e;
    if (!racer_game.over) lv_timer_resume(racer_timer);
}

static void racer_unloaded_cb(lv_event_t *e) {
    (void)e;
    lv_timer_pause(racer_timer);
    if ((uint32_t)config_get_int("arcade_racer", "best", 0) < racer_best) {
        config_set_int("arcade_racer", "best", (long)racer_best);
        config_save();
    }
}

static void racer_open_cb(lv_event_t *e) {
    (void)e;
    switch_screen(racer_screen);
}

static void setup_realtime_games(gui_config_t *cfg) {
    int pad = cfg->padding;
    int top = settingsrow_content_top(cfg);
    int width = cfg->screen_width - 2 * pad;
    int field_top = top + 23;
    int field_height = cfg->screen_height - field_top - pad - 39;
    if (field_height < 130) field_height = 130;

    stack_screen = lv_obj_create(NULL);
    lv_obj_add_style(stack_screen, &theme_style_screen, 0);
    settingsrow_title(stack_screen, cfg, "Stack It");
    stack_status = lv_label_create(stack_screen);
    lv_obj_add_style(stack_status, &theme_style_text, 0);
    lv_obj_set_style_text_font(stack_status, &font_ui_14, 0);
    lv_obj_set_pos(stack_status, pad, top);
    stack_field = lv_obj_create(stack_screen);
    lv_obj_set_size(stack_field, width, field_height);
    lv_obj_set_pos(stack_field, pad, field_top);
    lv_obj_set_style_pad_all(stack_field, 0, 0);
    lv_obj_set_style_border_width(stack_field, 0, 0);
    lv_obj_set_style_bg_color(stack_field, lv_color_hex(0x1c2736), 0);
    lv_obj_set_style_bg_opa(stack_field, LV_OPA_COVER, 0);
    lv_obj_set_scrollable(stack_field, false);
    lv_obj_set_clickable(stack_field, true);
    cabinet_skin(stack_screen,0x27182C,stack_field,0xF4C276,0x3A2440);
    arcade_scene_attach(stack_field,1,width,field_height);
    stack_x = 4; stack_w = width - 8; stack_h = field_height;
    /* A static skyline and setting sun give the tower a world without using
     * a continuously redrawn background (critical alongside audio decode). */
    lv_obj_t *sun=new_rect(stack_field);
    paint_rect(sun,width*2/3,14,30,30,lv_color_hex(0xF3B576));
    lv_obj_set_style_radius(sun,16,0);
    for(int i=0;i<11;i++) {
        lv_obj_t *building=new_rect(stack_field);
        int bh=9+(i*17)%36;
        paint_rect(building,i*(width/11),field_height-bh,width/11+1,bh,
            lv_color_hex((i&1)?0x3E2948:0x39263F));
    }
    stack_fx=arcade_fx_create(stack_field);
    stack_base = new_rect(stack_field);
    stack_current = new_rect(stack_field);
    for (int i = 0; i < STACK_HISTORY; i++) stack_blocks[i] = new_rect(stack_field);
    lv_obj_add_event_cb(stack_field, stack_drop_cb, LV_EVENT_CLICKED, NULL);
    make_button(stack_screen, "Drop!", pad,
       cfg->screen_height - pad - 29, width / 2 - 3, 28, stack_drop_cb);
    make_button(stack_screen, "Restart", pad + width / 2 + 3,
       cfg->screen_height - pad - 29, width / 2 - 3, 28, stack_reset_cb);
    stack_best = (uint32_t)config_get_int("arcade_stack", "best", 0);
    arcade_stack_reset(&stack_game, (uint32_t)time(NULL) ^ lv_tick_get());
    stack_timer = lv_timer_create(stack_tick_cb, 33, NULL);
    lv_timer_pause(stack_timer);
    power_pause_in_standby(stack_timer);
    lv_obj_add_event_cb(stack_screen, stack_loaded_cb, LV_EVENT_SCREEN_LOADED, NULL);
    lv_obj_add_event_cb(stack_screen, stack_unloaded_cb, LV_EVENT_SCREEN_UNLOADED, NULL);
    stack_render();

    racer_screen = lv_obj_create(NULL);
    lv_obj_add_style(racer_screen, &theme_style_screen, 0);
    settingsrow_title(racer_screen, cfg, "Pocket Racer");
    racer_status = lv_label_create(racer_screen);
    lv_obj_add_style(racer_status, &theme_style_text, 0);
    lv_obj_set_style_text_font(racer_status, &font_ui_14, 0);
    lv_obj_set_pos(racer_status, pad, top);
    racer_road = lv_obj_create(racer_screen);
    lv_obj_set_size(racer_road, width, field_height);
    lv_obj_set_pos(racer_road, pad, field_top);
    lv_obj_set_style_pad_all(racer_road, 0, 0);
    lv_obj_set_style_border_width(racer_road, 0, 0);
    lv_obj_set_style_bg_color(racer_road, lv_color_hex(0x1b2733), 0);
    lv_obj_set_style_bg_opa(racer_road, LV_OPA_COVER, 0);
    lv_obj_set_scrollable(racer_road, false);
    lv_obj_set_clickable(racer_road, true);
    cabinet_skin(racer_screen,0x1C142A,racer_road,0xFF9573,0x30344D);
    arcade_scene_attach(racer_road,2,width,field_height);
    /* The original sunset art has a perspective road, while our traffic
     * logic uses three constant-width lanes. Cover that conflicting road
     * with one crisp, top-down 3-lane track. Gameplay coordinates, cars and
     * dashed lane dividers now share the exact same road rectangle. */
    racer_x = width/10;
    racer_y = 0;
    racer_w = width-2*racer_x;
    racer_h = field_height;
    int lane_width=racer_w/RACER_LANES;
    lv_obj_t *asphalt=new_rect(racer_road);
    paint_rect(asphalt,racer_x,0,racer_w,racer_h,lv_color_hex(0x23293D));
    lv_obj_set_style_radius(asphalt,0,0);
    for(int lane=0;lane<RACER_LANES;lane++){
        lv_obj_t *strip=new_rect(racer_road);
        paint_rect(strip,racer_x+lane*lane_width,0,
                   lane==RACER_LANES-1?racer_w-lane*lane_width:lane_width,
                   racer_h,lv_color_hex(lane==1?0x2C3145:0x292F43));
        lv_obj_set_style_radius(strip,0,0);
    }
    for(int side=0;side<2;side++){
        lv_obj_t *edge=new_rect(racer_road);
        paint_rect(edge,racer_x+(side?racer_w:0)-2,0,3,racer_h,
                   lv_color_hex(0xF1A96A));
        lv_obj_set_style_radius(edge,0,0);
    }
    for(int i=0;i<RACER_ROWS;i++)for(int lane=1;lane<=2;lane++){
        int row_h=racer_h/RACER_ROWS;
        int y=((i+(racer_game.score/10)%RACER_ROWS)%RACER_ROWS)*row_h;
        lv_obj_t *mark=new_rect(racer_road);
        racer_lane_marks[i*2+lane-1]=mark;
        paint_rect(mark,racer_x+lane*lane_width-1,y+2,2,row_h/3,
                   lv_color_hex(0xF5D8A1));
    }
    racer_player = new_rect(racer_road);
    lv_obj_set_hidden(racer_player,true);
    for (int i = 0; i < RACER_ROWS; ++i) {
        racer_traffic_image[i]=lv_image_create(racer_road);
        lv_image_set_src(racer_traffic_image[i],arcade_stage_car(0));
        lv_obj_set_clickable(racer_traffic_image[i],false);
    }
    racer_car_image=lv_image_create(racer_road);
    lv_image_set_src(racer_car_image,arcade_stage_car(1));
    lv_obj_set_clickable(racer_car_image,false);
    lv_obj_set_style_image_recolor(racer_car_image,lv_color_hex(0xFFF09B),0);
    racer_fx=arcade_fx_create(racer_road);
    lv_obj_add_event_cb(racer_road, racer_road_cb, LV_EVENT_CLICKED, NULL);
    int unit=width/5;
    lv_obj_t *left = make_button(racer_screen, "<", pad,
        cfg->screen_height - pad - 29, unit-2, 28, NULL);
    lv_obj_t *right = make_button(racer_screen, ">", pad + unit,
        cfg->screen_height - pad - 29, unit-2, 28, NULL);
    lv_obj_add_event_cb(left, racer_steer_cb, LV_EVENT_CLICKED, (void *)(intptr_t)-1);
    lv_obj_add_event_cb(right, racer_steer_cb, LV_EVENT_CLICKED, (void *)(intptr_t)1);
    make_button(racer_screen,"NITRO",pad+unit*2,
        cfg->screen_height-pad-29,unit-2,28,racer_nitro_cb);
    make_button(racer_screen,"Reset",pad+unit*3,
        cfg->screen_height-pad-29,width-unit*3,28,racer_reset_cb);
    racer_best = (uint32_t)config_get_int("arcade_racer", "best", 0);
    arcade_racer_reset(&racer_game, (uint32_t)time(NULL) ^ lv_tick_get());
    racer_timer = lv_timer_create(racer_tick_cb, 145, NULL);
    lv_timer_pause(racer_timer);
    power_pause_in_standby(racer_timer);
    lv_obj_add_event_cb(racer_screen, racer_loaded_cb, LV_EVENT_SCREEN_LOADED, NULL);
    lv_obj_add_event_cb(racer_screen, racer_unloaded_cb, LV_EVENT_SCREEN_UNLOADED, NULL);
    racer_render();
}


/* --- Brick Breaker and Minesweeper: offline, silent, independent of ALSA. --- */
static arcade_bricks_t bricks_game;
static arcade_mines_t mines_game;
static lv_obj_t *bricks_screen, *mines_screen, *bricks_field;
static lv_obj_t *bricks_status, *mines_status, *bricks_ball, *bricks_paddle;
static lv_obj_t *bricks_rows[BRICKS_ROWS][BRICKS_COLS];
static lv_obj_t *mines_cells[MINES_CELLS], *mines_labels[MINES_CELLS];
static lv_obj_t *mines_mode_label;
static lv_timer_t *bricks_timer;
static bool mines_flag_mode;
static uint32_t bricks_best;
static int brick_w, brick_h;
static uint8_t bricks_painted_rows[BRICKS_ROWS];
static uint8_t bricks_painted_hard[BRICKS_ROWS];
static uint32_t bricks_painted_score = UINT32_MAX;
static uint32_t bricks_painted_best = UINT32_MAX;
static int bricks_painted_lives = -1;
static bool bricks_painted_over, bricks_painted_won;

static void bricks_render(void) {
    /* Ball/paddle geometry and style are initialized once in build().
     * An ordinary 33ms physics tick only moves their existing LVGL objects. */
    int px=bricks_game.paddle_x*brick_w/BRICKS_FIELD;
    if(lv_obj_get_x(bricks_paddle)!=px)lv_obj_set_x(bricks_paddle,px);
    int ball_x=bricks_game.x*brick_w/BRICKS_FIELD-3;
    int ball_y=bricks_game.y*brick_h/BRICKS_FIELD-3;
    if(lv_obj_get_x(bricks_ball)!=ball_x||lv_obj_get_y(bricks_ball)!=ball_y)
        lv_obj_set_pos(bricks_ball,ball_x,ball_y);
    /* Bricks are stationary. Don't repaint 40 widgets at 30 FPS just to
     * update a ball; touching the LVGL tree less helps protect audio threads. */
    for (int row = 0; row < BRICKS_ROWS; row++) {
        if(bricks_painted_rows[row]==bricks_game.brick_rows[row] &&
           bricks_painted_hard[row]==bricks_game.hard_rows[row])continue;
        for(int col=0;col<BRICKS_COLS;col++){
            uint8_t bit=(uint8_t)(1u<<col);
            lv_obj_set_hidden(bricks_rows[row][col],(bricks_game.brick_rows[row]&bit)==0);
            lv_obj_set_style_bg_color(bricks_rows[row][col],
                lv_color_hex((bricks_game.hard_rows[row]&bit)?0xffd16a:
                    (row%2?0x6fc1d4:0xca8ed6)),0);
        }
        bricks_painted_rows[row]=bricks_game.brick_rows[row];
        bricks_painted_hard[row]=bricks_game.hard_rows[row];
    }
    if (bricks_painted_score != bricks_game.score ||
        bricks_painted_best != bricks_best ||
        bricks_painted_lives != bricks_game.lives ||
        bricks_painted_over != bricks_game.over || bricks_painted_won != bricks_game.won) {
        lv_label_set_text_fmt(bricks_status,"%u Best%u HP%d x%d%s",
            (unsigned)bricks_game.score,(unsigned)bricks_best,bricks_game.lives,
            bricks_game.combo,bricks_game.won?" WIN":bricks_game.over?" OUT":"");
        bricks_painted_score = bricks_game.score;
        bricks_painted_best = bricks_best;
        bricks_painted_lives = bricks_game.lives;
        bricks_painted_over = bricks_game.over;
        bricks_painted_won = bricks_game.won;
    }
}
static void bricks_reset_cb(lv_event_t *e) {
    (void)e;
    arcade_bricks_reset(&bricks_game);
    bricks_render();
    if (bricks_timer) lv_timer_resume(bricks_timer);
}
static void bricks_touch_cb(lv_event_t *e) {
    (void)e;
    lv_indev_t *indev = lv_indev_active();
    if (!indev) return;
    lv_point_t point;
    lv_area_t area;
    lv_indev_get_point(indev, &point);
    lv_obj_get_coords(bricks_field, &area);
    arcade_bricks_move(&bricks_game,
        (point.x - area.x1) * BRICKS_FIELD / brick_w);
    bricks_render();
}
static void bricks_tick_cb(lv_timer_t *timer) {
    (void)timer;
    /* Keep the game paused while Sonix's now-playing sheet covers it. */
    if (!power_screen_is_on() || player_sheet_is_open() || player_sheet_drag_active()) return;
    uint32_t prior_score=bricks_game.score;
    arcade_bricks_tick(&bricks_game);
    if (bricks_game.score > bricks_best) bricks_best = bricks_game.score;
    if (bricks_game.over || bricks_game.won) lv_timer_pause(bricks_timer);
    bricks_render();
    if(bricks_game.score!=prior_score)arcade_fx_burst(brick_fx,bricks_game.x*brick_w/BRICKS_FIELD,bricks_game.y*brick_h/BRICKS_FIELD,lv_color_hex(0xFFE4A2));
}
static void bricks_loaded_cb(lv_event_t *e) {
    (void)e;
    if (!bricks_game.over && !bricks_game.won) lv_timer_resume(bricks_timer);
}
static void bricks_unloaded_cb(lv_event_t *e) {
    (void)e;
    lv_timer_pause(bricks_timer);
    if ((uint32_t)config_get_int("arcade_bricks", "best", 0) < bricks_best) {
        config_set_int("arcade_bricks", "best", (long)bricks_best);
        config_save();
    }
}
static void bricks_open_cb(lv_event_t *e) {
    (void)e;
    switch_screen(bricks_screen);
}
static void mines_render(void) {
    unsigned flags = 0;
    for (int i = 0; i < MINES_CELLS; i++) {
        bool revealed = mines_game.revealed[i];
        bool bomb = mines_game.over && mines_game.mine[i];
        if (mines_game.flagged[i]) flags++;
        lv_obj_set_style_bg_opa(mines_cells[i],revealed?LV_OPA_50:LV_OPA_COVER,0);
        lv_obj_set_style_bg_color(mines_cells[i],
            revealed ? lv_color_hex(0x304055) :
            bomb ? lv_color_hex(0x9d4050) : lv_color_hex(0x465570), 0);
        if (mines_game.flagged[i] && !revealed) lv_label_set_text(mines_labels[i], "F");
        else if (bomb) lv_label_set_text(mines_labels[i], "*");
        else if (revealed && mines_game.adjacent[i])
            lv_label_set_text_fmt(mines_labels[i], "%u", (unsigned)mines_game.adjacent[i]);
        else lv_label_set_text(mines_labels[i], "");
    }
    lv_label_set_text_fmt(mines_status, "Mines %d  Flags %u%s",
        MINES_COUNT, flags, mines_game.won ? "  CLEARED" : mines_game.over ? "  BOOM" : "");
    lv_label_set_text(mines_mode_label, mines_flag_mode ? "Flag mode" : "Reveal / chord");
}
static void mines_cell_cb(lv_event_t *e) {
    int index = (int)(intptr_t)lv_event_get_user_data(e);
    if (mines_flag_mode) arcade_mines_toggle_flag(&mines_game, index);
    else if(mines_game.revealed[index])arcade_mines_chord(&mines_game,index);
    else arcade_mines_reveal(&mines_game, index);
    mines_render();
    if(mines_game.over || mines_game.won){arcade_art_pulse(mines_status);arcade_fx_burst(mines_fx,arcade_cfg->screen_width/2,arcade_cfg->screen_height/2,lv_color_hex(mines_game.won?0xA9EFC8:0xFF8F8D));}
}
static void mines_mode_cb(lv_event_t *e) {
    (void)e;
    mines_flag_mode = !mines_flag_mode;
    mines_render();
}
static void mines_reset_cb(lv_event_t *e) {
    (void)e;
    arcade_mines_reset(&mines_game, (uint32_t)time(NULL) ^ lv_tick_get());
    mines_flag_mode = false;
    mines_render();
}
static void mines_open_cb(lv_event_t *e) {
    (void)e;
    switch_screen(mines_screen);
}
static void setup_bonus_games(gui_config_t *cfg) {
    int pad = cfg->padding;
    int top = settingsrow_content_top(cfg);
    int width = cfg->screen_width - 2 * pad;
    int area_top = top + 23;
    int area_h = cfg->screen_height - area_top - pad - 37;
    if (area_h < 110) area_h = 110;
    bricks_screen = lv_obj_create(NULL);
    lv_obj_add_style(bricks_screen, &theme_style_screen, 0);
    settingsrow_title(bricks_screen, cfg, "Brick Breaker");
    bricks_status = lv_label_create(bricks_screen);
    lv_obj_add_style(bricks_status, &theme_style_text, 0);
    lv_obj_set_style_text_font(bricks_status, &font_ui_14, 0);
    lv_obj_set_pos(bricks_status, pad, top);
    bricks_field = lv_obj_create(bricks_screen);
    lv_obj_set_pos(bricks_field, pad, area_top);
    lv_obj_set_size(bricks_field, width, area_h);
    lv_obj_set_style_pad_all(bricks_field, 0, 0);
    lv_obj_set_style_border_width(bricks_field, 0, 0);
    lv_obj_set_style_bg_color(bricks_field, lv_color_hex(0x1b2734), 0);
    lv_obj_set_style_bg_opa(bricks_field, LV_OPA_COVER, 0);
    lv_obj_set_scrollable(bricks_field, false);
    lv_obj_set_clickable(bricks_field, true);
    cabinet_skin(bricks_screen,0x0D1935,bricks_field,0x74E7ED,0x121D41);
    arcade_scene_attach(bricks_field,3,width,area_h);
    brick_w = width; brick_h = area_h;
    for (int r = 0; r < BRICKS_ROWS; r++) {
        bricks_painted_rows[r] = 0;
        for (int c = 0; c < BRICKS_COLS; c++) {
            bricks_rows[r][c] = new_rect(bricks_field);
            int cell_w = brick_w / BRICKS_COLS;
            paint_rect(bricks_rows[r][c], c * cell_w + 2,
                (100 + r * 50) * brick_h / BRICKS_FIELD,
                cell_w - 4, 48 * brick_h / BRICKS_FIELD,
                lv_color_hex(r % 2 ? 0x6fc1d4 : 0xca8ed6));
        }
    }
    brick_fx=arcade_fx_create(bricks_field);
    bricks_ball = new_rect(bricks_field);
    bricks_paddle = new_rect(bricks_field);
    /* One-time size, color, border and opacity setup; never on physics ticks. */
    paint_rect(bricks_ball,0,0,7,7,lv_color_hex(0xffdda3));
    paint_rect(bricks_paddle,0,895*brick_h/BRICKS_FIELD,
               BRICKS_PADDLE_W*brick_w/BRICKS_FIELD,
               18*brick_h/BRICKS_FIELD,lv_color_hex(0x52d6b6));
    lv_obj_add_event_cb(bricks_field, bricks_touch_cb, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(bricks_field, bricks_touch_cb, LV_EVENT_PRESSING, NULL);
    make_button(bricks_screen, "Restart", pad, cfg->screen_height - pad - 29,
                width, 28, bricks_reset_cb);
    bricks_best = (uint32_t)config_get_int("arcade_bricks", "best", 0);
    arcade_bricks_reset(&bricks_game);
    bricks_timer = lv_timer_create(bricks_tick_cb, 33, NULL);
    lv_timer_pause(bricks_timer);
    power_pause_in_standby(bricks_timer);
    lv_obj_add_event_cb(bricks_screen, bricks_loaded_cb, LV_EVENT_SCREEN_LOADED, NULL);
    lv_obj_add_event_cb(bricks_screen, bricks_unloaded_cb, LV_EVENT_SCREEN_UNLOADED, NULL);
    bricks_render();

    mines_screen = lv_obj_create(NULL);
    lv_obj_add_style(mines_screen, &theme_style_screen, 0);
    cabinet_skin(mines_screen,0x1E2D2C,NULL,0,0);
    settingsrow_title(mines_screen, cfg, "Minesweeper");
    mines_status = lv_label_create(mines_screen);
    lv_obj_add_style(mines_status, &theme_style_text, 0);
    lv_obj_set_style_text_font(mines_status, &font_ui_14, 0);
    lv_obj_set_pos(mines_status, pad, top);
    /* Eight columns, six rows; cell size fits the original 240px panel. */
    int cell = width / MINES_COLS;
    int grid_h = cell * MINES_ROWS;
    int grid_y = area_top + (area_h - grid_h) / 2;
    if (grid_y < area_top) grid_y = area_top;
    /* Separate minefield scenery under the interactive board cells. */
    lv_obj_t *mines_back=lv_obj_create(mines_screen);
    lv_obj_set_pos(mines_back,pad,grid_y);
    lv_obj_set_size(mines_back,width,grid_h);
    lv_obj_set_style_pad_all(mines_back,0,0);
    lv_obj_set_style_border_width(mines_back,0,0);
    lv_obj_set_style_bg_opa(mines_back,LV_OPA_TRANSP,0);
    lv_obj_set_scrollable(mines_back,false);
    lv_obj_set_clickable(mines_back,false);
    arcade_scene_attach(mines_back,4,width,grid_h);
    mines_fx=arcade_fx_create(mines_screen);
    for (int i = 0; i < MINES_CELLS; i++) {
        mines_cells[i] = make_button(mines_screen, "", pad + i % MINES_COLS * cell,
            grid_y + i / MINES_COLS * cell, cell - 2, cell - 2, NULL);
        lv_obj_set_style_bg_color(mines_cells[i], lv_color_hex(0x465570), 0);
        lv_obj_set_style_bg_opa(mines_cells[i], LV_OPA_COVER, 0);
        mines_labels[i] = lv_obj_get_child(mines_cells[i], 0);
        lv_obj_add_event_cb(mines_cells[i], mines_cell_cb, LV_EVENT_CLICKED,
                            (void *)(intptr_t)i);
    }
    int button_y = cfg->screen_height - pad - 29;
    make_button(mines_screen, "Restart", pad, button_y, width / 2 - 3, 28, mines_reset_cb);
    lv_obj_t *mode = make_button(mines_screen, "", pad + width / 2 + 3,
        button_y, width / 2 - 3, 28, mines_mode_cb);
    mines_mode_label = lv_obj_get_child(mode, 0);
    arcade_mines_reset(&mines_game, (uint32_t)time(NULL) ^ lv_tick_get());
    mines_render();
}

/* The second screen owns seven more games; no extra LVGL allocations
 * happen until one of the corresponding launcher rows is opened. */
static void extra_boxes_cb(lv_event_t *e) {(void)e;arcade_extra_open(0);}
static void extra_golf_cb(lv_event_t *e) {(void)e;arcade_extra_open(1);}
static void extra_orbit_cb(lv_event_t *e) {(void)e;arcade_extra_open(2);}
static void extra_melody_cb(lv_event_t *e) {(void)e;arcade_extra_open(3);}
static void extra_quiz_cb(lv_event_t *e) {(void)e;arcade_extra_open(4);}
static void extra_beat_cb(lv_event_t *e) {(void)e;arcade_extra_open(5);}
static void extra_dungeon_cb(lv_event_t *e) {(void)e;dungeon_deluxe_open();}

static void pocket_arcade_build(gui_config_t *cfg) {
    pocket_arcade_screen = lv_obj_create(NULL);
    arcade2048_screen = lv_obj_create(NULL);
    lv_obj_add_style(pocket_arcade_screen, &theme_style_screen, 0);
    lv_obj_add_style(arcade2048_screen, &theme_style_screen, 0);
    settingsrow_title(pocket_arcade_screen, cfg, "Arcade");
    settingsrow_title(arcade2048_screen, cfg, "2048");

    int pad = cfg->padding;
    int content_top = settingsrow_content_top(cfg);
    int width = cfg->screen_width - 2 * pad;
    /* A 240x320 V1 cannot fit five 54px game rows below its title.
     * Give the launcher a dedicated vertical scroller, leaving Sonix's top bar
     * and back gesture alone. */
    lv_obj_t *launcher = lv_obj_create(pocket_arcade_screen);
    lv_obj_set_pos(launcher, pad, content_top + 2);
    lv_obj_set_size(launcher, width, cfg->screen_height - content_top - pad - 2);
    lv_obj_set_style_pad_all(launcher, 0, 0);
    lv_obj_set_style_border_width(launcher, 0, 0);
    lv_obj_set_style_bg_opa(launcher, LV_OPA_TRANSP, 0);
    lv_obj_set_scroll_dir(launcher, LV_DIR_VER);
    make_game_row(launcher, 0, "2048", "Merge tiles, plan each move", 0, width, open_2048_cb);
    make_game_row(launcher, 1, "Stack It", "Drop blocks with perfect timing", 63, width, stack_open_cb);
    make_game_row(launcher, 2, "Pocket Racer", "Dodge traffic in three lanes", 126, width, racer_open_cb);
    make_game_row(launcher, 3, "Brick Breaker", "Clear the wall; control the paddle", 189, width, bricks_open_cb);
    make_game_row(launcher, 4, "Minesweeper", "Find the mines without guessing", 252, width, mines_open_cb);
    make_game_row(launcher, 5, "Box Pusher", "Eight puzzles, one-step undo", 315, width, extra_boxes_cb);
    make_game_row(launcher, 6, "Tiny Golf", "Three tricky tabletop courses", 378, width, extra_golf_cb);
    make_game_row(launcher, 7, "Orbit Hop", "One tap, perfect timing", 441, width, extra_orbit_cb);
    make_game_row(launcher, 8, "Melody Memory", "Remember a growing note pattern", 504, width, extra_melody_cb);
    make_game_row(launcher, 9, "Guess That Song", "Name the track playing right now", 567, width, extra_quiz_cb);
    make_game_row(launcher, 10, "Beat Raiders", "Line up your beat-powered shots", 630, width, extra_beat_cb);
    make_game_row(launcher, 11, "Pocket Dungeon", "The Black Testament / nine-depth campaign", 693, width, extra_dungeon_cb);

    best_score = (uint32_t)config_get_int("arcade2048", "best", 0);
    saved_best_score = best_score;
    score_label = lv_label_create(arcade2048_screen);
    lv_obj_add_style(score_label, &theme_style_text, 0);
    lv_obj_set_style_text_font(score_label, &font_ui_14, 0);
    lv_obj_set_pos(score_label, pad, content_top);
    lv_obj_set_width(score_label, width);
    lv_label_set_long_mode(score_label, LV_LABEL_LONG_DOT);

    int board_y = content_top + 23;
    int size = minimum(width, cfg->screen_height - board_y - pad - 41);
    /* 4 columns with 4 px outer/internal padding; fit on a V1 screen. */
    if (size < 96) size = 96;
    int tile_side = (size - 5 * 4) / 4;
    board = lv_obj_create(arcade2048_screen);
    lv_obj_set_size(board, size, size);
    lv_obj_set_pos(board, (cfg->screen_width - size) / 2, board_y);
    lv_obj_set_style_bg_color(board, lv_color_hex(0x181c25), 0);
    lv_obj_set_style_bg_opa(board, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(board, 0, 0);
    lv_obj_set_style_radius(board, 5, 0);
    lv_obj_set_style_pad_all(board, 0, 0);
    cabinet_skin(arcade2048_screen,0x251C32,board,0xF3C879,0x3C2848);
    arcade_scene_attach(board,0,size,size);
    lv_obj_set_scrollable(board, false);
    lv_obj_set_clickable(board, true);
    lv_obj_add_event_cb(board, board_touch_cb, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(board, board_touch_cb, LV_EVENT_RELEASED, NULL);
    lv_obj_add_event_cb(board, board_touch_cb, LV_EVENT_PRESS_LOST, NULL);

    for (int row = 0; row < 4; ++row) {
        for (int col = 0; col < 4; ++col) {
            int i = row * 4 + col;
            tiles[i] = lv_obj_create(board);
            lv_obj_set_size(tiles[i], tile_side, tile_side);
            lv_obj_set_pos(tiles[i], 4 + col * (tile_side + 4),
                           4 + row * (tile_side + 4));
            lv_obj_set_style_pad_all(tiles[i], 0, 0);
            lv_obj_set_style_border_width(tiles[i], 0, 0);
            lv_obj_set_style_radius(tiles[i], 5, 0);
            lv_obj_set_scrollable(tiles[i], false);
            lv_obj_set_clickable(tiles[i], false);
            tile_labels[i] = lv_label_create(tiles[i]);
            lv_obj_set_style_text_color(tile_labels[i], lv_color_white(), 0);
            lv_obj_set_style_text_font(tile_labels[i], &font_ui_14, 0);
            lv_obj_center(tile_labels[i]);
        }
    }

    fusion_fx=arcade_fx_create(board);
    hint_label = lv_label_create(arcade2048_screen);
    lv_obj_add_style(hint_label, &theme_style_text_dim, 0);
    lv_obj_set_style_text_font(hint_label, &font_ui_14, 0);
    lv_obj_set_style_text_align(hint_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(hint_label, width);
    lv_obj_set_pos(hint_label, pad, board_y + size + 2);
    /* The New Game button is always accessible without a second menu. */
    make_button(arcade2048_screen, "Undo", pad,
                cfg->screen_height - pad - 27, width / 2 - 3, 26, undo_game_cb);
    make_button(arcade2048_screen, "New Game", pad + width / 2 + 3,
                cfg->screen_height - pad - 27, width / 2 - 3, 26, new_game_cb);
    lv_obj_add_event_cb(arcade2048_screen, on_game_unloaded,
                        LV_EVENT_SCREEN_UNLOADED, NULL);
    setup_realtime_games(cfg);
    setup_bonus_games(cfg);
    arcade_extra_init(cfg);
    dungeon_deluxe_init(cfg);
    /* Bind only the narrow edge of each original cabinet and launcher.
     * No screen-wide player drag listener may intercept gameplay touches. */
    lv_obj_t *arcade_pages[] = {
        pocket_arcade_screen, arcade2048_screen,
        stack_screen, racer_screen, bricks_screen, mines_screen
    };
    for (unsigned i = 0; i < sizeof(arcade_pages)/sizeof(arcade_pages[0]); ++i) {
        pocket_arcade_enable_nowplaying_swipe(arcade_pages[i]);
    }
    /* Game timers only render while their game screens are active. */
}

void pocket_arcade_init(gui_config_t *cfg) {
    /* Only remember the profile here. No Arcade allocations or timers are
     * created at startup unless the owner opens More -> Arcade. */
    arcade_cfg = cfg;
}

void pocket_arcade_open(void) {
    if (!pocket_arcade_screen && arcade_cfg) pocket_arcade_build(arcade_cfg);
    if (pocket_arcade_screen) switch_screen(pocket_arcade_screen);
}
