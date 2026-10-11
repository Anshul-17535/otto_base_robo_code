#include "../Otto_ESP32S3/motion_engine.h"
#include <cassert>
#include <iostream>
#include <limits>
using namespace bobmotion;
static const float neutral[4]={90,90,90,90};
static Sample sensed(uint32_t t,float pitch=0,float roll=0) { return {true,true,t,pitch,roll}; }
static Sequence move(float angle=100) {
  Sequence s; strcpy(s.name,"test"); s.count=1;
  s.frames[0]={{angle,90,90,90},200,0}; return s;
}
int main() {
  assert(usableGravity(-515,21,-883));
  assert(!usableGravity(-521,0,0)); // recorded damaged/unsuitable gravity vector
  assert(!usableGravity(0,0,0));
  assert(!usableGravity(0,0,1962));

  {
    Engine e; const float idle[4]={90,90,90,31};
    assert(!e.arm(0,1,idle)); // old defaults cannot represent this assembly
    assert(captureNeutral(e.config,idle,3));
    assert(e.config.customNeutral && e.config.neutral[3]==31);
    assert(e.config.joint[3].low==28 && e.config.joint[3].high==34);
    assert(e.config.gains[0].kp==0 && e.config.gains[1].kp==0);
    assert(e.arm(0,1,idle));
    e.tick(20,sensed(20)); assert(e.output[3]==31); // arming cannot jump to 90
    Sequence s=move(); s.frames[0].angle[0]=91; s.frames[0].angle[3]=32;
    assert(e.play(s,1,20));
    for(uint32_t t=40;t<=2000;t+=20) {e.beat(1,t);e.tick(t,sensed(t));}
    assert(!e.playing && std::fabs(e.output[3]-32)<.01);
    s.frames[0].angle[3]=90; assert(!e.play(s,1,2020));
    Config c=e.config; c.joint[3].low=32; assert(!validConfig(c));
    c=e.config; const float bad[4]={90,90,90,0};
    assert(!captureNeutral(c,bad,3)); assert(c.neutral[3]==31);
    assert(!captureNeutral(c,idle,11));
    s.frames[0].angle[3]=32; assert(e.play(s,1,2020));
    e.tick(2020,sensed(2020,11)); assert(e.armed);          // one tilt spike tolerated
    e.tick(2040,sensed(2040,11)); e.tick(2060,sensed(2060,11));
    assert(!e.armed && !e.pid); // sustained tabletop tilt stops even with PID off
    assert(e.arm(2080,2,idle)); assert(e.play(s,1,2080));
    e.tick(2100,{true,false,2100,0,0}); assert(e.armed); // open-loop joint move ignores IMU dropouts

  }
  {
    Engine e; assert(!e.armed && !e.pid);
    assert(!e.play(move(),1,0));
    assert(e.arm(0,123,neutral)); assert(!e.arm(0,456,neutral));
    assert(!e.authorize(456,10)); assert(e.authorize(123,10));
    assert(!e.beat(123,LEASE_MS)); assert(!e.armed); // late heartbeat cannot resurrect motion
    assert(!e.pid && !e.playing);
  }
  {
    Engine e; e.arm(0,123,neutral);
    Sequence invalid=move(); invalid.expression=7; assert(!e.play(invalid,1,0));
    invalid.expression=-2; assert(!e.play(invalid,1,0));
    invalid.expression=5; assert(validSequence(invalid,e.config));
    invalid=move(); invalid.frames[0].angle[0]=121;
    assert(!e.play(invalid,1,0)); assert(!e.playing);
    invalid=move(); invalid.expression=7; assert(!e.play(invalid,1,0));
    invalid.expression=-2; assert(!e.play(invalid,1,0));
    invalid.expression=5; assert(validSequence(invalid,e.config));
    invalid=move(); invalid.frames[0].angle[0]=std::numeric_limits<float>::quiet_NaN();
    assert(!e.play(invalid,1,0));
    assert(e.play(move(110),2,0));
    float previous=e.output[0];
    for(uint32_t t=20;t<=1400;t+=20) {
      e.beat(123,t); e.tick(t,sensed(t));
      assert(std::fabs(e.output[0]-previous)<=0.801f);
      assert(e.output[0]>=60 && e.output[0]<=120); previous=e.output[0];
    }
    assert(!e.playing); assert(std::fabs(e.output[0]-110)<0.01f);
    const float held=e.output[0]; e.stop("test"); e.tick(2000,sensed(2000));
    assert(e.output[0]==held && e.base[0]==held); // stop never jumps to centre
  }
  {
    Engine e; e.config.slew=10; e.arm(0,123,neutral);
    Sequence s=move(100); s.frames[0].moveMs=100; s.frames[0].holdMs=500;
    e.play(s,1,0);
    for(uint32_t t=20;t<=1100;t+=20) { e.beat(123,t); e.tick(t,sensed(t)); }
    assert(e.playing); // hold time starts after the slow servo command arrives
    for(uint32_t t=1120;t<=1700;t+=20) { e.beat(123,t); e.tick(t,sensed(t)); }
    assert(!e.playing);
  }
  {
    Engine e; e.arm(100,7,neutral);
    assert(!e.enablePid(true,{false,true,100,0,0},100));
    assert(!e.enablePid(true,{true,false,100,0,0},100));
    assert(!e.enablePid(true,sensed(0),300));
    assert(e.enablePid(true,sensed(100),100));
    e.tick(120,sensed(120,10));
    assert(e.correction[0]<0 && e.output[0]<90 && e.output[2]<90);
    assert(e.output[1]==90 && e.output[3]==90);
    e.tick(140,sensed(120,10)); // same measurement must not integrate again
    assert(e.samples==1);
    e.tick(160,sensed(160,40)); assert(e.armed);   // one tilt spike tolerated
    e.tick(180,sensed(180,40)); e.tick(200,sensed(200,40));
    assert(!e.armed && !e.pid && !strcmp(e.reason,"tilt limit exceeded"));
  }
  {
    Engine e; e.arm(0,1,neutral); e.enablePid(true,sensed(0),0);
    for(uint32_t t=20;t<=260;t+=20) e.tick(t,sensed(0));
    assert(!e.armed && !strcmp(e.reason,"IMU unavailable or stale"));
    e.arm(300,1,neutral); e.tick(600,sensed(600));
    assert(!e.armed && !strcmp(e.reason,"control loop stalled"));
  }
  {
    Engine e; e.arm(0,1,neutral); e.play(move(110),1,0);
    Config c=e.config; c.joint[0].high=100;
    assert(!e.configure(c)); assert(e.config.joint[0].high==120);
    c=e.config; c.gains[0].ki=3; assert(!e.configure(c));
    c=e.config; c.slew=std::numeric_limits<float>::infinity(); assert(!e.configure(c));
    c=e.config; c.joint[0].low=121; assert(!e.configure(c));
  }
  {
    Pid p; Gains g={5,2,0};
    for(int i=0;i<1000;++i) assert(p.step(g,0,-10,.05,8,false)==8);
    assert(p.integral==0); // no accumulated wind-up at saturation
    assert(p.step(g,0,0,.05,8,false)==0);
    Pid d; Gains dg={0,0,1};
    d.step(dg,0,0,.05,8,false);
    assert(d.step(dg,10,0,.05,8,false)==0); // target change has no derivative kick
  }
  {
    Engine e; uint32_t start=UINT32_MAX-100;
    e.arm(start,1,neutral); e.play(move(),1,start);
    for(uint32_t i=20;i<=1000;i+=20) { e.beat(1,start+i); e.tick(start+i,sensed(start+i)); }
    assert(e.armed && !e.playing); // millis() wrap-around
  }
  {
    // Posture (tilt) sequences: the PID setpoint ramps to each target orientation,
    // `base` stays at neutral, and the joints move only through PID correction.
    Engine e; e.arm(0,9,neutral); assert(e.enablePid(true,sensed(0),0));
    Sequence s; strcpy(s.name,"nod"); s.mode=SEQ_TILT; s.count=2;
    s.frames[0]={{5,0,0,0},400,200};
    s.frames[1]={{0,0,0,0},400,200};
    assert(validSequence(s,e.config));
    assert(e.play(s,1,0));
    for(uint32_t t=20;t<=460;t+=20){ e.beat(9,t); e.tick(t,sensed(t,0)); }
    assert(std::fabs(e.target[0]-5)<0.01f && e.base[0]==90); // setpoint reached, base held at neutral
    assert(e.output[0]>90 && e.output[2]>90);               // joints driven purely by PID correction
    for(uint32_t t=480;t<=1400;t+=20){ e.beat(9,t); e.tick(t,sensed(t,0)); }
    assert(!e.playing && std::fabs(e.target[0])<0.01f);     // returned to level and finished
    Sequence bad=s; bad.frames[0].angle[0]=40; assert(!validSequence(bad,e.config));   // beyond ±15/cutoff
    bad=s; bad.frames[0].angle[1]=std::numeric_limits<float>::quiet_NaN(); assert(!validSequence(bad,e.config));
    bad=s; bad.mode=3; assert(!validSequence(bad,e.config)); // unknown mode rejected
  }
  {
    // Open-loop JOINT sequence under a custom idle must survive a flaky IMU:
    // uncalibrated/stale samples never abort it (they only disable tip-safety).
    Engine e; const float idle[4]={90,90,90,90};
    assert(captureNeutral(e.config,idle,10)); e.config.slew=40; assert(e.arm(0,1,idle));
    Sequence s; strcpy(s.name,"m"); s.count=1; s.frames[0]={{95,95,95,95},400,0};
    assert(e.play(s,1,0));
    for(uint32_t t=20;t<=800;t+=20){ e.beat(1,t); e.tick(t,{true,false,t,0,0}); }
    assert(e.armed && std::fabs(e.output[0]-95)<0.01f); // ran to the pose despite bad IMU
  }
  std::cout<<"Motion engine: ownership, limits, sequences, stops, PID, stale sensors, tilt posture and rollover passed.\n";
}
