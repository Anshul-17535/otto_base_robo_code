#pragma once

static void handleServoMapGet() {
  cJSON* j=cJSON_CreateObject();
  // Build the allowed array from the canonical list in servo_mapping.h
  int allowed[bobservo::ALLOWED_COUNT];
  for (int i=0; i<bobservo::ALLOWED_COUNT; i++) allowed[i]=bobservo::ALLOWED_PINS[i];
  cJSON_AddItemToObject(j,"pins",cJSON_CreateIntArray(servoPins,4));
  cJSON_AddItemToObject(j,"allowed",cJSON_CreateIntArray(allowed,bobservo::ALLOWED_COUNT));
  cJSON_AddItemToObject(j,"trim",cJSON_CreateIntArray(trim,4));
  cJSON_AddItemToObject(j,"pos",cJSON_CreateIntArray(servoCur,4));
  cJSON_AddBoolToObject(j,"saved",servoMapSaved);
  motionSend(j);
}
static void handleServoMapSave() {
  if (motion.armed || reactState!=RS_IDLE || scaredMode) {
    motionError(409,"Stop motion and recover from lift reaction before changing servo assignments"); return;
  }
  MotionBody body; if (!body.j) return;
  cJSON* array=cJSON_GetObjectItemCaseSensitive(body.j,"pins");
  if (!cJSON_IsArray(array) || cJSON_GetArraySize(array)!=4) {
    motionError(400,"pins must contain four GPIO numbers in LH, LF, RH, RF order"); return;
  }
  int next[4], indices[4];
  for (int i=0;i<4;i++) {
    cJSON* v=cJSON_GetArrayItem(array,i);
    if (!cJSON_IsNumber(v) || v->valuedouble!=v->valueint || !bobservo::isAllowed(v->valueint)) {
      // Build a readable list of allowed GPIOs for the error message
      char allowed_str[32];
      int pos=0;
      for (int k=0; k<bobservo::ALLOWED_COUNT; k++) {
        pos += snprintf(allowed_str+pos, sizeof(allowed_str)-pos,
                        k==0?"%d":", %d", bobservo::ALLOWED_PINS[k]);
      }
      char msg[80];
      snprintf(msg,sizeof(msg),"Use only whole GPIO numbers %s, each once",allowed_str);
      motionError(400,msg); return;
    }
    next[i]=v->valueint;
    if (servos[i].pin!=servoPins[i]) { motionError(503,"Servo PWM is not attached; check boot logs"); return; }
  }
  if (!bobservo::permutation(servoPins,next,indices)) {
    motionError(400,"Use each servo GPIO exactly once; other pins belong to peripherals"); return;
  }
  bool changed=false;
  for (unsigned i=0;i<4;++i) if (next[i]!=servoPins[i]) changed=true;
  ServoPwm previous[4]; int previousPos[4], nextTrim[4];
  for (int i=0;i<4;i++) { previous[i]=servos[i]; previousPos[i]=servoCur[i]; nextTrim[i]=trim[indices[i]]; }
  if (!persistServoLayout(next,nextTrim)) { motionError(500,"Flash save failed; assignments unchanged"); return; }
  // Arduino LEDC writes by pin. Preserve the existing attachment and duty:
  // only the logical roles change, with each physical motor's trim/position.
  for (int i=0;i<4;i++) {
    servos[i]=previous[indices[i]]; servoCur[i]=previousPos[indices[i]];
    servoPins[i]=next[i]; trim[i]=nextTrim[i];
  }
  servoMapSaved=true;
  // A pose profile belongs to the previous logical joint assignment.
  if (changed) motion.config=bobmotion::Config{};
  motionDisarm("servo assignments saved");
  imuCalValid=false;
  hwSet(HW_SERVOS,HW_NOCHECK,"PWM on GPIO %d/%d/%d/%d (no feedback)",next[0],next[1],next[2],next[3]);
  dlogPrintf("servo: saved LH=%d LF=%d RH=%d RF=%d; PWM unchanged",next[0],next[1],next[2],next[3]);
  handleServoMapGet();
}
