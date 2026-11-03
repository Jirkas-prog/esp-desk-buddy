#include "desk_buddy.h"
#include "questie_bridge.h"
#include <cmath>

namespace esphome::desk_buddy {
namespace {
constexpr const char *FIXED_LABELS[]={"HLASOVY ASISTENT","STOLNI KAMARAD","PODRZ 2 s PRO VYPNUTI","Zapnuti: USB / PWR","1 / 3    HODINY","2 / 3    HLAS","3 / 3    NAPAJENI"};
constexpr uint32_t ACCENT=0x00FFEA, MUTED=0x899BA4;
lv_obj_t *label(lv_obj_t *parent,const char *text,int x,int y,const lv_font_t *font,uint32_t color=ACCENT) {
  auto *o=lv_label_create(parent);
  lv_label_set_text(o,text);
  lv_obj_set_style_text_font(o,font,0);
  lv_obj_set_style_text_color(o,lv_color_hex(color),0);
  lv_obj_remove_flag(o,LV_OBJ_FLAG_CLICKABLE);
  lv_obj_align(o,LV_ALIGN_CENTER,x-233,y-233);
  return o;
}
void visible(lv_obj_t *o,bool show) {
  if(show) lv_obj_remove_flag(o,LV_OBJ_FLAG_HIDDEN);
  else lv_obj_add_flag(o,LV_OBJ_FLAG_HIDDEN);
}
lv_obj_t *line(lv_obj_t *parent,lv_point_precise_t *points,int width,uint32_t color) {
  auto *o=lv_line_create(parent);
  lv_line_set_points(o,points,2);
  lv_obj_set_style_line_width(o,width,0);
  lv_obj_set_style_line_color(o,lv_color_hex(color),0);
  lv_obj_set_style_line_rounded(o,true,0);
  return o;
}
}
void DeskBuddy::init_ui(font::Font *small,font::Font *large,font::Font *date,font::Font *controls) {
  if(pages_[0]) return;
  for(auto &p:pages_) {
    p=lv_obj_create(nullptr);
    lv_obj_set_style_bg_color(p,lv_color_black(),0);
    lv_obj_set_style_bg_opa(p,LV_OPA_COVER,0);
    lv_obj_set_style_pad_all(p,0,0);
    lv_obj_set_style_border_width(p,0,0);
    lv_obj_remove_flag(p,LV_OBJ_FLAG_SCROLLABLE);
  }
  init_robot_eyes(pages_[0]);
  voice_label_=label(pages_[0],"",233,402,small->get_lv_font(),MUTED);
  clock_time_=label(pages_[1],"--:--",233,225,large->get_lv_font());
  clock_seconds_=label(pages_[1],"",233,295,date->get_lv_font(),MUTED);
  clock_date_=label(pages_[1],tr("CEKAM NA CAS"),233,353,date->get_lv_font(),MUTED);
  analog_=lv_obj_create(pages_[1]);
  lv_obj_remove_style_all(analog_);
  lv_obj_set_size(analog_,466,466);
  lv_obj_remove_flag(analog_,LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(analog_,LV_OBJ_FLAG_CLICKABLE);
  static lv_point_precise_t tick_points[60][2];
  for(int i=0;i<60;i++) {
    float a=i*buddy_logic::PI/30;
    int inner=i%5?166:153;
    tick_points[i][0]={233+std::sin(a)*inner,220-std::cos(a)*inner};
    tick_points[i][1]={233+std::sin(a)*175,220-std::cos(a)*175};
    line(analog_,tick_points[i],i%5?2:4,MUTED);
  }
  for(int i=0;i<3;i++) {
    hand_points_[i][0]={233,220};hand_points_[i][1]={233,220};
    hands_[i]=line(analog_,hand_points_[i],i==2?2:6,i==2?0xFF7855:ACCENT);
  }
  lv_obj_move_foreground(clock_date_);
  translated_labels_[0]=label(pages_[2],tr("HLASOVY ASISTENT"),233,90,date->get_lv_font());
  mic_label_=label(pages_[2],tr("MIC ZAP"),142,192,small->get_lv_font());
  speaker_label_=label(pages_[2],tr("ZVUK ZAP"),327,192,small->get_lv_font());
  volume_label_=label(pages_[2],"60%",233,275,controls->get_lv_font());
  label(pages_[2],"-",133,340,controls->get_lv_font(),MUTED);
  label(pages_[2],"+",333,340,controls->get_lv_font(),MUTED);
  translated_labels_[1]=label(pages_[3],tr("STOLNI KAMARAD"),233,100,date->get_lv_font());
  battery_label_=label(pages_[3],"",233,173,small->get_lv_font());
  power_label_=label(pages_[3],"",233,218,small->get_lv_font(),MUTED);
  translated_labels_[2]=label(pages_[3],tr("PODRZ 2 s PRO VYPNUTI"),233,275,small->get_lv_font());
  translated_labels_[3]=label(pages_[3],tr("Zapnuti: USB / PWR"),233,318,small->get_lv_font(),MUTED);
  for(int page=1;page<4;page++) translated_labels_[page+3]=label(pages_[page],page==1?tr("1 / 3    HODINY"):page==2?tr("2 / 3    HLAS"):tr("3 / 3    NAPAJENI"),233,435,small->get_lv_font(),MUTED);
}
void DeskBuddy::set_language(bool czech) {
  czech_=czech;
  for (size_t i=0;i<translated_labels_.size();++i)
    if (translated_labels_[i]) lv_label_set_text(translated_labels_[i],tr(FIXED_LABELS[i]));
  labels_at_=ui_at_-1000; // Refresh live labels without recreating screens or interrupting voice.
}
void DeskBuddy::wake_reaction() {
  voice_busy=true;voice_phase=1;status="Posloucham";
  if(!pages_[0]) return;
  questie_voice(1);
  last_voice_phase_=1;labels_at_=0;
}
void DeskBuddy::loop() {
  if(!pages_[0] || !buddy_logic::elapsed(millis(),ui_at_,100)) return;
  ui_at_=millis();
  // Show the reaction over any page, then return to the user's selected page.
  bool attention=voice_busy;
  int speaking=voice_busy?voice_phase:0;
  if(speaking!=last_voice_phase_) {
    questie_voice(speaking);
    last_voice_phase_=speaking;labels_at_=0;
  }
  int active_screen=attention?0:screen;
  bool changed=shown_screen_!=active_screen;
  if(changed) {shown_screen_=active_screen;lv_screen_load(pages_[active_screen]);}
  if(!attention && (shape!=last_shape_||color!=last_color_)) {
    questie_set_base(shape,color);last_shape_=shape;last_color_=color;
  }
  bool idle=idle_emotions && base_mood==0 && !attention;
  if(idle!=last_idle_||eye_size!=last_eye_size_||animation_speed!=last_speed_) {
    questie_options(idle,eye_size,animation_speed);
    last_idle_=idle;last_eye_size_=eye_size;last_speed_=animation_speed;
  }
  if(!voice_busy && (base_mood!=last_mood_ || (base_mood && buddy_logic::elapsed(ui_at_,mood_at_,8000)))) {
    if(base_mood) {
      if(questie_effect(base_mood,6500)) {last_mood_=base_mood;mood_at_=ui_at_;}
    }
    else if(last_mood_>0) questie_set_base(shape,color);
    if(!base_mood) {last_mood_=base_mood;mood_at_=ui_at_;}
  }
  if(!changed && !buddy_logic::elapsed(ui_at_,labels_at_,1000)) return;
  labels_at_=ui_at_;
  lv_label_set_text(voice_label_,voice_busy?(voice_phase==1?tr("POSLOUCHAM"):voice_phase==2?tr("PREMYSLIM"):tr("ODPOVIDAM")):mic_on?"":tr("MIKROFON VYPNUT"));
  lv_label_set_text(mic_label_,mic_on?tr("MIC ZAP"):tr("MIC VYP"));
  lv_label_set_text(speaker_label_,speaker_on?tr("ZVUK ZAP"):tr("ZVUK VYP"));
  lv_label_set_text_fmt(volume_label_,"%.0f%%",volume);
  lv_label_set_text_fmt(battery_label_,tr("Baterie %.0f%% / %.2f V"),percent,volts);
  lv_label_set_text(power_label_,usb?tr("Napajeni USB"):tr("Napajeni baterie"));
  if(screen!=1) return;
  time_t now=::time(nullptr);auto d=buddy_logic::local(now);
  bool valid=buddy_logic::valid_time(now),analog=valid&&clock_style==1;
  visible(analog_,analog);visible(clock_time_,!analog);visible(clock_seconds_,valid&&clock_style==2);
  if(!valid) {lv_label_set_text(clock_time_,"--:--");lv_label_set_text(clock_date_,tr("CEKAM NA CAS"));return;}
  char text[24];strftime(text,sizeof(text),"%H:%M",&d);lv_label_set_text(clock_time_,text);
  strftime(text,sizeof(text),"%S",&d);lv_label_set_text(clock_seconds_,text);
  strftime(text,sizeof(text),czech_?"%d. %m. %Y":"%Y-%m-%d",&d);lv_label_set_text(clock_date_,text);
  int ox=int((ui_at_/60000)%5)-2,oy=int((ui_at_/120000)%5)-2;
  lv_obj_align(clock_time_,LV_ALIGN_CENTER,ox,(clock_style==2?201:225)-233+oy);
  lv_obj_align(clock_date_,LV_ALIGN_CENTER,ox,(analog?411:353)-233+oy);
  if(analog) {
    float angles[]={(d.tm_hour%12+d.tm_min/60.f)*float(buddy_logic::PI)/6,
                    (d.tm_min+d.tm_sec/60.f)*float(buddy_logic::PI)/30,d.tm_sec*float(buddy_logic::PI)/30};
    int lengths[]={88,137,150};
    for(int i=0;i<3;i++) {
      hand_points_[i][1]={233+std::sin(angles[i])*lengths[i],220-std::cos(angles[i])*lengths[i]};
      lv_line_set_points(hands_[i],hand_points_[i],2);
    }
  }
}
} // namespace esphome::desk_buddy
