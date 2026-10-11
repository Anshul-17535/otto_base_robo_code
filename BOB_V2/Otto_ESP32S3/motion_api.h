#pragma once
#include <cJSON.h>

// Included after the IMU implementation. HTTP access is local-network control,
// not Internet authentication. The token prevents two tuning clients fighting.
static void motionError(int code, const char* message) {
  server.send(code,"application/json",String("{\"error\":\"")+message+"\"}");
}
static void motionSend(cJSON* j) {
  char* body=cJSON_PrintUnformatted(j);
  if (body) { server.send(200,"application/json",body); cJSON_free(body); }
  else motionError(500,"out of memory");
  cJSON_Delete(j);
}
struct MotionBody {
  cJSON* j=nullptr;
  MotionBody() {
    const String body=server.arg("plain");
    if (body.length()>8192) { motionError(413,"JSON body exceeds 8192 bytes"); return; }
    j=cJSON_Parse(body.c_str());
    if (!cJSON_IsObject(j)) { cJSON_Delete(j); j=nullptr; motionError(400,"expected a JSON object"); }
  }
  ~MotionBody() { cJSON_Delete(j); }
};
static bool motionNumber(cJSON* j, const char* key, float& out, float lo, float hi) {
  cJSON* v=cJSON_GetObjectItemCaseSensitive(j,key);
  if (!cJSON_IsNumber(v) || !bobmotion::finiteRange(v->valuedouble,lo,hi)) return false;
  out=v->valuedouble; return true;
}
static bool motionArray(cJSON* a, float* out, unsigned count, float lo, float hi) {
  if (!cJSON_IsArray(a) || cJSON_GetArraySize(a)!=int(count)) return false;
  for (unsigned i=0; i<count; ++i) {
    cJSON* v=cJSON_GetArrayItem(a,i);
    if (!cJSON_IsNumber(v) || !bobmotion::finiteRange(v->valuedouble,lo,hi)) return false;
    out[i]=v->valuedouble;
  }
  return true;
}
static cJSON* motionFloats(const float* values, unsigned count) {
  cJSON* a=cJSON_CreateArray();
  for (unsigned i=0; i<count; ++i) cJSON_AddItemToArray(a,cJSON_CreateNumber(values[i]));
  return a;
}
static bool motionAuth(cJSON* j) {
  cJSON* t=cJSON_GetObjectItemCaseSensitive(j,"token");
  char expected[9]; snprintf(expected,sizeof(expected),"%08lx",(unsigned long)motion.token);
  const bool wasArmed=motion.armed;
  const bool valid=motion.authorize(motion.token,millis());
  if (wasArmed && !motion.armed) imuCalValid=false;
  if (!cJSON_IsString(t) || strcmp(t->valuestring,expected) || !valid) {
    motionError(409,"arm a session first; token expired or owned by another client"); return false;
  }
  return true;
}
static bobmotion::Sample motionSample() {
  float pitch=0,roll=0;
  accelPitchRoll(imu_ax,imu_ay,imu_az,&pitch,&roll);
  return {imuReady && bobmotion::usableGravity(imu_ax,imu_ay,imu_az),imuCalValid,(uint32_t)imuLastOkMs,
          angleDelta(pitch,imuCalPitch),angleDelta(roll,imuCalRoll)};
}
static cJSON* motionConfigJson(const bobmotion::Config& c) {
  cJSON* j=cJSON_CreateObject(), *g=cJSON_CreateArray(), *joints=cJSON_CreateArray();
  for (auto& axis:c.gains) {
    const float values[]={axis.kp,axis.ki,axis.kd};
    cJSON_AddItemToArray(g,motionFloats(values,3));
  }
  for (auto& joint:c.joint) {
    const float values[]={joint.low,joint.high,joint.pitch,joint.roll};
    cJSON_AddItemToArray(joints,motionFloats(values,4));
  }
  cJSON_AddItemToObject(j,"gains",g); cJSON_AddItemToObject(j,"joints",joints);
  cJSON_AddItemToObject(j,"neutral",motionFloats(c.neutral,4));
  cJSON_AddBoolToObject(j,"customNeutral",c.customNeutral);
  cJSON_AddItemToObject(j,"servoPins",cJSON_CreateIntArray(servoPins,4));
  cJSON_AddItemToObject(j,"servoTrims",cJSON_CreateIntArray(trim,4));
  cJSON_AddNumberToObject(j,"correction",c.correction);
  cJSON_AddNumberToObject(j,"slew",c.slew); cJSON_AddNumberToObject(j,"cutoff",c.cutoff);
  return j;
}
static bool motionParseConfig(cJSON* j, bobmotion::Config& c) {
  // Old configurations remain valid; clients omitting new fields preserve them.
  if (cJSON_HasObjectItem(j,"neutral") &&
      !motionArray(cJSON_GetObjectItemCaseSensitive(j,"neutral"),c.neutral,4,0,180)) return false;
  if (cJSON_HasObjectItem(j,"customNeutral")) {
    cJSON* custom=cJSON_GetObjectItemCaseSensitive(j,"customNeutral");
    if (!cJSON_IsBool(custom)) return false;
    c.customNeutral=cJSON_IsTrue(custom);
  }
  cJSON* gains=cJSON_GetObjectItemCaseSensitive(j,"gains");
  cJSON* joints=cJSON_GetObjectItemCaseSensitive(j,"joints");
  if (!cJSON_IsArray(gains) || cJSON_GetArraySize(gains)!=2 ||
      !cJSON_IsArray(joints) || cJSON_GetArraySize(joints)!=4) return false;
  for (unsigned axis=0; axis<2; ++axis) {
    float v[3]; if (!motionArray(cJSON_GetArrayItem(gains,axis),v,3,0,5)) return false;
    c.gains[axis]={v[0],v[1],v[2]};
  }
  for (unsigned ch=0; ch<4; ++ch) {
    float v[4]; if (!motionArray(cJSON_GetArrayItem(joints,ch),v,4,-1,180)) return false;
    c.joint[ch]={v[0],v[1],v[2],v[3]};
    if (v[0]+trim[ch]<0 || v[1]+trim[ch]>180) return false;
  }
  return motionNumber(j,"correction",c.correction,1,15) &&
         motionNumber(j,"slew",c.slew,5,120) && motionNumber(j,"cutoff",c.cutoff,10,60) &&
         bobmotion::validConfig(c);
}
static cJSON* motionSequenceJson(const bobmotion::Sequence& s) {
  cJSON* j=cJSON_CreateObject(), *frames=cJSON_CreateArray();
  cJSON_AddStringToObject(j,"name",s.name);
  cJSON_AddNumberToObject(j,"expression",s.expression);
  cJSON_AddStringToObject(j,"mode",s.mode==bobmotion::SEQ_TILT?"tilt":"joints");
  for (unsigned i=0; i<s.count; ++i) {
    cJSON* f=cJSON_CreateObject();
    if (s.mode==bobmotion::SEQ_TILT) cJSON_AddItemToObject(f,"tilt",motionFloats(s.frames[i].angle,2));
    else                             cJSON_AddItemToObject(f,"angles",motionFloats(s.frames[i].angle,4));
    cJSON_AddNumberToObject(f,"moveMs",s.frames[i].moveMs);
    cJSON_AddNumberToObject(f,"holdMs",s.frames[i].holdMs);
    cJSON_AddItemToArray(frames,f);
  }
  cJSON_AddItemToObject(j,"frames",frames); return j;
}
static bool motionParseSequence(cJSON* j, bobmotion::Sequence& s) {
  cJSON* name=cJSON_GetObjectItemCaseSensitive(j,"name");
  cJSON* frames=cJSON_GetObjectItemCaseSensitive(j,"frames");
  if (!cJSON_IsString(name) || !name->valuestring[0] || strlen(name->valuestring)>24 ||
      !cJSON_IsArray(frames)) return false;
  s={}; strncpy(s.name,name->valuestring,sizeof(s.name)-1);
  if (cJSON_HasObjectItem(j,"expression")) {
    float face;
    if (!motionNumber(j,"expression",face,-1,6) || floorf(face)!=face) return false;
    s.expression=(int)face;
  }
  // Optional "mode": "joints" (default, absolute joint keyframes) or "tilt"
  // (target body orientation per frame, driven by PID). Old files omit it.
  s.mode=bobmotion::SEQ_JOINTS;
  cJSON* mode=cJSON_GetObjectItemCaseSensitive(j,"mode");
  if (cJSON_IsString(mode)) {
    if (!strcmp(mode->valuestring,"tilt")) s.mode=bobmotion::SEQ_TILT;
    else if (strcmp(mode->valuestring,"joints")) return false;
  }
  s.count=cJSON_GetArraySize(frames);
  if (!s.count || s.count>bobmotion::MAX_FRAMES) return false;
  for (unsigned i=0; i<s.count; ++i) {
    cJSON* f=cJSON_GetArrayItem(frames,i);
    float ms,hold;
    if (!motionNumber(f,"moveMs",ms,100,5000) || !motionNumber(f,"holdMs",hold,0,5000) ||
        floorf(ms)!=ms || floorf(hold)!=hold) return false;
    if (s.mode==bobmotion::SEQ_TILT) {
      // "tilt":[pitch,roll] in degrees; angle[2]/[3] are unused for posture frames.
      if (!motionArray(cJSON_GetObjectItemCaseSensitive(f,"tilt"),s.frames[i].angle,2,-15,15)) return false;
      s.frames[i].angle[2]=s.frames[i].angle[3]=0;
    } else {
      if (!motionArray(cJSON_GetObjectItemCaseSensitive(f,"angles"),s.frames[i].angle,4,0,180)) return false;
    }
    s.frames[i].moveMs=(uint32_t)ms; s.frames[i].holdMs=(uint32_t)hold;
  }
  return bobmotion::validSequence(s,motion.config);
}
// Versioned JSON blobs stay readable across compiler/struct-layout changes.
static bool motionStore(const char* key, cJSON* value) {
  cJSON_AddNumberToObject(value,"version",1);
  char* data=cJSON_PrintUnformatted(value);
  bool ok=false;
  if (data) {
    Preferences store;
    if (store.begin("bobmotion",false)) {
      const size_t bytes=strlen(data)+1;
      ok=store.putBytes(key,data,bytes)==bytes; store.end();
    }
    cJSON_free(data);
  }
  cJSON_Delete(value); return ok;
}
static cJSON* motionLoad(const char* key) {
  Preferences store;
  if (!store.begin("bobmotion",true)) return nullptr;
  size_t count=store.getBytesLength(key);
  if (!count || count>8192) { store.end(); return nullptr; }
  char* data=(char*)malloc(count+1);
  if (!data) { store.end(); return nullptr; }
  const bool read=store.getBytes(key,data,count)==count;
  data[count]=0; store.end();
  cJSON* j=read ? cJSON_Parse(data) : nullptr; free(data);
  cJSON* version=cJSON_GetObjectItemCaseSensitive(j,"version");
  if (!cJSON_IsNumber(version) || version->valuedouble!=1) { cJSON_Delete(j); return nullptr; }
  return j;
}
static bool motionSlot(cJSON* j, unsigned& slot) {
  float n;
  if (!motionNumber(j,"slot",n,0,bobmotion::SLOTS-1) || floorf(n)!=n) return false;
  slot=(unsigned)n; return true;
}
static void motionSlotKey(unsigned slot, char* key, size_t size) { snprintf(key,size,"seq%u",slot); }
void motionLoadSettings() {
  cJSON* j=motionLoad("config");
  bobmotion::Config c;
  if (j && motionParseConfig(j,c)) {
    bool matches=true;
    if (c.customNeutral) {
      float pins[4],trims[4];
      matches=motionArray(cJSON_GetObjectItemCaseSensitive(j,"servoPins"),pins,4,0,48) &&
              motionArray(cJSON_GetObjectItemCaseSensitive(j,"servoTrims"),trims,4,-45,45);
      if (matches) for (unsigned i=0;i<4;++i)
        if (pins[i]!=servoPins[i] || trims[i]!=trim[i]) matches=false;
    }
    if (matches) motion.config=c;
    else dlogPrintf("motion: saved idle profile ignored; servo map or trims changed");
  }
  cJSON_Delete(j);
  motion.stop("ready; arm to control");
  dlogPrintf("motion: editor ready; PID off; 6 NVS movement slots");
}
void motionDisarm(const char* why) {
  motion.stop(why); liftHitCount=0; liftWindowStart=0; imuLifted=false;
}
void motionTick() {
  const bool wasArmed=motion.armed;
  motion.tick(millis(),motionSample());
  if (wasArmed && !motion.armed) {
    liftHitCount=0; liftWindowStart=0; imuLifted=false;
    // A stop/fault must not hand control straight to an automatic foot reaction.
    imuCalValid=false;
    dlogPrintf("motion: stopped (%s); recalibrate before re-enabling PID/lift",motion.reason);
  }
  if (motion.armed)
    for (unsigned ch=0; ch<4; ++ch) setServoSmooth(ch,(int)lroundf(motion.output[ch]));
}
void handleMotionState() {
  const auto s=motionSample();
  cJSON* j=cJSON_CreateObject();
  cJSON_AddNumberToObject(j,"apiVersion",2);
  cJSON_AddBoolToObject(j,"armed",motion.armed); cJSON_AddBoolToObject(j,"pid",motion.pid);
  cJSON_AddBoolToObject(j,"playing",motion.playing); cJSON_AddStringToObject(j,"reason",motion.reason);
  cJSON_AddStringToObject(j,"seqMode",motion.sequence.mode==bobmotion::SEQ_TILT?"tilt":"joints");
  cJSON_AddBoolToObject(j,"imuReady",imuReady);
  cJSON_AddBoolToObject(j,"imuSampleValid",s.ready);
  cJSON_AddNumberToObject(j,"accelMagnitude",accelMag(imu_ax,imu_ay,imu_az)); cJSON_AddBoolToObject(j,"calibrated",s.calibrated);
  cJSON_AddNumberToObject(j,"sampleAgeMs",uint32_t(millis()-s.stamp));
  cJSON_AddNumberToObject(j,"pitch",s.pitch); cJSON_AddNumberToObject(j,"roll",s.roll);
  cJSON_AddItemToObject(j,"base",motionFloats(motion.base,4));
  float commanded[4]; for (unsigned ch=0; ch<4; ++ch) commanded[ch]=servoCur[ch];
  cJSON_AddItemToObject(j,"commanded",motionFloats(commanded,4));
  cJSON_AddItemToObject(j,"correction",motionFloats(motion.correction,2));
  cJSON_AddItemToObject(j,"target",motionFloats(motion.target,2));
  cJSON_AddNumberToObject(j,"frame",motion.frame);
  cJSON_AddNumberToObject(j,"remaining",motion.repeats);
  cJSON_AddBoolToObject(j,"limited",motion.saturated);
  cJSON_AddNumberToObject(j,"limitedFrames",motion.clips);
  cJSON_AddNumberToObject(j,"rmsTilt",motion.rms()); cJSON_AddNumberToObject(j,"peakTilt",motion.peak);
  cJSON_AddNumberToObject(j,"leaseMs",bobmotion::LEASE_MS);
  cJSON_AddItemToObject(j,"config",motionConfigJson(motion.config));
  motionSend(j);
}
void handleMotionArm() {
  const bool wasArmed=motion.armed;
  motion.alive(millis());
  if (wasArmed && !motion.armed) imuCalValid=false;
  if (motion.armed || reactState!=RS_IDLE || scaredMode) { motionError(409,"another controller or lift reaction is active"); return; }
  float current[4];
  for (unsigned ch=0; ch<4; ++ch) {
    if (servos[ch].pin<0) { motionError(503,"a servo PWM channel is unavailable"); return; }
    if (motion.config.joint[ch].low+trim[ch]<0 || motion.config.joint[ch].high+trim[ch]>180) {
      motionError(409,"trim conflicts with motion limits; correct trims first"); return;
    }
    current[ch]=servoCur[ch];
  }
  uint32_t owner=esp_random(); if (!owner) owner=1;
  if (!motion.arm(millis(),owner,current)) { motionError(409,"current pose is outside limits; capture the assembled idle pose while disarmed"); return; }
  liftHitCount=0; liftWindowStart=0; imuLifted=false;
  cJSON* j=cJSON_CreateObject();
  char token[9]; snprintf(token,sizeof(token),"%08lx",(unsigned long)owner);
  cJSON_AddStringToObject(j,"token",token); cJSON_AddNumberToObject(j,"leaseMs",bobmotion::LEASE_MS);
  motionSend(j);
}
void handleMotionHeartbeat() {
  MotionBody body; if (!body.j || !motionAuth(body.j)) return;
  motion.beat(motion.token,millis()); server.send(200,"application/json","{\"ok\":true}");
}
void handleMotionStop() {
  // Auto-release from an old tab cannot stop a newer owner's session. An
  // explicit emergency stop can omit token and is always accepted.
  if (server.hasArg("plain") && server.arg("plain").length()) {
    MotionBody body; if (!body.j) return;
    if (cJSON_HasObjectItem(body.j,"token") && !motionAuth(body.j)) return;
  }
  motionDisarm("stopped by operator"); imuCalValid=false;
  server.send(200,"application/json","{\"ok\":true}");
}
void handleMotionSetup() {
  MotionBody body; if (!body.j) return;
  if (motion.armed || reactState!=RS_IDLE || scaredMode) {
    motionError(409,"stop motion before capturing idle pose"); return;
  }
  float expected[4],radius;
  if (!motionArray(cJSON_GetObjectItemCaseSensitive(body.j,"expected"),expected,4,0,180) ||
      !motionNumber(body.j,"radius",radius,1,10)) {
    motionError(400,"expected current angles and radius 1..10 required"); return;
  }
  float current[4];
  for (unsigned i=0;i<4;++i) {
    current[i]=servoCur[i];
    if (current[i]!=expected[i] || servos[i].pin<0) {
      motionError(409,"pose changed or PWM unavailable; refresh before capturing"); return;
    }
    if (current[i]-radius+trim[i]<0 || current[i]+radius+trim[i]>180) {
      motionError(400,"capture range conflicts with trims"); return;
    }
  }
  auto candidate=motion.config;
  if (!bobmotion::captureNeutral(candidate,current,radius)) {
    motionError(400,"idle pose too close to servo boundary"); return;
  }
  // Persist before changing RAM. This endpoint never writes a servo command.
  if (!motionStore("config",motionConfigJson(candidate))) {
    motionError(507,"idle settings could not be saved"); return;
  }
  motion.configure(candidate);
  for (unsigned i=0;i<4;++i) motion.base[i]=motion.output[i]=motion.from[i]=current[i];
  motion.reason="idle captured; PID gains zero; ready to arm";
  server.send(200,"application/json","{\"ok\":true}");
}
void handleMotionConfig() {
  MotionBody body; if (!body.j || !motionAuth(body.j)) return;
  bobmotion::Config candidate=motion.config;
  if (!motionParseConfig(cJSON_GetObjectItemCaseSensitive(body.j,"config"),candidate)) {
    motionError(400,"invalid PID gains, mapping, limits or trim range"); return;
  }
  const auto previous=motion.config;
  if (!motion.configure(candidate)) { motionError(409,"limits would clip current pose or sequence"); return; }
  if (cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(body.j,"persist")) &&
      !motionStore("config",motionConfigJson(candidate))) {
    motion.configure(previous); motionError(507,"settings could not be stored in NVS"); return;
  }
  server.send(200,"application/json","{\"ok\":true}");
}
void handleMotionPid() {
  MotionBody body; if (!body.j || !motionAuth(body.j)) return;
  cJSON* enabled=cJSON_GetObjectItemCaseSensitive(body.j,"enabled");
  float target[2];
  if (!cJSON_IsBool(enabled) || !motionArray(cJSON_GetObjectItemCaseSensitive(body.j,"target"),target,2,-15,15)) {
    motionError(400,"expected enabled boolean and target [pitch,roll] in -15..15 degrees"); return;
  }
  if (!motion.enablePid(cJSON_IsTrue(enabled),motionSample(),millis())) {
    motionError(409,"PID needs a fresh calibrated IMU within the tilt limit"); return;
  }
  motion.target[0]=target[0]; motion.target[1]=target[1];
  server.send(200,"application/json","{\"ok\":true}");
}
void handleMotionPlay() {
  MotionBody body; if (!body.j || !motionAuth(body.j)) return;
  bobmotion::Sequence seq;
  float repeats;
  if (!motionParseSequence(cJSON_GetObjectItemCaseSensitive(body.j,"sequence"),seq) ||
      !motionNumber(body.j,"repeats",repeats,1,10) || floorf(repeats)!=repeats) {
    motionError(400,"invalid sequence, pose outside limits, or repeats not in 1..10"); return;
  }
  // Only posture (tilt) sequences need the IMU to start. Open-loop joint
  // sequences play without it, so an intermittent sensor cannot block them.
  if (seq.mode==bobmotion::SEQ_TILT) {
    const auto sample=motionSample();
    if (!sample.ready || !sample.calibrated || uint32_t(millis()-sample.stamp)>bobmotion::STALE_MS ||
        !std::isfinite(sample.pitch) || !std::isfinite(sample.roll) ||
        fabsf(sample.pitch)>motion.config.cutoff || fabsf(sample.roll)>motion.config.cutoff) {
      motionError(409,"posture (tilt) sequences need a fresh calibrated IMU within the tilt limit"); return;
    }
  }
  // A posture (tilt) sequence only moves the joints through PID correction, so
  // PID must already be enabled — otherwise it would silently hold neutral.
  if (seq.mode==bobmotion::SEQ_TILT && !motion.pid) {
    motionError(409,"enable PID before playing a posture (tilt) sequence"); return;
  }
  if (!motion.play(seq,(unsigned)repeats,millis())) { motionError(409,"unable to play"); return; }
  if (seq.expression>=0) {
    expression=(uint8_t)seq.expression;
    rpsHold=false; danceMode=false; prevExpr=255;
  }
  server.send(200,"application/json","{\"ok\":true}");
}
void handleMotionLibrary() {
  cJSON* j=cJSON_CreateObject(), *slots=cJSON_CreateArray();
  for (unsigned slot=0; slot<bobmotion::SLOTS; ++slot) {
    char key[12]; motionSlotKey(slot,key,sizeof(key));
    cJSON* stored=motionLoad(key);
    cJSON* entry=cJSON_CreateObject();
    cJSON_AddNumberToObject(entry,"slot",slot);
    // Return the saved document even if current joint limits exclude it.
    if (stored) cJSON_AddItemToObject(entry,"sequence",stored);
    else cJSON_AddNullToObject(entry,"sequence");
    cJSON_AddItemToArray(slots,entry);
  }
  cJSON_AddItemToObject(j,"slots",slots); motionSend(j);
}
void handleMotionSave() {
  MotionBody body; if (!body.j || !motionAuth(body.j)) return;
  unsigned slot; bobmotion::Sequence seq;
  if (!motionSlot(body.j,slot) || !motionParseSequence(cJSON_GetObjectItemCaseSensitive(body.j,"sequence"),seq)) {
    motionError(400,"invalid slot or sequence"); return;
  }
  char key[12]; motionSlotKey(slot,key,sizeof(key));
  if (!motionStore(key,motionSequenceJson(seq))) { motionError(507,"movement could not be stored in NVS"); return; }
  server.send(200,"application/json","{\"ok\":true}");
}
void handleMotionDelete() {
  MotionBody body; if (!body.j || !motionAuth(body.j)) return;
  unsigned slot; if (!motionSlot(body.j,slot)) { motionError(400,"invalid slot"); return; }
  char key[12]; motionSlotKey(slot,key,sizeof(key));
  Preferences store;
  bool ok=store.begin("bobmotion",false);
  if (ok) { ok=!store.isKey(key) || store.remove(key); store.end(); }
  if (!ok) { motionError(507,"could not delete movement"); return; }
  server.send(200,"application/json","{\"ok\":true}");
}
