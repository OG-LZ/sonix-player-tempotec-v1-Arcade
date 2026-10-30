#ifndef DG_ART_H
#define DG_ART_H
#include "lvgl/lvgl.h"
/* kind: 0 hero,1 skeleton,2 slime,3 bat,4 cultist,5 stone,6 ghost,7 beetle,8 mimic,9 boss,10 chest,11 altar,12 stairs,13 spring,14 shop */
const lv_image_dsc_t *dg_art_actor(unsigned kind,unsigned frame);
const lv_image_dsc_t *dg_art_tile(unsigned biome,unsigned kind);
const lv_image_dsc_t *dg_art_pose(unsigned pose,unsigned frame);
const lv_image_dsc_t *dg_art_boss(unsigned type,unsigned frame);
const lv_image_dsc_t *dg_art_crate(unsigned frame);
const lv_image_dsc_t *dg_art_tile(unsigned biome,unsigned kind);
const lv_image_dsc_t *dg_art_pose(unsigned pose,unsigned frame);
const lv_image_dsc_t *dg_art_boss(unsigned type,unsigned frame);
const lv_image_dsc_t *dg_art_crate(unsigned frame);
const lv_image_dsc_t *dg_art_heavensfall(unsigned kind,unsigned house);
#endif
