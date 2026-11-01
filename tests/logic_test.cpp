#include "../components/desk_buddy/logic.h"
#include <cassert>
#include <cstdlib>
#include <iostream>
using namespace buddy_logic;
static time_t stamp(int year,int month,int day,int hour,int minute=0) {
  tm d{};d.tm_year=year-1900;d.tm_mon=month-1;d.tm_mday=day;d.tm_hour=hour;d.tm_min=minute;d.tm_isdst=-1;return mktime(&d);
}
int main() {
  for(int page=0;page<4;page++) {
    int screen=page;
    for(bool started : {false,true}) {
      assert(touch_action(screen,0,0,120,180,100,true,started)==8);
      assert(touch_action(screen,-200,0,120,180,2500,true,started)==8);
      assert(screen==page);
    }
    // The reply can finish while the finger is still on the screen.
    assert(touch_action(screen,0,0,120,180,2500,false,true)==0);
    assert(screen==page);
  }
  int screen=0;
  assert(touch_action(screen,-100,0,120,180,100,false,false)==0 && screen==1);
  assert(touch_action(screen,0,0,120,180,100,false,false)==6);
  screen=2;
  assert(touch_action(screen,0,0,120,180,100,false,false)==1);
  assert(touch_action(screen,0,0,320,180,100,false,false)==2);
  assert(touch_action(screen,0,0,120,340,100,false,false)==3);
  assert(touch_action(screen,0,0,320,340,100,false,false)==4);
  screen=3;
  assert(touch_action(screen,0,0,120,180,1999,false,false)==0);
  assert(touch_action(screen,0,0,120,180,2000,false,false)==5);
  assert(!shaken(0,0,1,3));assert(!shaken(0,-1,0,3));assert(!shaken(1,0,0,3));
  assert(!shaken(.57735f,.57735f,.57735f,3));assert(!shaken(.03f,.02f,1.04f,3));
  assert(shaken(0,0,1.5f,3));assert(shaken(0,0,0,3));
  assert(!shaken(NAN,0,1,3));assert(!shaken(INFINITY,0,1,3));
#ifdef _WIN32
  _putenv_s("TZ","UTC0");_tzset();
#else
  setenv("TZ","UTC0",1);tzset();
#endif
  // Prague summer sunrise ~02:52 UTC, sunset ~19:16 UTC, tolerance 12 min.
  auto d=local(stamp(2026,6,21,12));
  auto rise=sun(d,50.08,14.42,true),set=sun(d,50.08,14.42,false);
  assert(std::abs(rise-stamp(2026,6,21,2,52))<12*60);
  assert(std::abs(set-stamp(2026,6,21,19,16))<12*60);
  assert(!sun(d,89,0,true));assert(!sun(d,NAN,0,true));
  Schedule s;s.morning={FIXED,360,0};s.day={FIXED,480,0};s.evening={FIXED,1080,0};s.night={FIXED,1380,0};
  assert(s.phase(0)==-1);
  assert(s.phase(stamp(2026,10,23,5,59))==3);
  assert(s.phase(stamp(2026,10,23,6))==0);
  assert(s.phase(stamp(2026,10,23,8))==1);
  assert(s.phase(stamp(2026,10,23,18))==2);
  assert(s.phase(stamp(2026,10,23,23))==3);
  s.night={FIXED,1380,120}; // Yesterday's 23:00 + 120 min = today 01:00.
  assert(s.phase(stamp(2026,10,23,0,59))==2);
  assert(s.phase(stamp(2026,10,23,1))==3);
  s.day=s.morning;assert(s.phase(stamp(2026,10,23,6))==1);
  Presence p;p.observe(-60,100,-65,8);assert(p.near);
  p.observe(-69,200,-65,8);assert(p.near);
  for(int i=0;i<20;i++) p.observe(-90,300+i,-65,8);
  assert(!p.near);p.expire(40000,30000);assert(!p.seen);
  p.observe(-60,UINT32_MAX-100,-65,8);p.expire(40000,30000);assert(!p.near);
  unsigned samples=0;
  for(int i=0;i<9;i++) assert(!low_battery(3.19,true,false,samples,3.2));
  assert(low_battery(3.19,true,false,samples,3.2));
  assert(!low_battery(3.0,true,true,samples,3.2)&&samples==0);
  assert(!low_battery(0,true,false,samples,3.2));
  assert(!low_battery(NAN,true,false,samples,3.2));
  assert(!low_battery(3.0,false,false,samples,3.2));
  assert(valid_identity("01:AB:cd:12:34:56"));
  assert(valid_identity("00112233-4455-6677-8899-aabbccddeeff"));
  assert(!valid_identity("garbage"));assert(!valid_identity("gg:11:22:33:44:55"));
  assert(smooth(-1)==0&&smooth(2)==1&&smooth(.5f)==.5f);
  std::cout<<"PASS: voice touch cancellation and hidden controls, solar events, midnight, phase boundaries, polar day, BLE hysteresis/timeout/rollover, battery, identity validation\n";
}
