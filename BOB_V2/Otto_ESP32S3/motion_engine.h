#pragma once
#include <cmath>
#include <cstdint>
#include <cstring>

// Platform-independent controller. All angles are COMMANDS, not joint feedback.
namespace bobmotion {
constexpr unsigned JOINTS = 4, MAX_FRAMES = 16, SLOTS = 12;
constexpr uint32_t LEASE_MS = 3000, STALE_MS = 250, FRAME_MS = 20;
// The MPU on this build emits occasional single-sample spikes. Require this many
// consecutive out-of-range SAMPLES before a tilt stop so one glitch cannot abort
// an (open-loop) movement; a genuine sustained tip still stops within ~150 ms.
constexpr unsigned TILT_STREAK = 3;
inline float bound(float x, float lo, float hi) { return x < lo ? lo : (x > hi ? hi : x); }
inline bool finiteRange(float x, float lo, float hi) { return std::isfinite(x) && x >= lo && x <= hi; }
struct Gains { float kp, ki, kd; };
struct Joint { float low, high, pitch, roll; };
struct Config {
  Gains gains[2] = {{0.35f, 0, 0.03f}, {0.35f, 0, 0.03f}};
  // Direction depends on horn orientation. Confirm these with a supported robot.
  Joint joint[JOINTS] = {{60,120,1,0}, {60,120,0,1}, {60,120,1,0}, {60,120,0,-1}};
  float correction = 8, slew = 40, cutoff = 35;
  float neutral[JOINTS] = {90,90,90,90};
  bool customNeutral = false;
};
struct Frame { float angle[JOINTS]; uint32_t moveMs, holdMs; };
// SEQ_JOINTS: frames are absolute joint-angle keyframes (the original system).
// SEQ_TILT:   frames are target body orientations — angle[0]=pitch°, angle[1]=roll°
//             (angle[2]/[3] unused). Played with PID on, the controller moves the
//             four motors to drive the IMU to each target. This is the "use PID so
//             the 4 motors change the IMU orientation to our required motion" mode:
//             motions are authored in orientation space and are robust to servo
//             calibration, and the joints are only ever base(neutral)+PID correction.
enum : uint8_t { SEQ_JOINTS = 0, SEQ_TILT = 1 };
struct Sequence { char name[25] = ""; unsigned count = 0; int expression = -1; uint8_t mode = SEQ_JOINTS; Frame frames[MAX_FRAMES] = {}; };
struct Sample { bool ready, calibrated; uint32_t stamp; float pitch, roll; };
// Accelerometer-only tilt is used for slow posture trials. Free-fall, strong
// translation or corrupt frames must not be accepted as a gravity direction.
inline bool usableGravity(float ax, float ay, float az) {
  const float squared=ax*ax+ay*ay+az*az;
  return finiteRange(squared,(981*.75f)*(981*.75f),(981*1.25f)*(981*1.25f));
}
inline bool validConfig(const Config& c) {
  for (auto& g : c.gains)
    if (!finiteRange(g.kp,0,5) || !finiteRange(g.ki,0,2) || !finiteRange(g.kd,0,2)) return false;
  for (auto& j : c.joint)
    if (!finiteRange(j.low,0,179) || !finiteRange(j.high,1,180) || j.low >= j.high ||
        !finiteRange(j.pitch,-1,1) || !finiteRange(j.roll,-1,1)) return false;
  if (c.customNeutral) for (unsigned i=0;i<JOINTS;++i)
    if (!finiteRange(c.neutral[i],c.joint[i].low,c.joint[i].high)) return false;
  return finiteRange(c.correction,1,15) && finiteRange(c.slew,5,120) && finiteRange(c.cutoff,10,60);
}
inline bool validSequence(const Sequence& s, const Config& c) {
  if (s.expression < -1 || s.expression > 6) return false;
  if (s.mode != SEQ_JOINTS && s.mode != SEQ_TILT) return false;
  if (!s.count || s.count > MAX_FRAMES || !s.name[0] || !memchr(s.name,0,sizeof(s.name))) return false;
  for (unsigned i=0; i<s.count; ++i) {
    if (s.frames[i].moveMs < 100 || s.frames[i].moveMs > 5000 || s.frames[i].holdMs > 5000) return false;
    if (s.mode == SEQ_TILT) {
      // Orientation targets: pitch/roll must be finite, within ±15° and within
      // the configured tilt cutoff so the target is actually reachable/safe.
      const float lim = c.cutoff < 15 ? c.cutoff : 15;
      if (!finiteRange(s.frames[i].angle[0],-lim,lim) || !finiteRange(s.frames[i].angle[1],-lim,lim)) return false;
    } else {
      for (unsigned ch=0; ch<JOINTS; ++ch)
        if (!finiteRange(s.frames[i].angle[ch], c.joint[ch].low, c.joint[ch].high)) return false;
    }
  }
  return true;
}
// Capture a known assembled rest pose without writing PWM or enabling PID.
inline bool captureNeutral(Config& config, const float current[JOINTS], float radius) {
  if (!finiteRange(radius,1,10)) return false;
  Config candidate=config;
  for (unsigned i=0;i<JOINTS;++i) {
    if (!finiteRange(current[i],radius,180-radius)) return false;
    candidate.neutral[i]=current[i];
    candidate.joint[i].low=current[i]-radius;
    candidate.joint[i].high=current[i]+radius;
  }
  candidate.customNeutral=true;
  candidate.slew=5; candidate.correction=1; candidate.cutoff=10;
  // Direction is not inferred from a rest pose. Gains start at zero.
  candidate.gains[0]=candidate.gains[1]={0,0,0};
  if (!validConfig(candidate)) return false;
  config=candidate; return true;
}
struct Pid {
  float integral = 0, previous = 0, derivative = 0;
  bool initialized = false;
  void reset() { integral=previous=derivative=0; initialized=false; }
  float step(const Gains& g, float target, float measured, float dt, float limit, bool freezeI) {
    const float error = target-measured;
    if (!initialized) { previous=measured; initialized=true; }
    // Filtered derivative on measurement avoids a kick when target changes.
    derivative += (dt/(0.12f+dt)) * ((measured-previous)/dt - derivative);
    previous=measured;
    const float candidate = g.ki == 0 ? 0 : bound(integral + g.ki*error*dt, -limit, limit);
    const float proposed = g.kp*error + candidate - g.kd*derivative;
    // Conditional integration for both controller and downstream joint clipping.
    if (!freezeI && !(proposed > limit && error > 0) && !(proposed < -limit && error < 0))
      integral = candidate;
    return bound(g.kp*error + integral - g.kd*derivative, -limit, limit);
  }
};

struct Engine {
  Config config;
  bool armed=false, pid=false, playing=false, saturated=false;
  uint32_t token=0, heartbeat=0, lastTick=0, lastSample=0, segmentStart=0;
  unsigned frame=0, repeats=0;
  float base[JOINTS]={90,90,90,90}, output[JOINTS]={90,90,90,90}, from[JOINTS]={90,90,90,90};
  float target[2]={0,0}, correction[2]={0,0}, tiltFrom[2]={0,0};
  uint32_t lastTiltSample=0; unsigned tiltStreak=0;
  float filtered[2]={0,0}, peak=0;
  double squareSum=0;
  uint32_t samples=0, clips=0;
  bool haveSample=false, holding=false;
  uint32_t holdStart=0;
  const char* reason="disarmed";
  Sequence sequence;
  Pid controller[2];

