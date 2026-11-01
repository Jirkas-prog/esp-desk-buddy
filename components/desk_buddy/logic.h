#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <ctime>
#include <string>

namespace buddy_logic {
constexpr double PI = 3.14159265358979323846;
inline bool elapsed(uint32_t now, uint32_t then, uint32_t duration) { return uint32_t(now-then) >= duration; }
// A touch during voice always cancels; never activate the page hidden by the eyes.
inline int touch_action(int &screen,int dx,int dy,int x,int y,uint32_t held,bool voice,bool started_during_voice) {
  if(voice) return 8;
  if(started_during_voice) return 0;
  if(std::abs(dx)>70 && std::abs(dx)>std::abs(dy)) {screen=(screen+(dx<0?1:3))%4;return 0;}
  if(std::abs(dy)>70 || screen==0) return 0;
  if(screen==1) return 6;
  if(screen==2) return y<240 ? (x<233?1:2) : (x<233?3:4);
  return screen==3 && held>=2000 ? 5 : 0;
}
// QMI8658 publishes acceleration in g; the adjustable threshold is in m/s².
inline bool shaken(float x,float y,float z,float threshold) {
  const float magnitude=std::sqrt(x*x+y*y+z*z);
  return std::isfinite(magnitude) && std::abs(magnitude-1.f)*9.80665f>threshold;
}
inline bool valid_time(time_t t) { return t >= 1704067200; }
inline bool leap(int y) { return y%4 == 0 && (y%100 != 0 || y%400 == 0); }
inline tm local(time_t t) { tm d{};
#ifdef _WIN32
  localtime_s(&d, &t);
#else
  localtime_r(&t, &d);
#endif
  return d;
}
inline int day_key(const tm &d) { return (d.tm_year+1900)*10000+(d.tm_mon+1)*100+d.tm_mday; }
inline tm shift_date(tm d,int n) { d.tm_hour=12; d.tm_min=d.tm_sec=0; d.tm_mday+=n; d.tm_isdst=-1; return local(mktime(&d)); }
// Adapted from project 41 schedule.h: NOAA approximation, true UTC epoch.
inline time_t sun(const tm &date,double lat,double lon,bool rise) {
  if (!std::isfinite(lat) || !std::isfinite(lon) || std::abs(lat)>90 || std::abs(lon)>180) return 0;
  const double rad=PI/180;
  double minutes=720-4*lon;
  for(int i=0;i<3;i++) {
    double g=2*PI/(leap(date.tm_year+1900)?366:365)*(date.tm_yday+(minutes/60-12)/24);
    double eq=229.18*(.000075+.001868*cos(g)-.032077*sin(g)-.014615*cos(2*g)-.040849*sin(2*g));
    double dec=.006918-.399912*cos(g)+.070257*sin(g)-.006758*cos(2*g)+.000907*sin(2*g)-.002697*cos(3*g)+.00148*sin(3*g);
    double c=(cos(90.833*rad)-sin(lat*rad)*sin(dec))/(cos(lat*rad)*cos(dec));
    if(!std::isfinite(c) || c < -1 || c > 1) return 0;
    minutes=720-4*(lon+(rise?1:-1)*acos(c)/rad)-eq;
  }
  int days=date.tm_yday;
  for(int y=1970;y<date.tm_year+1900;y++) days+=leap(y)?366:365;
  return time_t(int64_t(days)*86400)+time_t(lround(minutes*60));
}
enum Event { FIXED, SUNRISE, SUNSET };
struct Point { Event kind=FIXED; int minute=0, offset=0; };
inline time_t event(const tm &date, const Point &p, double lat, double lon) {
  if(p.kind==FIXED) { tm d=date; d.tm_hour=p.minute/60; d.tm_min=p.minute%60; d.tm_sec=0; d.tm_isdst=-1; return mktime(&d)+p.offset*60; }
  for(int s=-1;s<=1;s++) {
    time_t t=sun(shift_date(date,s),lat,lon,p.kind==SUNRISE);
    if(t && day_key(local(t))==day_key(date)) return t+p.offset*60;
  }
  return 0; // Polar day/night: no fabricated solar event.
}
struct Schedule {
  double latitude=50.0,longitude=14.0;
  Point morning{SUNRISE,420,-30}, day{SUNRISE,480,0}, evening{SUNSET,1080,0}, night{FIXED,1380,0};
  float levels[4]={35,70,15,0};
  int phase(time_t now) const {
    if(!valid_time(now)) return -1;
    auto d=local(now); time_t latest=0; int selected=-1;
    const Point points[]={morning,day,evening,night};
    // Evaluate neighboring LOCAL dates, including offsets across midnight/DST.
    // Later phase wins exact ties. Missing polar events are skipped.
    for(int n=-2;n<=1;n++) for(int i=0;i<4;i++) {
      time_t t=event(shift_date(d,n),points[i],latitude,longitude);
      if(t && t<=now && t>=latest) { latest=t; selected=i; }
    }
    return selected;
  }
};
struct Presence {
  bool seen=false,near=false; uint32_t last=0; float rssi=-100;
  void observe(int value,uint32_t now,float enter,float hysteresis) {
    rssi=seen ? rssi*.72f+value*.28f : value; seen=true; last=now;
    if(!near && rssi>=enter) near=true;
    else if(near && rssi<enter-hysteresis) near=false;
  }
  void expire(uint32_t now,uint32_t timeout) { if(seen && elapsed(now,last,timeout)) {seen=false;near=false;rssi=-100;} }
};
inline bool low_battery(float volts,bool connected,bool usb,unsigned &samples,float threshold) {
  if(!connected || usb || !std::isfinite(volts) || volts<2.5f || volts>4.6f || volts>threshold) { samples=0; return false; }
  return ++samples>=10; // sustained 10 s, never a single noisy ADC sample
}
inline bool valid_identity(const std::string &s) {
  if(s.empty()) return true;
  auto hex=[](char c){return (c>='0'&&c<='9')||(c>='a'&&c<='f')||(c>='A'&&c<='F');};
  if(s.size()==17) {for(size_t i=0;i<s.size();i++) if(i%3==2 ? s[i]!=':' : !hex(s[i])) return false; return true;}
  // UUID alone is enough with a unique Companion App iBeacon UUID per phone.
  if(s.size()==36) {for(size_t i=0;i<s.size();i++) if(i==8||i==13||i==18||i==23 ? s[i]!='-' : !hex(s[i])) return false; return true;}
  return false;
}
inline std::string lower(std::string s) {for(char &c:s) if(c>='A'&&c<='Z') c+=32; return s;}
inline float smooth(float t) { t=std::max(0.f,std::min(1.f,t)); return t*t*(3-2*t); }
} // namespace buddy_logic
