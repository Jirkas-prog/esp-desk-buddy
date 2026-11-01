#include <cassert>
#include <cstdio>
#include <initializer_list>
#include "../../components/desk_buddy/robot_eyes.cpp"
static void advance(uint32_t ms) {
    for(uint32_t t=0;t<ms;t+=10) {lv_tick_inc(10);lv_timer_handler();}
}
static void attentive() {
    assert(s_effect_locked && s_effect_active);
    assert(s_eye_h_l==cfg.eye_height_surprise && s_eye_h_r==cfg.eye_height_surprise);
    assert(s_eye_w_l==cfg.eye_width_surprise && s_eye_w_r==cfg.eye_width_surprise);
    assert(s_off_x==0 && s_off_y==0);
    assert(!lv_obj_has_flag(left_eye,LV_OBJ_FLAG_HIDDEN));
}
int main() {
    lv_init();
    auto *display=lv_display_create(466,466);
    static uint16_t pixels[466*20];
    lv_display_set_buffers(display,pixels,nullptr,sizeof(pixels),LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(display,[](lv_display_t *d,const lv_area_t *,uint8_t *) {lv_display_flush_ready(d);});
    auto *page=lv_obj_create(nullptr);
    lv_screen_load(page);init_robot_eyes(page);
    // Tests should not depend on the workstation's time of day.
    cfg.night_start_hour=24;cfg.night_end_hour=0;
    questie_options(false,1,1);advance(1000);
    triangle_shape(6000);
    int previous_opacity=0;
    for(int t=0;t<600;t+=10) {
        advance(10);
        int opacity=lv_obj_get_style_opa(s_tri_l,LV_PART_MAIN);
        assert(opacity>=previous_opacity); // No uint8 opacity wrap during overshoot.
        previous_opacity=opacity;
    }
    assert(previous_opacity==255);
    lv_obj_update_layout(page);
    lv_area_t root,left,right;
    lv_obj_get_coords(s_root,&root);lv_obj_get_coords(s_tri_l,&left);lv_obj_get_coords(s_tri_r,&right);
    assert(abs(left.x1+left.x2-(root.x1+root.x2-2*cfg.eye_spacing))<=1);
    assert(abs(right.x1+right.x2-(root.x1+root.x2+2*cfg.eye_spacing))<=1);
    assert(abs(left.y1+left.y2-(root.y1+root.y2))<=1);
    for(auto *canvas : {s_tri_l,s_tri_r}) {
        auto *d=lv_canvas_get_image(canvas);
        int partial=0,solid=0;
        for(int y=0;y<cfg.tri_canvas_h;y++) for(int x=0;x<cfg.tri_canvas_w;x++) {
            const auto *p=d->data+y*d->header.stride+x*4;
            if(p[3]>0 && p[3]<255) partial++;
            if(p[3]==255) solid++;
            // Rounded tips/edges fit the buffer without clipped corners.
            if(x==0 || y==0 || x==cfg.tri_canvas_w-1 || y==cfg.tri_canvas_h-1) assert(p[3]==0);
            if(p[3]==255) assert(p[0]==(cfg.color_eye&255) && p[1]==((cfg.color_eye>>8)&255) && p[2]==((cfg.color_eye>>16)&255));
        }
        assert(partial>50 && solid>2000);
    }
    questie_voice(1);advance(500);
    assert(!s_tri_l && !s_tri_r && !s_tri_buf_l && !s_tri_buf_r);
    attentive();
    questie_voice(0);advance(1000);
    assert(EFFECT_PAUSE_MIN_MS==5000 && EFFECT_PAUSE_MAX_MS==6000);
    for(int effect=1;effect<EFFECT_COUNT;effect++) {
        questie_voice(0);advance(1000);
        EFFECTS[effect].fn(6000);
        advance(100);
        // An old base-return timer must not cancel this listening state.
        lv_timer_t *old=lv_timer_create([](lv_timer_t *) {return_to_base_state(500);},2000,nullptr);
        lv_timer_set_repeat_count(old,1);
        questie_voice(1);advance(1000);attentive();
        for(int second=0;second<15;second++) {
            assert(!questie_effect(14,3500));
            root_press_cb(nullptr);root_pressing_cb(nullptr);root_release_cb(nullptr);
            advance(1000);attentive();
        }
    }
    questie_voice(2);advance(500);assert(s_effect_locked);
    questie_voice(3);advance(500);assert(s_effect_locked);
    questie_voice(0);
    assert(!s_effect_locked);
    const uint32_t deadline=s_next_action_ms;
    assert(deadline-lv_tick_get()>=cfg.anim_settle_ms+5000);
    assert(deadline-lv_tick_get()<=cfg.anim_settle_ms+6000);
    advance(cfg.anim_settle_ms+4990);
    assert(!questie_effect(14,3500));
    advance(1020);
    assert(questie_effect(14,3500));
    advance(3800);
    assert(!s_effect_active);
    assert(s_next_action_ms-lv_tick_get()>=cfg.anim_settle_ms+4990);
    g_blank_effect_percent=0;
    int previous=-1;
    for(int i=0;i<100;i++) {
        run_random_effect();
        assert(s_previous_random_effect!=previous);
        previous=s_previous_random_effect;
        advance(10000);
    }
    puts("PASS: anti-aliased rounded triangles, centering, monotonic fade, cleanup; all 21 effects interrupted by stable listening; quiet intervals and no consecutive repeats.");
}
