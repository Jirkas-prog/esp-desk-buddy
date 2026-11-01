#include <cassert>
#include <cstdio>
#include <cstring>
#include "../../components/desk_buddy/robot_eyes.cpp"
static uint16_t frame[466*466];
static void advance(uint32_t ms) {
    for(uint32_t t=0;t<ms;t+=10) {lv_tick_inc(10);lv_timer_handler();}
}
static void save(const char *path) {
    lv_refr_now(nullptr);
    auto *f=fopen(path,"wb");assert(f);
    fprintf(f,"P6\n466 466\n255\n");
    for(auto p:frame) {
        uint8_t rgb[]={uint8_t(((p>>11)&31)*255/31),uint8_t(((p>>5)&63)*255/63),uint8_t((p&31)*255/31)};
        fwrite(rgb,1,3,f);
    }
    fclose(f);
}
int main(int argc,char **argv) {
    assert(argc==3);
    lv_init();
    auto *display=lv_display_create(466,466);
    lv_display_set_color_format(display,LV_COLOR_FORMAT_RGB565);
    static uint16_t pixels[466*20];
    lv_display_set_buffers(display,pixels,nullptr,sizeof(pixels),LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(display,[](lv_display_t *d,const lv_area_t *a,uint8_t *p) {
        int width=lv_area_get_width(a);
        for(int y=a->y1;y<=a->y2;y++)
            memcpy(frame+y*466+a->x1,p+(y-a->y1)*width*2,width*2);
        lv_display_flush_ready(d);
    });
    auto *page=lv_obj_create(nullptr);
    lv_obj_remove_style_all(page);lv_obj_set_size(page,466,466);
    lv_obj_set_style_bg_opa(page,LV_OPA_COVER,0);
    lv_screen_load(page);init_robot_eyes(page);
    cfg.night_start_hour=24;cfg.night_end_hour=0;
    questie_options(false,1,1);advance(1000);save(argv[1]);
    triangle_shape(6000);advance(1000);save(argv[2]);
}
