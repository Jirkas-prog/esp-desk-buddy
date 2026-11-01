// QuestieV3 geometry/effects, with quiet intervals and exclusive voice reactions.
#ifdef QUESTIE_INCLUDE_BRIDGE
extern "C" int dashboard_load_eye_tunable(const char *key,int value) {
    if(!strcmp(key,"eyes_effect_pause_min_ms")) return 5000;
    if(!strcmp(key,"eyes_effect_pause_max_ms")) return 6000;
    return value;
}
static lv_timer_t *buddy_effect_timer=nullptr;
void questie_set_base(int shape,int color) {
    if(s_effect_locked) return;
    if(buddy_effect_timer) {lv_timer_delete(buddy_effect_timer);buddy_effect_timer=nullptr;}
    apply_base_state(shape,color,cfg.anim_state_transition_ms,false);
    schedule_next_state_pick();
}
bool questie_effect(int effect,uint32_t duration) {
    if(!left_eye || effect<=0 || effect>=EFFECT_COUNT || s_effect_locked || s_effect_active ||
       int32_t(lv_tick_get()-s_next_action_ms)<0) return false;
    if(buddy_effect_timer) lv_timer_delete(buddy_effect_timer);
    s_effect_active=true;
    s_reaction_until_ms=lv_tick_get()+duration+600;
    EFFECTS[effect].fn(duration);
    buddy_effect_timer=lv_timer_create([](lv_timer_t *) {
        buddy_effect_timer=nullptr;
        if(left_eye && !s_finger_down) return_to_base_state(cfg.anim_settle_ms);
    },duration+300,nullptr);
    lv_timer_set_repeat_count(buddy_effect_timer,1);
    return true;
}
void questie_voice(int phase) {
    if(!left_eye) return;
    if(buddy_effect_timer) {lv_timer_delete(buddy_effect_timer);buddy_effect_timer=nullptr;}
    s_effect_locked=false;
    s_finger_down=false;s_tap_count=0;s_closed=false;s_sleeping=false;
    if(!phase) {return_to_base_state(cfg.anim_settle_ms);return;}
    prepare_new_shape();
    set_eye_color(cfg.color_eye);
    set_eye_border_visible(true);
    set_x_left_cb(left_eye,0);set_x_right_cb(right_eye,0);
    set_y_cb(left_eye,0);set_y_cb(right_eye,0);
    EFFECTS[phase==1?4:phase==2?8:2].fn(60000);
    if(phase==1) {
        // Keep Questie's wide attentive eyes, without the surprise bounce.
        lv_anim_delete(left_eye,set_y_cb);lv_anim_delete(right_eye,set_y_cb);
        set_y_cb(left_eye,0);set_y_cb(right_eye,0);
    }
    s_effect_active=true;
    s_effect_locked=true;
}
void questie_options(bool idle,float scale,float speed) {
    if(!s_root) return;
    if(s_brain_timer) {
        if(idle) lv_timer_resume(s_brain_timer);
        else lv_timer_pause(s_brain_timer);
    }
    lv_obj_set_style_transform_scale(s_root,int(scale*256),0);
    // Preserve all original timings at 1.0, expose speed through the web control.
    static const auto original=cfg;
    #define SPEED(field) cfg.field=uint32_t(original.field/speed)
    SPEED(anim_state_transition_ms); SPEED(anim_settle_ms);
    SPEED(anim_blink_close_ms); SPEED(anim_blink_open_ms); SPEED(anim_dbl_blink_open_ms);
    SPEED(anim_look_ms); SPEED(anim_look_smooth_ms); SPEED(anim_happy_resize_ms);
    SPEED(anim_happy_bounce_ms); SPEED(anim_sad_resize_ms); SPEED(anim_wink_resize_ms);
    SPEED(anim_surprise_resize_ms); SPEED(anim_surprise_bounce_ms); SPEED(anim_sleepy_resize_ms);
    SPEED(anim_sleepy_bounce_ms); SPEED(anim_suspicious_resize_ms); SPEED(anim_suspicious_scan_ms);
    SPEED(anim_love_resize_ms); SPEED(anim_love_beat_ms); SPEED(anim_dizzy_resize_ms);
    SPEED(anim_dizzy_x_ms); SPEED(anim_dizzy_y_ms); SPEED(anim_scan_resize_ms); SPEED(anim_scan_move_ms);
    SPEED(anim_glitch_x_ms); SPEED(anim_glitch_y_ms); SPEED(anim_glitch_h_ms); SPEED(anim_glitch_color_ms);
    SPEED(anim_slow_glitch_x_ms); SPEED(anim_slow_glitch_y_ms); SPEED(anim_slow_glitch_color_ms);
    SPEED(anim_narrow_resize_ms); SPEED(anim_narrow_bounce_ms); SPEED(anim_circle_resize_ms);
    SPEED(anim_rolling_intro_ms); SPEED(anim_triangle_fade_ms); SPEED(anim_angry_resize_ms);
    SPEED(anim_angry_shake_ms); SPEED(anim_eyebrow_raise_ms); SPEED(anim_eyebrow_lower_ms);
    #undef SPEED
}
#endif
