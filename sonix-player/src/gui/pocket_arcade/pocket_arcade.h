#ifndef GUI_POCKET_ARCADE_H
#define GUI_POCKET_ARCADE_H

#include "lvgl/lvgl.h"
#include "src/gui/shell/gui.h"

extern lv_obj_t *pocket_arcade_screen;
extern lv_obj_t *arcade2048_screen;

void pocket_arcade_init(gui_config_t *cfg);
void pocket_arcade_open(void);
/* Allow Now Playing ONLY from the 6px extreme right edge of Arcade pages.
 * Unlike R22.6.2/3, the game screen and children do not attach a player-sheet
 * drag listener; game-board gestures are exclusively game input. */
void pocket_arcade_enable_nowplaying_swipe(lv_obj_t *screen);

#endif
