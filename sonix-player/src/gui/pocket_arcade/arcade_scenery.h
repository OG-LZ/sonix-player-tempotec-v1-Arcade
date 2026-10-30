#ifndef ARCADE_SCENERY_H
#define ARCADE_SCENERY_H
#include "lvgl/lvgl.h"
const lv_image_dsc_t *arcade_scene_image(int game);
void arcade_scene_update(lv_obj_t *img,int game,int width,int height);
lv_obj_t *arcade_scene_attach(lv_obj_t *parent,int game,int width,int height);
#endif
