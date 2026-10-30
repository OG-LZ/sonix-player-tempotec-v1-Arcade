#ifndef ARCADE_FX_H
#define ARCADE_FX_H
#include "lvgl/lvgl.h"
/* Reusable 5-piece impact flash. Built once per board, no allocations per hit. */
lv_obj_t *arcade_fx_create(lv_obj_t *parent);
void arcade_fx_burst(lv_obj_t *fx,int center_x,int center_y,lv_color_t color);
#endif
