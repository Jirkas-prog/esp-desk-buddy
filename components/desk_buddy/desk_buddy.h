#pragma once
#include "esphome/core/component.h"
#include "esphome/core/preferences.h"
#include "esphome/components/i2c/i2c.h"
#include "esphome/components/display/display.h"
#include "esphome/components/lvgl/lvgl_esphome.h"
#include "esphome/components/web_server_base/web_server_base.h"
#include "logic.h"
#include <array>

namespace esphome::desk_buddy {
class DeskBuddy : public PollingComponent, public i2c::I2CDevice, public AsyncWebHandler {
 public:
  void setup() override;
  void update() override;
  float get_setup_priority() const override { return 250; }
  void dump_config() override;
  bool canHandle(AsyncWebServerRequest *request) const override;
  void handleRequest(AsyncWebServerRequest *request) override;
  void init_ui(font::Font *small,font::Font *large,font::Font *date,font::Font *controls);
  void loop() override;
  void wake_reaction();
  void touch_start(int x,int y);
  void touch_move(int x,int y) { touch_x_=x; touch_y_=y; }
  int touch_end();
  void interact(int emotion=1,uint32_t duration=5000);
  void motion(float x,float y,float z);
  void observe(const std::string &mac,const std::string &uuid,int rssi);
  void set_identity(int slot,const std::string &value);
  void set_zone(const std::string &zone);
  void power_off();
  void invalidate() { cached_minute_=-1; }
  float brightness();
  std::string sun_text(bool rise);
  std::string phase_name();
  std::string status="Startuji";
  buddy_logic::Schedule schedule;
  std::array<buddy_logic::Presence,4> presence;
  std::array<std::string,4> identities;
  std::string nearby;
  std::string location="Stul";
  bool automatic=true,display_on=true,motion_wake=true,idle_emotions=true;
  bool mic_on=true,speaker_on=true,voice_busy=false,ble_on=true;
  int voice_phase=0,screen=0,clock_style=0,shape=0,base_mood=0,color=0;
  float manual_brightness=70,volume=60,eye_size=1,animation_speed=1;
  float rssi_enter=-65,rssi_hysteresis=8,cutoff=3.2,motion_threshold=3;
  float presence_timeout=30,wake_seconds=30;
  float volts=NAN,percent=NAN;
  bool battery_present=false,usb=false,pmic_ok=false;
 protected:
  struct NetworkSettings {
    uint32_t version=1;
    char host[97]{},user[65]{},password[97]{};
    char identities[4][37]{};
    uint16_t port=1883;
  } network_;
  ESPPreferenceObject network_pref_;
  bool network_saved_=false;
  unsigned low_samples_=0;
  int cached_phase_=-1;
  time_t cached_minute_=-1;
  uint32_t effect_at_=0,effect_duration_=0,awake_at_=0;
  bool awake_=false;
  int effect_mood_=0;
  int start_x_=0,start_y_=0,touch_x_=0,touch_y_=0;
  uint32_t touch_at_=0,last_motion_=0;
  bool touch_voice_=false;
  lv_obj_t *pages_[4]{},*clock_time_{},*clock_date_{},*clock_seconds_{},*analog_{},*hands_[3]{};
  lv_obj_t *mic_label_{},*speaker_label_{},*volume_label_{},*battery_label_{},*power_label_{},*voice_label_{};
  lv_point_precise_t hand_points_[3][2]{};
  int shown_screen_=-1,last_shape_=-1,last_color_=-1,last_mood_=-1,last_voice_phase_=-1;
  float last_eye_size_=-1,last_speed_=-1;
  bool last_idle_=true;
  uint32_t ui_at_=0,labels_at_=0,mood_at_=0;
};
} // namespace esphome::desk_buddy