  void resetPid() {
    controller[0].reset(); controller[1].reset();
    correction[0]=correction[1]=0;
    haveSample=false; saturated=false;
    tiltStreak=0; lastTiltSample=0;
  }
  void stop(const char* why) {
    armed=pid=playing=false; token=0; reason=why; resetPid();
    // Hold the last command; a stop must not trigger a jump to neutral.
    for (unsigned ch=0; ch<JOINTS; ++ch) base[ch]=output[ch];
  }
  bool arm(uint32_t now, uint32_t owner, const float current[JOINTS]) {
    if (armed || !owner) return false;
    for (unsigned ch=0; ch<JOINTS; ++ch)
      if (!finiteRange(current[ch], config.joint[ch].low, config.joint[ch].high)) return false;
    for (unsigned ch=0; ch<JOINTS; ++ch) base[ch]=output[ch]=from[ch]=current[ch];
    armed=true; pid=playing=false; token=owner; heartbeat=lastTick=now;
    resetPid(); squareSum=0; peak=0; samples=clips=0; reason="armed; holding";
    return true;
  }
  bool alive(uint32_t now) {
    if (armed && uint32_t(now-heartbeat) >= LEASE_MS) stop("controller heartbeat lost");
    return armed;
  }
  bool authorize(uint32_t owner, uint32_t now) { return alive(now) && owner && owner==token; }
  bool beat(uint32_t owner, uint32_t now) {
    if (!authorize(owner,now)) return false;
    heartbeat=now; return true;
  }
  bool configure(const Config& c) {
    if (!validConfig(c)) return false;
    // Do not unexpectedly clamp current position or an in-flight keyframe.
    for (unsigned ch=0; ch<JOINTS; ++ch)
      if (armed && (!finiteRange(output[ch],c.joint[ch].low,c.joint[ch].high) ||
                    !finiteRange(base[ch],c.joint[ch].low,c.joint[ch].high))) return false;
    if (playing && !validSequence(sequence,c)) return false;
    config=c; resetPid(); return true;
  }
  bool enablePid(bool on, const Sample& s, uint32_t now) {
    if (!armed) return false;
    if (on && (!s.ready || !s.calibrated || uint32_t(now-s.stamp)>STALE_MS ||
               !std::isfinite(s.pitch) || !std::isfinite(s.roll) ||
               fabsf(s.pitch)>config.cutoff || fabsf(s.roll)>config.cutoff)) return false;
    pid=on; resetPid(); reason=on ? "PID active" : "PID off";
    return true;
  }
  bool play(const Sequence& s, unsigned count, uint32_t now) {
    if (!armed || !count || count>10 || !validSequence(s,config)) return false;
    sequence=s; repeats=count; frame=0; playing=true; segmentStart=now; holding=false;
    // Joint mode ramps `base` through the keyframes. Tilt mode holds `base` at the
    // current (neutral) pose and ramps the PID setpoint instead, so start the tilt
    // ramp from wherever the setpoint currently is.
    for (unsigned ch=0; ch<JOINTS; ++ch) from[ch]=base[ch];
    tiltFrom[0]=target[0]; tiltFrom[1]=target[1];
    reason="playing"; return true;
  }
  void tick(uint32_t now, const Sample& s) {
    if (!alive(now)) return;
    const uint32_t elapsed=now-lastTick;
    if (elapsed>STALE_MS) { stop("control loop stalled"); return; }
    if (elapsed<FRAME_MS) return;
    const float dt=elapsed/1000.0f;
    lastTick=now;
    // PID and posture (tilt) sequences genuinely need the IMU. An open-loop
    // JOINT sequence does not — it must keep running through IMU dropouts, with
    // the IMU used only as an optional tip-safety while it has good data.
    const bool needImu = pid || (playing && sequence.mode==SEQ_TILT);
    const bool tipWatch = playing && config.customNeutral;
    if (needImu || tipWatch) {
      const bool imuBad = !s.ready || !s.calibrated || uint32_t(now-s.stamp)>STALE_MS ||
                          !std::isfinite(s.pitch) || !std::isfinite(s.roll);
      if (needImu && imuBad) { stop("IMU unavailable or stale"); return; }
      if (!imuBad) {
        // Evaluate the tilt limit once per fresh sample and require a short streak,
        // so a lone sensor spike does not abort the movement.
        if (s.stamp!=lastTiltSample) {
          lastTiltSample=s.stamp;
          const bool over = fabsf(s.pitch)>config.cutoff || fabsf(s.roll)>config.cutoff;
          tiltStreak = over ? tiltStreak+1 : 0;
        }
        if (tiltStreak>=TILT_STREAK) { stop("tilt limit exceeded"); return; }
      } else {
        tiltStreak=0;   // no usable data — never accumulate toward a false stop
      }
      if (pid && (!haveSample || s.stamp!=lastSample)) {
        const float sensorDt=haveSample ? bound(uint32_t(s.stamp-lastSample)/1000.0f,0.001f,0.25f) : 0.05f;
        const float m[2]={s.pitch,s.roll};
        for (unsigned axis=0; axis<2; ++axis) {
          filtered[axis]=haveSample ? filtered[axis]+sensorDt/(0.08f+sensorDt)*(m[axis]-filtered[axis]) : m[axis];
          correction[axis]=controller[axis].step(config.gains[axis],target[axis],filtered[axis],
                                               sensorDt,config.correction,saturated);
        }
        lastSample=s.stamp; haveSample=true;
        squareSum+=s.pitch*s.pitch+s.roll*s.roll; ++samples;
        peak=fmaxf(peak,fmaxf(fabsf(s.pitch),fabsf(s.roll)));
      }
    }
    if (playing) {
      const Frame& f=sequence.frames[frame];
      const uint32_t phase=now-segmentStart;
      const float t=bound(float(phase)/f.moveMs,0,1);
      const float ease=t*t*(3-2*t);
      if (sequence.mode==SEQ_TILT) {
        // Ramp the PID setpoint toward the frame's target orientation. `base`
        // stays at neutral; the joints follow only through PID correction below.
        for (unsigned a=0; a<2; ++a) target[a]=tiltFrom[a]+(f.angle[a]-tiltFrom[a])*ease;
      } else {
        for (unsigned ch=0; ch<JOINTS; ++ch) base[ch]=from[ch]+(f.angle[ch]-from[ch])*ease;
      }
      if (phase>=f.moveMs) {
        bool reached=true;
        if (sequence.mode==SEQ_TILT) {
          // Orientation convergence depends on the physical loop (gains, servo
          // authority), so advance a posture frame on time once the ramp is done.
          reached=true;
        } else {
          for (unsigned ch=0; ch<JOINTS; ++ch) {
            const float desired=bound(base[ch]+config.joint[ch].pitch*correction[0]+config.joint[ch].roll*correction[1],
                                      config.joint[ch].low,config.joint[ch].high);
            if (fabsf(output[ch]-desired)>1) reached=false;
          }
        }
        // Slew limits can extend a frame; never skip unfinished poses to catch up.
        if (reached && !holding) { holding=true; holdStart=now; }
        if (holding && uint32_t(now-holdStart)>=f.holdMs) {
          if (++frame>=sequence.count) {
            frame=0;
            if (--repeats==0) { playing=false; reason="finished; holding"; }
          }
          segmentStart=now; holding=false;
          for (unsigned ch=0; ch<JOINTS; ++ch) from[ch]=base[ch];
          tiltFrom[0]=target[0]; tiltFrom[1]=target[1];
        }
      }
    }
    saturated=false;
    for (unsigned ch=0; ch<JOINTS; ++ch) {
      const Joint& j=config.joint[ch];
      const float desired=base[ch]+j.pitch*correction[0]+j.roll*correction[1];
      const float limited=bound(desired,j.low,j.high);
      const float delta=bound(limited-output[ch],-config.slew*dt,config.slew*dt);
      if (fabsf(desired-limited)>0.001f || fabsf(limited-output[ch]-delta)>0.001f) saturated=true;
      output[ch]+=delta;
    }
    if (saturated) ++clips;
  }
  float rms() const { return samples ? float(std::sqrt(squareSum/(2*samples))) : 0; }
};
} // namespace bobmotion
