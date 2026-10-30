/* Beat Raiders R22.6 dedicated LVGL cabinet; bounded objects, no audio calls. */
#ifndef BEAT_STAGE_H
#define BEAT_STAGE_H
#include "arcade_extra7.h"
#include "lvgl/lvgl.h"
void beat_stage_create(lv_obj_t *board);
void beat_stage_resize(int width,int height);
void beat_stage_show(bool visible);
void beat_stage_render(const pa_beat_t *g);
#endif
