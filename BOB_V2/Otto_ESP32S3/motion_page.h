// ⚠ EDITABLE SOURCE only — firmware serves the gzip copy in web_gz.h.
//   After editing, run:  python3 tools/gen_web_gz.py
#pragma once
#include <pgmspace.h>
const char MOTION_PAGE[] PROGMEM = R"motionhtml(
<!doctype html><html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1"><title>BOB / Motion Studio</title>
<style>
:root{color-scheme:dark;--bg:#0b111a;--panel:#141e2b;--line:#2b3b4e;--muted:#a2b4c8;--mint:#9aefca;--orange:#ffb878}
*{box-sizing:border-box}body{margin:0;font:14px/1.5 system-ui,sans-serif;color:#edf3fb;background:radial-gradient(ellipse at top right,#153738,transparent 45%),var(--bg)}
header,main{max-width:1440px;margin:auto;padding:24px 28px}header{display:flex;gap:20px;align-items:center;justify-content:space-between;border-bottom:1px solid var(--line)}
a{color:var(--mint)}h1{font-size:clamp(26px,4vw,42px);margin:5px 0;letter-spacing:-.04em}h2{font-size:17px;margin:0 0 14px}h3{font-size:13px;color:var(--mint);margin:20px 0 8px}p{margin:6px 0 15px;color:var(--muted)}
.eyebrow{font-size:10px;letter-spacing:.2em;color:var(--mint);text-transform:uppercase}.tag{font:12px monospace;padding:8px 12px;border:1px solid var(--line);border-radius:20px}
.toolbar{position:sticky;top:0;z-index:3;background:#101924f5;border:1px solid var(--line);border-radius:14px;padding:12px;display:flex;gap:10px;flex-wrap:wrap;align-items:center;margin-bottom:20px}
.toolbar span{flex:1;min-width:170px}.grid{display:grid;grid-template-columns:1.05fr 1.4fr 1fr;gap:18px;align-items:start}.card{background:var(--panel);border:1px solid var(--line);border-radius:18px;padding:20px;min-width:0;margin-bottom:18px}
button,input,select{font:inherit}button,.link{border:1px solid #3b5265;border-radius:9px;background:#263748;color:#edf3fb;padding:10px 13px;cursor:pointer;min-height:42px;text-decoration:none;display:inline-block}
button:hover{filter:brightness(1.2)}button:disabled{opacity:.45;cursor:not-allowed}.primary{background:var(--mint);color:#0d3827;border-color:var(--mint);font-weight:700}.stop{background:#82383d;border-color:#e8787e;color:#fff;font-weight:750}
input,select{border:1px solid #405269;background:#0b1420;color:#edf3fb;border-radius:7px;min-width:0;padding:7px;width:100%}input[type=range]{padding:0;accent-color:var(--mint)}input[type=checkbox]{width:auto;accent-color:var(--mint)}
button:focus-visible,input:focus-visible,select:focus-visible,a:focus-visible{outline:2px solid var(--mint);outline-offset:3px}label{display:block;color:var(--muted);font-size:12px}.row{display:flex;align-items:center;gap:8px;margin:10px 0}.row>*{flex:1}.row input[type=checkbox]{flex:0}
.joint{margin:18px 0}.joint .row{justify-content:space-between}.joint input[type=number]{max-width:75px}.muted{color:var(--muted);font-size:12px}.small{font-size:11px}.metrics{display:grid;grid-template-columns:1fr 1fr;gap:8px}.metric{padding:10px;background:#0c1622;border-radius:9px;font:22px monospace}.metric small{display:block;font:10px system-ui;color:var(--muted);text-transform:uppercase;letter-spacing:.1em}
canvas{display:block;width:100%;height:140px;margin-top:12px;background:#0b1521;border-radius:9px}.legend{display:flex;gap:15px;font-size:11px;margin:8px 0}.notice{color:var(--orange)}
.scroll{overflow-x:auto}table{width:100%;border-collapse:collapse;font-size:12px}th{text-align:left;font-size:10px;color:var(--muted);padding:7px 3px}td{padding:5px 3px;border-top:1px solid var(--line)}td input{min-width:50px;padding:6px 4px}td button{min-height:30px;padding:3px 8px}.frames{max-height:480px;overflow:auto;margin:12px 0}.frame{border:1px solid var(--line);border-radius:10px;padding:12px;margin-bottom:10px;background:#101a27}.frame.active{border-color:var(--mint)}.framehead{display:flex;align-items:center;gap:6px;margin-bottom:8px}.framehead strong{flex:1}.framehead button{min-height:30px;padding:3px 8px}.frameangles{display:grid;grid-template-columns:repeat(4,1fr);gap:6px}.empty{padding:30px 15px;text-align:center;border:1px dashed #405469;border-radius:10px;color:var(--muted)}
#message{min-height:24px;color:var(--mint);overflow-wrap:anywhere}.footnote{max-width:80ch;margin-top:20px}.check{display:flex;align-items:center;gap:8px;margin:12px 0}
@media(max-width:1100px){.grid{grid-template-columns:1fr 1.2fr}.tuning{grid-column:1/-1}}
@media(max-width:650px){header,main{padding:18px 14px}.grid{grid-template-columns:1fr}.tuning{grid-column:auto}.card{padding:16px}.toolbar{top:0}.tag{display:none}h1{font-size:30px}.frameangles{gap:4px}}
</style></head><body>
<header><div><div class="eyebrow">BOB / Motion Studio</div><h1>Teach Bob a new move.</h1><p>Shape four joints. Tune the posture. Save what works.</p></div><a class="link" href="/">Robot dashboard ↗</a></header>
<main>
<div class="toolbar"><span id="session" role="status">Connecting to Bob…</span><button id="arm" class="primary">Arm controls</button><button id="stop" class="stop">Stop &amp; hold</button></div>
<div id="message" role="status" aria-live="polite"></div>
<div class="grid">
<div>
<section class="card"><div class="eyebrow">01 / Pose</div><h2>Four joints, one pose</h2><p class="small">Angles are commanded positions. Your servos do not report their actual shaft angle.</p>
<div id="joints"></div><p id="pinMap" class="small">Reading active servo pin map…</p>
<div class="row"><label>Move time · ms<input id="moveMs" type="number" min="100" max="5000" step="100" value="800"></label><label>Hold time · ms<input id="holdMs" type="number" min="0" max="5000" step="100" value="200"></label></div>
<label class="check"><input id="livePose" type="checkbox">Preview slider changes live</label>
<div class="row"><button id="preview" class="primary">Preview pose</button><button id="capture">Use current command</button></div>
<button id="add" style="width:100%">＋ Add keyframe</button>
<div class="row"><button id="centre">Return to saved idle</button><button id="calibrate">Calibrate IMU</button></div>
<label>Small test range around idle · °<input id="idleRadius" type="number" min="1" max="10" value="3"></label>
<button id="captureIdle">Save current pose as idle</button>
<p id="idleInfo" class="small">Capturing idle saves the current command without moving any servo. It sets a small travel range, speed 5°/s, correction 1°, cutoff 10° and zero PID gains. Calibrate the IMU while still. A stop requires fresh IMU calibration before PID runs again.</p>
</section>
<section class="card"><div class="eyebrow">Live / 4 Hz display</div><h2>See the response</h2>
<div class="metrics"><div class="metric"><small>Pitch</small><span id="pitch">—</span></div><div class="metric"><small>Roll</small><span id="roll">—</span></div><div class="metric"><small>RMS tilt · PID session</small><span id="rms">—</span></div><div class="metric"><small>Peak tilt</small><span id="peak">—</span></div></div>
<canvas id="chart" width="620" height="280" aria-label="Recent pitch and roll history"></canvas>
<div class="legend"><span style="color:var(--mint)">● Pitch</span><span style="color:var(--orange)">● Roll</span><span class="muted">±45°</span></div>
<div id="telemetry" class="muted">Waiting for telemetry.</div><button id="csv" style="margin-top:12px">Download telemetry CSV</button>
<div style="margin-top:16px"><button id="cameraToggle">Start camera</button><img id="camera" alt="Bob camera stream" hidden style="width:100%;border-radius:10px;margin-top:10px"><p class="small">Keep this tab visible while tuning. Bob's onboard camera may not show his legs; observe the mechanics directly too.</p></div>
</section>
</div>
<section class="card"><div class="eyebrow">02 / Sequence</div><h2>Build a movement</h2>
<label>Movement type<select id="seqMode"><option value="joints">Joint poses (absolute angles)</option><option value="tilt">Posture — PID tilt (target pitch/roll)</option></select></label>
<p class="small">Posture movements set a target body orientation each frame; with PID enabled the four motors drive the IMU to it. Enable and tune PID before playing them.</p>
<h3>Starter movements</h3>
<p class="small">Load an editable template, then test with Bob supported. Loading never moves the motors. These patterns have not been physically tuned.</p>
<label>Template<select id="preset"></select></label>
<div class="row"><label>Amplitude · °<input id="amplitude" type="number" min="1" max="15" value="2"></label><label>Tempo · BPM<input id="tempo" type="number" min="30" max="120" value="60"></label></div>
<button id="buildPreset">Load template into editor</button>
<p id="readiness" class="small notice">Waiting for readiness checks.</p>
<label>Expression during movement<select id="expression"><option value="-1">Keep current face</option><option value="0">Happy</option><option value="1">Sad</option><option value="2">Angry</option><option value="3">Surprised</option><option value="4">Sleepy</option><option value="5">Love</option><option value="6">Normal</option></select></label>
<label>Movement name<input id="name" maxlength="24" value="My first move"></label>
<div id="frames" class="frames"></div>
<div class="row"><label>Repetitions<input id="repeats" type="number" min="1" max="10" value="1"></label><button id="play" class="primary">Play sequence</button></div>
<p class="small">Smooth transitions respect each joint's speed limit. Slow limits can extend a keyframe. Playback holds the final pose.</p>
<div class="row"><button id="export">Export JSON</button><button id="import">Import JSON</button><input id="file" type="file" accept=".json,application/json" hidden></div>
<h3>Saved on Bob</h3><p class="small">Six movement slots survive power-off. Saving replaces the selected slot.</p>
<label>Storage slot<select id="slot"></select></label>
<div class="row"><button id="load">Load slot</button><button id="save">Save / replace slot</button><button id="remove">Delete slot</button></div>
<p id="storage" class="muted">Reading saved movements…</p>
</section>
<section class="card tuning"><div class="eyebrow">03 / Posture control</div><h2>PID tuning</h2>
<p class="small">The MPU measures body tilt. PID adds a small correction to your poses. It cannot infer a stable walking gait or calibrate the motor directions by itself.</p>
<div class="scroll"><table><thead><tr><th>Axis</th><th>P</th><th>I</th><th>D</th></tr></thead><tbody id="gains"></tbody></table></div>
<div class="row"><label>Pitch target · °<input id="targetPitch" type="number" min="-15" max="15" step="1" value="0"></label><label>Roll target · °<input id="targetRoll" type="number" min="-15" max="15" step="1" value="0"></label></div>
<button id="pid" style="width:100%">Enable PID</button>
<h3>Limits &amp; correction directions</h3>
<p class="small notice">Check the correction direction with Bob supported. The correct sign depends on how each servo and horn are mounted.</p>
<div class="scroll"><table><thead><tr><th>Joint</th><th>Min °</th><th>Max °</th><th>Pitch</th><th>Roll</th></tr></thead><tbody id="limits"></tbody></table></div>
<div class="row"><label>Correction limit · °<input id="correction" type="number" min="1" max="15" value="8"></label><label>Speed · °/s<input id="slew" type="number" min="5" max="120" value="40"></label></div>
<label>Stop beyond body tilt · °<input id="cutoff" type="number" min="10" max="60" value="35"></label>
<label class="check"><input id="liveTune" type="checkbox">Apply tuning changes live</label>
<div class="row"><button id="apply">Apply settings</button><button id="persist">Save settings on Bob</button></div>
<p class="small">Start with I = 0 and a small P. PID stays off after restart. Playback and PID stop if this tab loses its heartbeat; blocking audio and older movement controls wait until you stop this session.</p>
</section>
</div>
<p class="muted footnote">For remote tuning, use the same local HTTP controls documented in BOB_V2/MOTION_STUDIO.md. Camera video remains available from the dashboard. An accepted motor command confirms PWM output, not physical movement.</p>
</main><script>
(() => {
'use strict';
const $=id=>document.getElementById(id), names=['Left hip','Left foot','Right hip','Right foot'], short=['LH','LF','RH','RF'];
let token='', state=null, configLoaded=false, frames=[], slots=[], busyBeat=false, poseTimer, tuneTimer, rows=[], statusBusy=false, seqMode='joints';
function message(s,error=false){$('message').textContent=s;$('message').style.color=error?'#ffb878':'#9aefca';}
async function request(path,body){
  const options=body===undefined?{}:{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(path==='/motion/stop'?body:{...body,token})};
  const r=await fetch(path,options), text=await r.text();let d;
  try{d=JSON.parse(text);}catch(_){d={error:text};}
  if(!r.ok)throw Error(d.error||'Request failed');return d;
}
function act(fn){return async()=>{try{await fn();}catch(e){message(e.message,true);}};}
function number(id,lo,hi){const v=Number($(id).value);if($(id).value.trim()===''||!Number.isFinite(v)||v<lo||v>hi)throw Error(id+': expected '+lo+' to '+hi);return v;}
function pose(){return names.map((_,i)=>number('angle'+i,0,180));}
function frameNow(){const moveMs=number('moveMs',100,5000),holdMs=number('holdMs',0,5000);if(!Number.isInteger(moveMs)||!Number.isInteger(holdMs))throw Error('Times must be whole milliseconds');if(seqMode==='tilt')return{tilt:[number('targetPitch',-15,15),number('targetRoll',-15,15)],moveMs,holdMs};return{angles:pose(),moveMs,holdMs};}
function sequence(){const name=$('name').value.trim();if(!name||new TextEncoder().encode(name).length>24)throw Error('Use a name of 1–24 bytes');if(!frames.length)throw Error('Add at least one keyframe');return{name,mode:seqMode,expression:number('expression',-1,6),frames};}
function own(){if(!token||!state?.armed)throw Error('Arm controls first');}
function setPose(angles){angles.forEach((v,i)=>{$('angle'+i).value=String(v);$('slide'+i).value=String(v);});}
names.forEach((name,i)=>{
  const node=document.createElement('div');node.className='joint';
  node.innerHTML='<div class="row"><label for="slide'+i+'">'+name+' · '+short[i]+'</label><input aria-label="'+name+' angle" id="angle'+i+'" type="number" min="60" max="120" value="90"></div><input aria-label="'+name+' slider" id="slide'+i+'" type="range" min="60" max="120" value="90">';
  $('joints').append(node);
  ['slide','angle'].forEach(prefix=>$(prefix+i).addEventListener('input',()=>{
    $(prefix==='slide'?'angle'+i:'slide'+i).value=$(prefix+i).value;
    clearTimeout(poseTimer);if($('livePose').checked)poseTimer=setTimeout(act(preview),180);
  }));
  const row=document.createElement('tr');row.innerHTML='<td>'+short[i]+'</td>'+
    ['low','high'].map((key,k)=>'<td><input aria-label="'+name+' '+key+'" id="'+key+i+'" type="number" min="0" max="180" value="'+(k?120:60)+'"></td>').join('')+
    ['pitchMap','rollMap'].map(key=>'<td><select aria-label="'+name+' '+key+'" id="'+key+i+'"><option value="-1">−</option><option value="0">0</option><option value="1">＋</option></select></td>').join('');
  $('limits').append(row);
});
['Pitch','Roll'].forEach((axis,a)=>{const tr=document.createElement('tr');tr.innerHTML='<td>'+axis+'</td>'+['kp','ki','kd'].map((key,i)=>'<td><input aria-label="'+axis+' '+key+'" id="'+key+a+'" type="number" min="0" max="'+(i?2:5)+'" step="0.01" value="'+[.35,0,.03][i]+'"></td>').join('');$('gains').append(tr);});
function readConfig(){
  return{neutral:state.config.neutral,customNeutral:state.config.customNeutral,gains:[0,1].map(a=>['kp','ki','kd'].map((g,i)=>number(g+a,0,i?2:5))),
    joints:names.map((_,i)=>[number('low'+i,0,179),number('high'+i,1,180),number('pitchMap'+i,-1,1),number('rollMap'+i,-1,1)]),
    correction:number('correction',1,15),slew:number('slew',5,120),cutoff:number('cutoff',10,60)};
}
function fillConfig(c){
  c.gains.forEach((g,a)=>g.forEach((v,i)=>$(['kp','ki','kd'][i]+a).value=v));
  c.joints.forEach((j,i)=>j.forEach((v,k)=>$(['low','high','pitchMap','rollMap'][k]+i).value=v));
  ['correction','slew','cutoff'].forEach(k=>$(k).value=c[k]);
}
function limits(c){c.joints.forEach((j,i)=>['angle','slide'].forEach(k=>{$(k+i).min=j[0];$(k+i).max=j[1];}));}
async function apply(persist=false){own();await request('/motion/config',{config:readConfig(),persist});message(persist?'Settings saved on Bob.':'Tuning applied.');}
document.querySelectorAll('.tuning input,.tuning select').forEach(el=>el.addEventListener('change',()=>{
  clearTimeout(tuneTimer);if($('liveTune').checked&&token)tuneTimer=setTimeout(act(()=>apply()),300);
}));
function renderFrames(){
  $('frames').replaceChildren();
  if(!frames.length){const e=document.createElement('div');e.className='empty';e.textContent='Your timeline starts with a pose. Add a keyframe from the left.';$('frames').append(e);return;}
  frames.forEach((f,i)=>{
    const item=document.createElement('div');item.className='frame';item.dataset.frame=i;
    const head=document.createElement('div');head.className='framehead';const title=document.createElement('strong');title.textContent='Keyframe '+(i+1);head.append(title);
    const isTilt=Array.isArray(f.tilt);
    [['Use',()=>{if(isTilt){$('targetPitch').value=f.tilt[0];$('targetRoll').value=f.tilt[1];}else{setPose(f.angles);}$('moveMs').value=f.moveMs;$('holdMs').value=f.holdMs;}],['↑',()=>{if(i){[frames[i-1],frames[i]]=[frames[i],frames[i-1]];renderFrames();}}],['×',()=>{frames.splice(i,1);renderFrames();}]].forEach(([label,fn])=>{const b=document.createElement('button');b.textContent=label;b.setAttribute('aria-label',(label==='×'?'Remove':label==='↑'?'Move up':'Use')+' keyframe '+(i+1));b.onclick=fn;head.append(b);});
    item.append(head);const angles=document.createElement('div');angles.className='frameangles';
    if(isTilt){[['Pitch°',0],['Roll°',1]].forEach(([lbl,k])=>{const label=document.createElement('label');label.textContent=lbl;const input=document.createElement('input');input.type='number';input.min=-15;input.max=15;input.step=0.1;input.value=f.tilt[k];input.setAttribute('aria-label','Frame '+(i+1)+' '+lbl);input.onchange=()=>{f.tilt[k]=input.value===''?NaN:Number(input.value);};label.append(input);angles.append(label);});}
    else{f.angles.forEach((v,ch)=>{const label=document.createElement('label');label.textContent=short[ch];const input=document.createElement('input');input.type='number';input.min=0;input.max=180;input.value=v;input.setAttribute('aria-label','Frame '+(i+1)+' '+short[ch]);input.onchange=()=>{f.angles[ch]=input.value===''?NaN:Number(input.value);};label.append(input);angles.append(label);});}
    item.append(angles);const time=document.createElement('div');time.className='row';
    ['moveMs','holdMs'].forEach(key=>{const label=document.createElement('label');label.textContent=key==='moveMs'?'Move · ms':'Hold · ms';const input=document.createElement('input');input.type='number';input.min=key==='moveMs'?100:0;input.max=5000;input.value=f[key];input.setAttribute('aria-label','Frame '+(i+1)+' '+key);input.onchange=()=>{f[key]=input.value===''?NaN:Number(input.value);};label.append(input);time.append(label);});
    item.append(time);$('frames').append(item);
  });
}
async function library(){
  const d=await request('/motion/library');slots=d.slots;const selected=$('slot').value;
  $('slot').replaceChildren();slots.forEach(s=>{const o=document.createElement('option');o.value=s.slot;o.textContent=(s.slot+1)+' / '+(s.sequence?.name||'Empty');$('slot').append(o);});if(selected)$('slot').value=selected;
  $('storage').textContent=slots.filter(s=>s.sequence).length+' of 6 slots used.';
}
function download(name,text,type){const u=URL.createObjectURL(new Blob([text],{type}));const a=document.createElement('a');a.href=u;a.download=name;a.click();setTimeout(()=>URL.revokeObjectURL(u),1000);}
function acceptSequence(s){
  if(!s||typeof s.name!=='string'||!s.name.trim()||new TextEncoder().encode(s.name).length>24||!Array.isArray(s.frames)||!s.frames.length||s.frames.length>16)throw Error('Invalid movement JSON');
  const mode=(s.mode==='tilt')?'tilt':(s.mode===undefined||s.mode==='joints')?'joints':null;
  if(!mode)throw Error('Unknown movement mode');
  for(const f of s.frames){
    if(!Number.isInteger(f.moveMs)||f.moveMs<100||f.moveMs>5000||!Number.isInteger(f.holdMs)||f.holdMs<0||f.holdMs>5000)throw Error('Invalid keyframe timing');
    if(mode==='tilt'){if(!Array.isArray(f.tilt)||f.tilt.length!==2||f.tilt.some(x=>typeof x!=='number'||!Number.isFinite(x)||x< -15||x>15))throw Error('Invalid tilt target (pitch/roll must be −15..15)');}
    else{if(!Array.isArray(f.angles)||f.angles.length!==4||f.angles.some(x=>typeof x!=='number'||!Number.isFinite(x)||x<0||x>180))throw Error('Invalid keyframe');}
  }
  if(s.expression!==undefined&&(!Number.isInteger(s.expression)||s.expression< -1||s.expression>6))throw Error('Invalid expression');
  seqMode=mode;$('seqMode').value=mode;fillPresets();
  $('expression').value=s.expression??-1;
  $('name').value=s.name;frames=structuredClone(s.frames);renderFrames();
  message('Movement loaded into editor.'+(mode==='tilt'?' Posture mode — enable PID to play.':''));
}
// Normalized joint offsets, scaled around the editable neutral pose.
const presets=[
  {name:'Gentle sway',face:0,poses:[[0,1,0,-1],[0,-1,0,1]]},
  {name:'Happy bounce',face:0,poses:[[.5,1,-.5,-1],[-.5,-1,.5,1]]},
  {name:'Curious lean',face:3,poses:[[.5,.8,.5,-.8],[0,0,0,0],[-.5,-.8,-.5,.8]]},
  {name:'Little bow',face:5,poses:[[1,0,1,0],[1,.3,1,-.3],[0,0,0,0]]},
  {name:'Sleepy rock',face:4,poses:[[.3,.5,.3,-.5],[-.3,-.5,-.3,.5]]},
  {name:'Twist dance',face:0,poses:[[1,.5,-1,-.5],[-1,-.5,1,.5],[1,-.5,-1,.5],[-1,.5,1,-.5]]},
  {name:'Step-touch study',face:0,poses:[[0,1,0,-1],[1,1,-1,-1],[0,0,0,0],[0,-1,0,1],[-1,-1,1,1]]}
];
// Posture templates: base target orientations (pitch,roll) scaled by amplitude.
const postures=[
  {name:'Nod',face:0,targets:[[6,0],[-3,0],[0,0]]},
  {name:'Shake head',face:2,targets:[[0,6],[0,-6],[0,6],[0,-6],[0,0]]},
  {name:'Gentle sway',face:4,targets:[[0,4],[0,-4],[0,0]]},
  {name:'Wobble',face:3,targets:[[3,3],[-3,3],[-3,-3],[3,-3],[0,0]]},
  {name:'Bow',face:5,targets:[[8,0],[0,0]]},
  {name:'Perk up',face:0,targets:[[-5,0],[0,0]]}
];
function fillPresets(){$('preset').replaceChildren();(seqMode==='tilt'?postures:presets).forEach((p,i)=>{const o=document.createElement('option');o.value=i;o.textContent=p.name;$('preset').append(o);});}
fillPresets();
$('seqMode').onchange=()=>{seqMode=$('seqMode').value;frames=[];fillPresets();renderFrames();message(seqMode==='tilt'?'Posture mode: frames are target pitch/roll; enable PID to play.':'Joint mode: frames are joint angles.');};
$('buildPreset').onclick=act(()=>{
  if(!state)throw Error('Connect to Bob to read travel limits first');
  const beat=Math.round(60000/number('tempo',30,120)), amplitude=number('amplitude',1,15);
  if(seqMode==='tilt'){
    const p=postures[Number($('preset').value)], scale=amplitude/8;
    const built=[{tilt:[0,0],moveMs:beat,holdMs:200},...p.targets.map(t=>({tilt:[Math.round(t[0]*scale*10)/10,Math.round(t[1]*scale*10)/10],moveMs:beat,holdMs:150})),{tilt:[0,0],moveMs:beat,holdMs:400}];
    if(built.some(f=>f.tilt.some(x=>Math.abs(x)>15)))throw Error('Reduce amplitude; a tilt target exceeds ±15°.');
    acceptSequence({name:p.name,mode:'tilt',expression:p.face,frames:built});
    message('Posture template loaded. Calibrate, arm, enable PID, then play while observing.');
    return;
  }
  const p=presets[Number($('preset').value)], neutral=pose();
  const built=[{angles:neutral.slice(),moveMs:beat,holdMs:200},...p.poses.map(offset=>({angles:offset.map((v,i)=>Math.round((neutral[i]+v*amplitude)*10)/10),moveMs:beat,holdMs:100})),{angles:neutral.slice(),moveMs:beat,holdMs:400}];
  if(built.some(f=>f.angles.some((v,i)=>v<state.config.joints[i][0]||v>state.config.joints[i][1])))throw Error('Template exceeds travel limits. Reduce amplitude or adjust the starting pose.');
  acceptSequence({name:p.name,mode:'joints',expression:p.face,frames:built});
  message('Template loaded around the pose sliders. Review keyframes, then arm and play.');
});
async function preview(){own();await request('/motion/play',{sequence:{name:'Pose preview',mode:seqMode,expression:-1,frames:[frameNow()]},repeats:1});message(seqMode==='tilt'?'Previewing tilt target (needs PID enabled).':'Previewing pose.');}
$('preview').onclick=act(preview);
$('add').onclick=act(()=>{if(frames.length>=16)throw Error('Maximum 16 keyframes');frames.push(frameNow());renderFrames();message('Keyframe added.');});
$('capture').onclick=act(()=>{if(!state)throw Error('No telemetry yet');setPose(state.commanded);message('Copied commanded positions; these are not measured joint angles.');});
$('arm').onclick=act(async()=>{const d=await request('/motion/arm',{});token=d.token;rows=[];await poll();message('Controls armed. PID remains off.');});
$('stop').onclick=act(async()=>{clearTimeout(poseTimer);clearTimeout(tuneTimer);await request('/motion/stop',{});token='';$('livePose').checked=$('liveTune').checked=false;await poll();message('Stopped. Motors hold the last command.');});
$('centre').onclick=act(async()=>{own();if(!state.config.customNeutral)throw Error('Save the assembled idle pose first');await request('/motion/play',{sequence:{name:'Return to idle',frames:[{angles:state.config.neutral,moveMs:2000,holdMs:400}]},repeats:1});message('Returning slowly to saved idle.');});
$('captureIdle').onclick=act(async()=>{if(!state||state.armed)throw Error('Stop and refresh before capturing idle');if(state.apiVersion<2||!state.apiVersion)throw Error('Upload the updated firmware to capture a custom idle pose');await request('/motion/setup',{expected:state.commanded,radius:number('idleRadius',1,10)});configLoaded=false;await poll();message('Idle pose and small test limits saved on Bob. No servo movement. PID gains reset to zero.');});
$('calibrate').onclick=act(async()=>{if(state?.armed)throw Error('Stop before calibrating');message('Keep Bob still for three seconds…');await request('/imucal');message('IMU calibrated. You can arm and enable PID.');});
$('play').onclick=act(async()=>{own();const seq=sequence();acceptSequence(seq);await request('/motion/play',{sequence:seq,repeats:number('repeats',1,10)});message('Playing '+seq.name);});
$('apply').onclick=act(()=>apply());$('persist').onclick=act(()=>apply(true));
$('pid').onclick=act(async()=>{own();await request('/motion/pid',{enabled:!state.pid,target:[number('targetPitch',-15,15),number('targetRoll',-15,15)]});await poll();message(state.pid?'PID enabled. Watch the response.':'PID off.');});
['targetPitch','targetRoll'].forEach(id=>$(id).addEventListener('change',act(async()=>{if(token&&state?.pid)await request('/motion/pid',{enabled:true,target:[number('targetPitch',-15,15),number('targetRoll',-15,15)]});})));
$('save').onclick=act(async()=>{own();const seq=sequence();acceptSequence(seq);await request('/motion/save',{slot:Number($('slot').value),sequence:seq});await library();message('Movement saved on Bob.');});
$('load').onclick=act(()=>{const s=slots.find(s=>s.slot===Number($('slot').value));if(!s?.sequence)throw Error('This slot is empty');acceptSequence(s.sequence);});
$('remove').onclick=act(async()=>{own();await request('/motion/delete',{slot:Number($('slot').value)});await library();message('Slot deleted.');});
$('export').onclick=act(()=>{const seq=sequence();acceptSequence(seq);download('bob-movement.json',JSON.stringify({version:1,...seq},null,2),'application/json');});
$('import').onclick=()=>$('file').click();
$('file').onchange=act(async()=>{const f=$('file').files[0];if(!f)return;if(f.size>8192)throw Error('File exceeds 8192 bytes');acceptSequence(JSON.parse(await f.text()));$('file').value='';});
$('csv').onclick=()=>download('bob-motion-telemetry.csv','time_ms,pitch,roll,pid,frame,lh,lf,rh,rf,pitch_correction,roll_correction,limited,sample_age_ms\n'+rows.map(r=>r.join(',')).join('\n'),'text/csv');
$('cameraToggle').onclick=()=>{const img=$('camera');if(img.hidden){img.hidden=false;img.src=location.protocol+'//'+location.hostname+':81/stream';$('cameraToggle').textContent='Stop camera';}else{img.hidden=true;img.removeAttribute('src');$('cameraToggle').textContent='Start camera';}};
$('camera').onerror=()=>{message('Camera stream unavailable; check Camera in the dashboard.',true);$('camera').hidden=true;$('cameraToggle').textContent='Start camera';};
const history=[];
function chart(){
  const c=$('chart'),ctx=c.getContext('2d'),w=c.width,h=c.height;ctx.clearRect(0,0,w,h);ctx.strokeStyle='#2b3b4e';ctx.beginPath();ctx.moveTo(0,h/2);ctx.lineTo(w,h/2);ctx.stroke();
  ['#9aefca','#ffb878'].forEach((color,axis)=>{ctx.strokeStyle=color;ctx.lineWidth=3;ctx.beginPath();history.forEach((p,i)=>{const x=i/119*w,y=h/2-Math.max(-45,Math.min(45,p[axis]))/45*h/2;i?ctx.lineTo(x,y):ctx.moveTo(x,y);});ctx.stroke();});
}
async function poll(){
  if(statusBusy)return;statusBusy=true;
  try{
    state=await request('/motion/state');
    if(token&&!state.armed){token='';$('livePose').checked=$('liveTune').checked=false;}
    if(!configLoaded){fillConfig(state.config);setPose(state.commanded);configLoaded=true;}limits(state.config);
    const outside=state.commanded.map((v,i)=>v<state.config.joints[i][0]||v>state.config.joints[i][1]?short[i]:null).filter(Boolean);
    $('readiness').textContent=outside.length?'Outside travel limits: '+outside.join(', ')+'. If this is the correct standing pose, save it as idle before arming.':!state.imuReady?'IMU offline; PID unavailable.':state.imuSampleValid===false?'IMU sample unsuitable for posture control; keep Bob still and check sensor data.':!state.calibrated?'IMU needs calibration in the supported standing pose before PID tuning.':'Ready for a supported trial. Verify motor directions before enabling PID.';
    $('session').textContent=state.armed?(token?'● You control Bob':'● Another client controls Bob'):'○ '+state.reason;
    $('arm').disabled=state.armed;$('calibrate').disabled=$('captureIdle').disabled=state.armed;$('centre').disabled=!token||!state.config.customNeutral;
    ['preview','play','save','remove','apply','persist'].forEach(id=>$(id).disabled=!token);
    $('pid').disabled=!token||(!state.pid&&(!state.calibrated||!state.imuReady||state.imuSampleValid===false));$('pid').textContent=state.pid?'Disable PID':'Enable PID';
    $('pitch').textContent=state.pitch.toFixed(1)+'°';$('roll').textContent=state.roll.toFixed(1)+'°';
    $('rms').textContent=state.rmsTilt.toFixed(1)+'°';$('peak').textContent=state.peakTilt.toFixed(1)+'°';
    $('telemetry').textContent=(state.imuReady?'IMU online':'IMU offline')+' · '+(state.calibrated?'calibrated':'needs calibration')+' · sample '+state.sampleAgeMs+' ms · '+(state.limited?'limits active':'within limits')+' · command '+state.commanded.map(x=>Math.round(x)+'°').join(' / ');
    document.querySelectorAll('.frame').forEach(n=>n.classList.toggle('active',state.playing&&Number(n.dataset.frame)===state.frame));
    history.push([state.pitch,state.roll]);if(history.length>120)history.shift();chart();
    if(token){rows.push([Date.now(),state.pitch,state.roll,state.pid?1:0,state.frame,...state.commanded,...state.correction,state.limited?1:0,state.sampleAgeMs]);if(rows.length>2400)rows.shift();}
  }catch(e){$('session').textContent='Connection unavailable';message(e.message,true);}finally{statusBusy=false;}
}
setInterval(async()=>{if(!token||busyBeat)return;busyBeat=true;try{await request('/motion/heartbeat',{});}catch(e){token='';message('Control connection lost; Bob will stop when the heartbeat expires.',true);}finally{busyBeat=false;}},800);
function release(){if(token){fetch('/motion/stop',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({token}),keepalive:true}).catch(()=>{});token='';}}
window.addEventListener('pagehide',release);
document.addEventListener('visibilitychange',()=>{if(document.hidden)release();});
request('/servomap').then(m=>{$('pinMap').textContent='Active GPIOs · '+short.map((n,i)=>n+' '+m.pins[i]).join(' / ');}).catch(()=>{$('pinMap').textContent='Pin map unavailable; check the dashboard.';});
renderFrames();poll();library().catch(e=>message(e.message,true));setInterval(poll,250);
})();
</script></body></html>
)motionhtml";
