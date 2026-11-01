#include "desk_buddy.h"
#include "setup_page.h"
#include "questie_bridge.h"
#include "esphome/core/log.h"
#include "esphome/core/helpers.h"
#include "esphome/core/application.h"
#include "esphome/components/wifi/wifi_component.h"
#include "esphome/components/mqtt/mqtt_client.h"
#include "esphome/components/json/json_util.h"
#include <cstring>
#include <cstdlib>

namespace esphome::desk_buddy {
static const char *const TAG="desk_buddy";
void DeskBuddy::setup() {
  uint8_t chip=0,adc=0;
  pmic_ok=read_byte(3,&chip) && (chip&0xCF)==0x4A;
  if(pmic_ok && read_byte(0x30,&adc)) pmic_ok=write_byte(0x30,adc|1);
  status=pmic_ok?"Cekam na Home Assistant":"Chyba AXP2101";
  // Preserve factory rail voltages and charge current: battery capacity is unknown.
  network_pref_=global_preferences->make_preference<NetworkSettings>(0xB0470001,true);
  NetworkSettings saved;
  if(network_pref_.load(&saved) && saved.version==1 && saved.port &&
     memchr(saved.host,0,sizeof(saved.host)) && memchr(saved.user,0,sizeof(saved.user)) && memchr(saved.password,0,sizeof(saved.password))) {
    network_=saved; network_saved_=true;
    if(network_.host[0]) {
      auto *m=mqtt::global_mqtt_client;
      m->set_broker_address(network_.host);m->set_broker_port(network_.port);
      m->set_username(network_.user);m->set_password(network_.password);
    }
    for(int i=0;i<4;i++) if(memchr(saved.identities[i],0,37)&&buddy_logic::valid_identity(saved.identities[i])) identities[i]=saved.identities[i];
  }
  web_server_base::global_web_server_base->add_handler(this); // Includes configured authentication.
}
void DeskBuddy::dump_config() { ESP_LOGCONFIG(TAG,"Desk buddy: Waveshare 1.75, PMIC %s",pmic_ok?"OK":"FAILED"); }
void DeskBuddy::update() {
  const uint32_t now=millis();
  for(auto &p:presence) p.expire(now,uint32_t(presence_timeout*1000));
  if(awake_ && buddy_logic::elapsed(now,awake_at_,uint32_t(wake_seconds*1000))) awake_=false;
  if(!pmic_ok) return;
  uint8_t a=0,b=0,v[2]{},pct=0;
  if(!read_byte(0,&a)||!read_byte(1,&b)||!read_bytes(0x34,v,2)||!read_byte(0xA4,&pct)) {
    volts=percent=NAN;low_samples_=0;status="Chyba cteni baterie";return;
  }
  battery_present=a&8;usb=(a&32) && !(b&8);
  volts=battery_present ? (((v[0]&0x1F)<<8)|v[1])/1000.f : NAN;
  percent=battery_present && pct<=100 ? pct : NAN;
  if(buddy_logic::low_battery(volts,battery_present,usb,low_samples_,cutoff)) power_off();
}
void DeskBuddy::power_off() {
  uint8_t power;
  if(!pmic_ok || !read_byte(0x10,&power)) {status="Vypnuti selhalo";return;}
  global_preferences->sync();
  // AXP2101 software power-off. PWR and VBUS wake are hardware-controlled.
  if(!write_byte(0x10,power|1)) status="Vypnuti selhalo";
}
void DeskBuddy::set_zone(const std::string &zone) {
  const char *rule="CET-1CEST,M3.5.0,M10.5.0/3";
  if(zone=="UTC") rule="UTC0";
  else if(zone=="Europe/London") rule="GMT0BST,M3.5.0/1,M10.5.0";
  else if(zone=="Europe/Helsinki") rule="EET-2EEST,M3.5.0/3,M10.5.0/4";
  setenv("TZ",rule,1);tzset();invalidate();
}
float DeskBuddy::brightness() {
  if(!display_on) return 0;
  time_t now=::time(nullptr);
  if(now/60!=cached_minute_) {cached_phase_=schedule.phase(now);cached_minute_=now/60;}
  float value=automatic && cached_phase_>=0 ? schedule.levels[cached_phase_] : manual_brightness;
  if(awake_ || voice_busy) value=std::max(value,std::min(manual_brightness,35.f));
  return std::max(0.f,std::min(100.f,value));
}
std::string DeskBuddy::phase_name() {
  brightness();
  if(!automatic) return "rucni";
  const char *names[]={"rano","den","vecer","noc"};
  return cached_phase_<0 ? "cekam na cas" : names[cached_phase_];
}
std::string DeskBuddy::sun_text(bool rise) {
  time_t now=::time(nullptr); if(!buddy_logic::valid_time(now)) return "Cas neni synchronizovan";
  auto t=buddy_logic::event(buddy_logic::local(now),{rise?buddy_logic::SUNRISE:buddy_logic::SUNSET,0,0},schedule.latitude,schedule.longitude);
  if(!t) return "Dnes nenastava";
  auto d=buddy_logic::local(t);char s[16];strftime(s,sizeof(s),"%H:%M",&d);return s;
}
void DeskBuddy::interact(int emotion,uint32_t duration) {
  if(pages_[0] && !voice_busy) questie_effect(emotion==7?14:emotion==2?21:2,duration);
  awake_=true;awake_at_=millis();
}
void DeskBuddy::motion(float x,float y,float z) {
  if(buddy_logic::shaken(x,y,z,motion_threshold) && buddy_logic::elapsed(millis(),last_motion_,10000)) {
    last_motion_=millis();if(pages_[0] && !voice_busy) questie_effect(14,3500);
    if(motion_wake) {awake_=true;awake_at_=millis();}
  }
}
void DeskBuddy::set_identity(int slot,const std::string &value) {
  if(slot<0||slot>=4||!buddy_logic::valid_identity(value)) return;
  auto normalized=buddy_logic::lower(value);
  if(identities[slot]==normalized) return;
  identities[slot]=normalized;presence[slot]={};
  strcpy(network_.identities[slot],normalized.c_str());
  if(!network_pref_.save(&network_)) status="Chyba ulozeni BLE";
}
void DeskBuddy::observe(const std::string &mac,const std::string &uuid,int rssi) {
  if(!ble_on) return;
  for(size_t i=0;i<identities.size();i++) if(!identities[i].empty() && (identities[i]==buddy_logic::lower(mac)||identities[i]==buddy_logic::lower(uuid))) {
    bool was=presence[i].near;presence[i].observe(rssi,millis(),rssi_enter,rssi_hysteresis);
    if(!was&&presence[i].near) interact(1,6000);
  }
  // Discovery is bounded and only published at the text sensor polling interval.
  nearby=mac+" / "+std::to_string(rssi)+" dBm"+(uuid.empty()?"":" / "+uuid);
}
void DeskBuddy::touch_start(int x,int y) {start_x_=touch_x_=x;start_y_=touch_y_=y;touch_at_=millis();touch_voice_=voice_busy;awake_=true;awake_at_=millis();}
int DeskBuddy::touch_end() {
  return buddy_logic::touch_action(screen,touch_x_-start_x_,touch_y_-start_y_,
                                  touch_x_,touch_y_,millis()-touch_at_,voice_busy,touch_voice_);
}
bool DeskBuddy::canHandle(AsyncWebServerRequest *r) const {
  std::array<char,AsyncWebServerRequest::URL_BUF_SIZE> path{};
  return r->url_to(path)=="/setup";
}
void DeskBuddy::handleRequest(AsyncWebServerRequest *r) {
  if(r->method()==HTTP_GET) {r->send(200,"text/html; charset=utf-8",SETUP_PAGE);return;}
  if(r->method()!=HTTP_POST) {r->send(400,"text/plain","POST required");return;}
  // A custom header prevents cross-origin HTML forms from changing credentials.
  auto h=r->get_header("X-Buddy-Setup");
  if(!h.has_value() || *h!="1") {r->send(400,"text/plain","Missing setup header");return;}
  auto next=network_;
  const auto host=r->arg("mqtt_host"),user=r->arg("mqtt_user"),pass=r->arg("mqtt_password");
  const auto port=r->arg("mqtt_port"),ssid=r->arg("wifi_ssid"),wp=r->arg("wifi_password");
  bool mqtt_change=!host.empty(),wifi_change=!ssid.empty();
  unsigned p=0;bool valid_port=!port.empty()&&port.size()<=5;
  for(char c:port) {if(c<'0'||c>'9') valid_port=false;else p=p*10+c-'0';}
  bool valid_host=host.size()<sizeof(next.host);
  for(char c:host) if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='.'||c=='-'||c==':')) valid_host=false;
  if((!mqtt_change&&!wifi_change) || (mqtt_change&&(!valid_host||!valid_port||p<1||p>65535||user.size()>=sizeof(next.user)||pass.size()>=sizeof(next.password))) ||
      (wifi_change&&(ssid.size()>32||(!wp.empty()&&(wp.size()<8||wp.size()>63))))) {r->send(400,"text/plain; charset=utf-8","Neplatny host, port, SSID nebo heslo.");return;}
  if(mqtt_change) {
    strcpy(next.host,host.c_str());strcpy(next.user,user.c_str());next.port=p;
    // Password is always explicitly supplied; blank means no MQTT password.
    strcpy(next.password,pass.c_str());
    if(!network_pref_.save(&next)||!global_preferences->sync()) {r->send(500,"text/plain","NVS save failed");return;}
    network_=next;
  }
  if(wifi_change) wifi::global_wifi_component->save_wifi_sta(ssid,wp);
  global_preferences->sync();
  r->send(200,"text/plain; charset=utf-8","Ulozeno. Zarizeni se restartuje.");
  set_timeout("network_restart",1200,[]{App.safe_reboot();});
}
} // namespace esphome::desk_buddy
