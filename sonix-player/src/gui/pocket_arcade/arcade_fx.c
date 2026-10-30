/* Tiny original four-direction collision sparkle, GPL-3.0-or-later.
 * Owned by the parent board; animations contain no sound or I/O. */
#include "arcade_fx.h"

static void sparkle_opa(void *var,int32_t value){
    lv_obj_set_style_opa((lv_obj_t *)var,(lv_opa_t)value,0);
}

lv_obj_t *arcade_fx_create(lv_obj_t *parent){
    lv_obj_t *spark=lv_obj_create(parent);
    lv_obj_set_size(spark,24,24);
    lv_obj_set_style_pad_all(spark,0,0);
    lv_obj_set_style_bg_opa(spark,LV_OPA_TRANSP,0);
    lv_obj_set_style_border_width(spark,0,0);
    lv_obj_set_style_shadow_width(spark,0,0);
    lv_obj_set_scrollable(spark,false);
    lv_obj_set_clickable(spark,false);
    const int x[]={11,11,2,17,8},y[]={2,17,11,11,8};
    const int w[]={2,2,5,5,8},h[]={5,5,2,2,8};
    for(int i=0;i<5;i++){
        lv_obj_t *arm=lv_obj_create(spark);
        lv_obj_set_pos(arm,x[i],y[i]);
        lv_obj_set_size(arm,w[i],h[i]);
        lv_obj_set_style_pad_all(arm,0,0);
        lv_obj_set_style_radius(arm,0,0);
        lv_obj_set_style_border_width(arm,0,0);
        lv_obj_set_style_shadow_width(arm,0,0);
        lv_obj_set_scrollable(arm,false);
        lv_obj_set_clickable(arm,false);
        lv_obj_set_style_bg_color(arm,lv_color_hex(0xFFF0BA),0);
    }
    lv_obj_set_style_opa(spark,LV_OPA_TRANSP,0);
    return spark;
}

void arcade_fx_burst(lv_obj_t *fx,int x,int y,lv_color_t color){
    if(!fx)return;
    lv_anim_delete(fx,sparkle_opa); /* no overlapping animations on one spark */
    lv_obj_set_pos(fx,x-12,y-12);
    for(int i=0;i<5;i++){
        lv_obj_t *arm=lv_obj_get_child(fx,i);
        if(arm)lv_obj_set_style_bg_color(arm,color,0);
    }
    lv_obj_move_foreground(fx);
    lv_obj_set_style_opa(fx,LV_OPA_COVER,0);
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a,fx);
    lv_anim_set_exec_cb(&a,sparkle_opa);
    lv_anim_set_values(&a,LV_OPA_COVER,LV_OPA_TRANSP);
    lv_anim_set_duration(&a,220);
    lv_anim_start(&a);
}
