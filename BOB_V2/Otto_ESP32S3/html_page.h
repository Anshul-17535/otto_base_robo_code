// ⚠ This is the EDITABLE SOURCE of the web UI. The firmware does NOT include
//   this file directly — it serves the gzip-compressed copy in web_gz.h.
//   After editing this file, run:  python3 tools/gen_web_gz.py
//
// Web UI page for Otto - kept in its OWN header file on purpose.
// The Arduino IDE sketch preprocessor scans .ino files to auto-generate
// C++ prototypes and gets confused by the JavaScript inside this raw
// string (it emitted bogus 'function toast(m);' prototypes and broke the
// build). Header files are passed straight to the real compiler, which
// handles raw strings correctly. Do NOT move this back into the .ino.
#pragma once
#include <pgmspace.h>

const char HTML_PAGE[] PROGMEM = R"rawhtml(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Anvyxo • Robot Studio</title>
<style>
*{box-sizing:border-box;margin:0;padding:0}
:root{color-scheme:dark;--bg:#0b111a;--surface:#141d29;--line:#2a3748;--mint:#9aefca;--muted:#a0aec0;--text:#edf2f8}
body{font-family:-apple-system,"Segoe UI",system-ui,sans-serif;background:var(--bg);color:var(--text);font-size:14px;line-height:1.5;padding-bottom:60px}
a{color:var(--mint);text-decoration:none}
button,input,select{font-family:inherit}
button{transition:filter .16s,transform .16s;cursor:pointer}
button:hover{filter:brightness(1.18)}
button:focus-visible,input:focus-visible,select:focus-visible{outline:2px solid var(--mint)!important;outline-offset:3px}
button:disabled{opacity:.45;cursor:wait}
button{min-height:38px}
input,select{max-width:100%}

/* ── Splash (shown instantly on first paint, fades once UI is ready) ── */
#splash{position:fixed;inset:0;z-index:9999;background:radial-gradient(ellipse at 50% 38%,#16304a,var(--bg) 70%);display:flex;flex-direction:column;align-items:center;justify-content:center;gap:14px;transition:opacity .45s ease}
#splash.hide{opacity:0;pointer-events:none}
.splash-mark{font-size:40px;font-weight:800;letter-spacing:.42em;color:var(--mint);text-shadow:0 0 28px #9aefca55;padding-left:.42em;animation:splashPulse 1.4s ease-in-out infinite}
.splash-sub{font-size:11px;letter-spacing:.4em;color:var(--muted);padding-left:.4em}
.splash-bar{width:140px;height:4px;border-radius:3px;background:#1e2d3f;overflow:hidden;margin-top:6px}
.splash-bar i{display:block;height:100%;width:40%;border-radius:3px;background:var(--mint);animation:splashBar 1.1s ease-in-out infinite}
@keyframes splashPulse{0%,100%{opacity:1}50%{opacity:.55}}
@keyframes splashBar{0%{transform:translateX(-120%)}100%{transform:translateX(360%)}}

/* ── Header ── */
header{background:#141d29;border-bottom:1px solid var(--line);padding:14px 20px;display:flex;align-items:center;gap:10px;flex-wrap:wrap;position:sticky;top:0;z-index:200}
header h1{font-size:18px;font-weight:700;color:var(--text);flex:1;letter-spacing:.1em}
header h1 small{display:block;font-size:10px;color:var(--muted);letter-spacing:.2em;font-weight:400}
.dot{width:11px;height:11px;border-radius:50%;background:var(--mint);box-shadow:0 0 0 5px #9aefca18;flex-shrink:0}
#hdrIp{font-family:monospace;font-size:11px;color:var(--muted)}
#bRecover{background:#283345;color:#d3ddea;border:1px solid #3b4a60;border-radius:9px;padding:7px 11px;font-size:12px;font-weight:700;white-space:nowrap}

/* ── Tab bar ── */
.tab-bar{background:#0f1720;border-bottom:1px solid var(--line);display:flex;overflow-x:auto;position:sticky;top:53px;z-index:190;scrollbar-width:none;gap:2px;padding:0 6px}
.tab-bar::-webkit-scrollbar{display:none}
.tab-btn{flex:1 1 0;min-width:72px;padding:9px 10px 7px;font-size:12px;font-weight:600;color:var(--muted);border:none;background:transparent;border-bottom:3px solid transparent;white-space:nowrap;cursor:pointer;transition:color .15s,border-color .15s,background .15s;min-height:0;display:flex;flex-direction:column;align-items:center;gap:3px}
.tab-btn .ti{font-size:18px;line-height:1}
.tab-btn:hover{color:var(--text)}
.tab-btn.active{color:var(--mint);border-bottom-color:var(--mint);background:#13202c}
/* On wide screens the row centres instead of stretching full width */
@media(min-width:720px){.tab-bar{justify-content:center}.tab-btn{flex:0 0 auto;min-width:96px}}

/* ── Tab panels ── */
.tab-panel{display:none;max-width:960px;margin:0 auto;padding:22px 16px}
.tab-panel.active{display:block}

/* ── Cards ── */
.card{background:var(--surface);border:1px solid var(--line);border-radius:14px;padding:18px;margin-bottom:16px}
.card-title{font-size:11px;font-weight:700;color:#c2d1e2;letter-spacing:.1em;text-transform:uppercase;margin-bottom:14px;display:flex;justify-content:space-between;align-items:center}
.two-col{display:grid;grid-template-columns:1fr 1fr;gap:16px}
@media(max-width:600px){.two-col{grid-template-columns:1fr}}

/* ── Calibration banner ── */
.cal-banner{display:none;background:#7c2d12;color:#fed7aa;border:1px solid #ea580c;border-radius:10px;padding:12px 14px;margin-bottom:14px;font-size:13px;font-weight:700;line-height:1.5}

/* ── D-pad ── */
.dpad{display:grid;grid-template-columns:repeat(3,62px);grid-template-rows:repeat(3,58px);gap:8px;justify-content:center;padding:6px 0}
.dpad button{width:62px;height:58px;border-radius:12px;border:1px solid #34465e;background:#233246;color:var(--text);font-size:22px;-webkit-tap-highlight-color:transparent}
.dpad button:active{background:#477f71;transform:scale(.91)}
.dpad .stop{background:var(--mint);color:#103a2b;border-color:var(--mint);font-size:12px;font-weight:800;letter-spacing:.05em}
.dpad .empty{background:transparent;pointer-events:none;border:none}

/* ── Expression grid ── */
.expr-grid{display:grid;grid-template-columns:1fr 1fr 1fr;gap:8px}
.xbtn{padding:13px 4px;border-radius:11px;border:1px solid var(--line);font-size:21px;font-weight:800;cursor:pointer;-webkit-tap-highlight-color:transparent;line-height:1.5;text-align:center;background:var(--surface);color:var(--text)}
.xbtn .xl{font-size:10px;font-weight:700;display:block;margin-top:2px;color:#b7c5d5}
.xbtn:active{transform:scale(.93)}
.xbtn.active{border-color:var(--mint)!important;box-shadow:inset 0 0 0 1px var(--mint);background:#203c35}
.xbtn-happy{background:#0f2e18}.xbtn-sad{background:#0f1e3a}.xbtn-angry{background:#2e0f0f}
.xbtn-surprised{background:#1e1a00}.xbtn-sleepy{background:#1a1a2e}.xbtn-love{background:#2e0f1e}

/* ── Action / Dance buttons ── */
.act-row{display:grid;grid-template-columns:1fr 1fr;gap:8px}
.abtn{padding:12px 8px;border-radius:10px;border:none;font-size:13px;font-weight:700;-webkit-tap-highlight-color:transparent}
.abtn:active{transform:scale(.95)}
.abtn-dance{background:#393454;color:#e0d6ff}
.abtn-stand{background:#214d41;color:#c1f9df}
.dance-grid{display:grid;grid-template-columns:1fr 1fr 1fr;gap:6px;margin-top:8px}
.dbtn{padding:10px 4px;border-radius:10px;border:1px solid var(--line);font-size:11px;font-weight:700;cursor:pointer;-webkit-tap-highlight-color:transparent;text-align:center;line-height:1.4;color:var(--text);min-height:52px}
.dbtn:active{transform:scale(.94);filter:brightness(1.3)}

/* ── Sliders ── */
.sl-row{margin-bottom:14px}
.sl-row:last-child{margin-bottom:0}
.sl-top{display:flex;justify-content:space-between;margin-bottom:6px;font-size:12px;color:#b2c0d0}
.sl-top span{color:#7dd3fc;font-weight:700;min-width:36px;text-align:right}
input[type=range]{width:100%;-webkit-appearance:none;height:6px;border-radius:3px;background:#334155;outline:none;cursor:pointer;accent-color:var(--mint)}
input[type=range]::-webkit-slider-thumb{-webkit-appearance:none;width:20px;height:20px;border-radius:50%;background:var(--mint);cursor:pointer}

/* ── Trim grid ── */
.trim-item .tlabel{font-size:11px;color:var(--muted);margin-bottom:5px}
.trim-ctrl{display:flex;align-items:center;gap:5px}
.trim-ctrl button{width:36px;height:34px;min-height:34px;border-radius:6px;border:none;background:#334155;color:var(--text);font-size:14px;font-weight:700;flex-shrink:0}
.trim-ctrl button:active{background:#3b82f6}
.trim-ctrl button.big{width:34px;font-size:11px;font-weight:800;color:#fbbf24;background:#2d2008}
.trim-ctrl button.big:active{background:#a16207}
.trim-val{flex:1;text-align:center;font-size:15px;font-weight:800;color:#7dd3fc;font-family:monospace}
.save-bar{display:flex;gap:8px;margin-top:12px;flex-wrap:wrap}
.sbtn{flex:1;padding:11px 8px;border-radius:9px;border:none;font-size:13px;font-weight:700;min-height:38px}
.sbtn:active{transform:scale(.96)}
.sbtn-save{background:#166534;color:#bbf7d0}
.sbtn-reset{background:#450a0a;color:#fca5a5}
.code-box{background:#0a1118;border:1px solid #1e3a5f;border-radius:6px;padding:9px 11px;font-family:monospace;font-size:11px;color:#86efac;word-break:break-all;margin-top:10px;line-height:1.7}
.copy-btn{width:100%;margin-top:7px;padding:8px;border-radius:7px;border:none;background:#1e3a5f;color:#7dd3fc;font-size:11px;font-weight:700}
.copy-btn:active{background:#1d4ed8;color:#fff}

/* ── Audio / Speak ── */
.audio-row{display:flex;gap:8px;margin-bottom:10px}
.rec-btn{flex:1;padding:12px 8px;border-radius:9px;border:none;font-size:13px;font-weight:700;background:#450a0a;color:#fca5a5;-webkit-tap-highlight-color:transparent;transition:background .15s}
.rec-btn.active{background:#dc2626;color:#fff;animation:pulse .6s infinite alternate}
@keyframes pulse{from{opacity:1}to{opacity:.6}}
.speak-row{display:flex;gap:6px;flex-wrap:wrap}
.speak-input{flex:1;padding:10px;border-radius:8px;border:1px solid var(--line);background:#0d1520;color:var(--text);font-size:13px;outline:none;min-width:0}
.speak-input:focus{border-color:#3b82f6}
.speak-btn{padding:10px 15px;border-radius:8px;border:none;background:#1e3a5f;color:#7dd3fc;font-size:13px;font-weight:700;white-space:nowrap}
.speak-btn:active{background:#1d4ed8;color:#fff;transform:scale(.95)}

/* ── Log ── */
.log-hdr{display:flex;justify-content:space-between;align-items:center;margin-bottom:8px}
.log-hdr span{font-size:10px;font-weight:700;color:var(--muted);text-transform:uppercase;letter-spacing:.06em}
.log-hdr button{background:none;border:none;color:var(--muted);font-size:10px;cursor:pointer;min-height:0}
#log{height:180px;overflow-y:auto;font-family:monospace;font-size:11px;line-height:1.8}
.lo{color:#64748b}.lk{color:#4ade80}.lw{color:#fbbf24}.le{color:#f87171;font-weight:700}.lh{color:#7dd3fc;font-weight:700}

/* ── IMU cube ── */
#imuCube{width:96px;height:96px;position:relative;transform-style:preserve-3d;will-change:transform}
.cube-face{position:absolute;width:96px;height:96px;border:1px solid #527b98;border-radius:9px;background:linear-gradient(135deg,#27465c,#102335);backface-visibility:hidden;display:flex;flex-direction:column;gap:10px;align-items:center;justify-content:center;font-size:10px;font-weight:800;letter-spacing:2px}
.imu-stage{display:grid;place-items:center;perspective:650px;width:200px;height:220px;border-radius:16px;background:radial-gradient(ellipse,#183c50,#0c1523 72%);flex-shrink:0}
.imu-camera{transform:rotateX(-18deg) rotateY(-28deg);transform-style:preserve-3d}
.cube-eyes{display:flex;gap:12px}.cube-eyes i{width:18px;height:9px;border-radius:3px;background:#85ffee;box-shadow:0 0 10px #67ffee88}
.cube-vents{width:42px;height:20px;background:repeating-linear-gradient(0deg,transparent 0 4px,#ffca86 4px 6px)}
body.imu-focus .tab-bar,body.imu-focus header{display:none}body.imu-focus .imu-stage{width:min(52vw,520px);height:65vh}body.imu-focus #imuCube{zoom:2}
@media(max-width:520px){.imu-stage{width:140px;height:185px}body.imu-focus .imu-stage{width:50vw}body.imu-focus #imuCube{zoom:1.25}}

/* ── Toast ── */
#toast{position:fixed;bottom:18px;left:50%;transform:translateX(-50%);background:var(--mint);color:#103a2b;padding:10px 20px;border-radius:12px;font-size:12px;font-weight:700;opacity:0;transition:opacity .25s;pointer-events:none;white-space:normal;max-width:calc(100vw - 24px);z-index:999;box-shadow:0 8px 32px #0008;text-align:center}

@media(prefers-reduced-motion:reduce){*,*::before,*::after{animation:none!important;transition:none!important}}
</style>
</head>
<body>
<div id="splash">
  <div class="splash-mark">ANVYXO</div>
  <div class="splash-sub">ROBOT STUDIO</div>
  <div class="splash-bar"><i></i></div>
</div>
<header>
  <div class="dot"></div>
  <h1>ANVYXO<small>ROBOT STUDIO / V2</small></h1>
  <button id="bRecover" title="Re-initialise the screen and return to the Normal face">&#128268; Recover Screen</button>
  <span id="hdrIp">192.168.4.1</span>
</header>

<nav class="tab-bar" role="tablist">
  <button class="tab-btn active" data-tab="move"   role="tab"><span class="ti">&#127918;</span>Move</button>
  <button class="tab-btn"        data-tab="motors" role="tab"><span class="ti">&#9881;</span>Motors</button>
  <button class="tab-btn"        data-tab="speaker"role="tab"><span class="ti">&#127908;</span>Speaker</button>
  <button class="tab-btn"        data-tab="imu"    role="tab"><span class="ti">&#127760;</span>IMU</button>
  <button class="tab-btn"        data-tab="camera" role="tab"><span class="ti">&#128247;</span>Camera</button>
  <button class="tab-btn"        data-tab="system" role="tab"><span class="ti">&#128268;</span>System</button>
</nav>

<!-- ══════════════════════════════════════════════════════
     TAB: MOVE
══════════════════════════════════════════════════════ -->
<div id="tab-move" class="tab-panel active">

  <!-- IMU calibration banner (Move tab) -->
  <div id="calBanner" class="cal-banner">
    &#9888; IMU not calibrated this boot &#8212; place Anvyxo in his standing pose, then
    <button id="bBannerCal" style="margin-left:6px;background:#166534;color:#bbf7d0;border:none;border-radius:6px;padding:6px 12px;font-weight:700;cursor:pointer;min-height:34px">Calibrate Now</button>
  </div>

  <!-- Motion Studio link -->
  <div class="card">
    <a href="/motion" style="display:block;background:#214d41;color:#c1f9df;padding:14px;border-radius:10px;text-decoration:none;font-weight:700">Open Motion Studio &#8594;<br><span style="font-size:11px;font-weight:400">Four-joint poses &middot; saved sequences &middot; live PID tuning</span></a>
  </div>

  <!-- IMU Orientation card -->
  <div class="card">
    <div class="card-title">
      <span>&#127981; IMU Orientation</span>
      <span id="imuOrientStatus" style="font-size:10px;color:#64748b;text-transform:none;letter-spacing:0">connecting&#8230;</span>
    </div>
    <div style="display:flex;gap:14px;align-items:center;margin-bottom:10px">
      <div style="position:relative;width:80px;height:80px;border-radius:50%;border:2px solid var(--line);background:#0d1520;flex-shrink:0;overflow:hidden">
        <div style="position:absolute;left:50%;top:50%;transform:translate(-50%,-50%);width:1px;height:100%;background:#1e3a5f"></div>
        <div style="position:absolute;left:0;top:50%;width:100%;height:1px;background:#1e3a5f"></div>
        <div id="tiltBall" style="position:absolute;width:18px;height:18px;border-radius:50%;background:var(--mint);left:50%;top:50%;transform:translate(-50%,-50%);transition:left .12s,top .12s;box-shadow:0 0 8px #9aefca55"></div>
      </div>
      <div style="flex:1;font-size:11px">
        <div style="display:flex;gap:8px;align-items:center;margin-bottom:6px">
          <span style="color:#94a3b8;min-width:36px">Pitch</span>
          <span id="oPitch" style="color:#7dd3fc;font-family:monospace;font-weight:700;min-width:52px">0.0&#176;</span>
          <div style="flex:1;height:6px;border-radius:3px;background:#1e293b;overflow:hidden"><div id="oPitchBar" style="height:100%;width:50%;background:#3b82f6;transition:width .1s"></div></div>
        </div>
        <div style="display:flex;gap:8px;align-items:center;margin-bottom:6px">
          <span style="color:#94a3b8;min-width:36px">Roll</span>
          <span id="oRoll" style="color:#fb923c;font-family:monospace;font-weight:700;min-width:52px">0.0&#176;</span>
          <div style="flex:1;height:6px;border-radius:3px;background:#1e293b;overflow:hidden"><div id="oRollBar" style="height:100%;width:50%;background:#fb923c;transition:width .1s"></div></div>
        </div>
        <div style="font-size:10px;color:#64748b;margin-top:4px" id="imuOrientHint">Calibrate IMU first for zeroed values</div>
      </div>
    </div>
    <div style="display:flex;align-items:center;justify-content:space-between;gap:8px;padding:8px;background:#0b1220;border:1px solid #1e293b;border-radius:6px;margin-bottom:8px">
      <div>
        <div style="font-size:12px;font-weight:700">Drive by Tilt</div>
        <div style="font-size:10px;color:#94a3b8">Tilt Anvyxo to trigger IMU moves live. Polls every 800 ms.</div>
      </div>
      <button id="bTiltDrive" style="min-width:76px;background:#334155;color:#cbd5e1;border:none;border-radius:6px;padding:6px 10px;font-size:12px;font-weight:700;cursor:pointer;min-height:38px">OFF</button>
    </div>
    <div style="font-size:10px;font-weight:700;color:#8a9ab0;text-transform:uppercase;letter-spacing:.07em;margin-bottom:6px">IMU-Driven Moves</div>
    <div style="display:grid;grid-template-columns:1fr 1fr;gap:6px">
      <button class="dbtn" id="bImuLean"     style="background:#003020;border-color:#0d4a38;color:#9aefca">&#129318;<br><span style="font-size:9px">Lean Follow</span></button>
      <button class="dbtn" id="bImuBalance"  style="background:#001530;border-color:#0d2e5e;color:#7dd3fc">&#9878;<br><span style="font-size:9px">Balance</span></button>
      <button class="dbtn" id="bImuTiltWalk" style="background:#1a1000;border-color:#3d2a00;color:#fbbf24">&#128694;<br><span style="font-size:9px">Tilt Walk</span></button>
      <button class="dbtn" id="bImuSwagger"  style="background:#1a0030;border-color:#3d005e;color:#c084fc">&#128588;<br><span style="font-size:9px">Swagger</span></button>
      <button class="dbtn" id="bImuSway"     style="background:#001a1a;border-color:#004444;color:#2dd4bf;grid-column:1/-1">&#127775;<br><span style="font-size:9px">Sway (gyro-driven)</span></button>
    </div>
  </div>

  <!-- Movement D-pad -->
  <div class="card">
    <div class="card-title">
      <span>Movement</span>
      <span id="moveOrientBadge" style="font-size:10px;font-family:monospace;color:#64748b;text-transform:none;letter-spacing:0"></span>
    </div>
    <div class="dpad">
      <div class="empty"></div><button id="bFwd" aria-label="Move forward">&#9650;</button><div class="empty"></div>
      <button id="bLft" aria-label="Turn left">&#9664;</button><button class="stop" id="bStop">STOP</button><button id="bRgt" aria-label="Turn right">&#9654;</button>
      <div class="empty"></div><button id="bBwd" aria-label="Move backward">&#9660;</button><div class="empty"></div>
    </div>
  </div>

  <!-- Actions / Dances -->
  <div class="card">
    <div class="card-title">Actions</div>
    <div class="act-row">
      <button class="abtn abtn-dance" id="bDance">&#127939; Auto Dance</button>
      <button class="abtn abtn-stand" id="bStand">&#9654; Stand</button>
    </div>
    <div class="dance-grid">
      <button class="dbtn" id="bD0" style="background:#1a0030">&#128171;<br><span style="font-size:9px">Bounce</span></button>
      <button class="dbtn" id="bD1" style="background:#001a30">&#128246;<br><span style="font-size:9px">Moonwalk</span></button>
      <button class="dbtn" id="bD2" style="background:#1a1a00">&#9889;<br><span style="font-size:9px">Shimmy</span></button>
      <button class="dbtn" id="bD3" style="background:#001a10">&#128260;<br><span style="font-size:9px">Spin/Twist</span></button>
      <button class="dbtn" id="bD4" style="background:#1a0010">&#127465;<br><span style="font-size:9px">Wave Bow</span></button>
      <button class="dbtn" id="bD5" style="background:#3b1d00">&#127881;<br><span style="font-size:9px">Happy Dance</span></button>
      <button class="dbtn" id="bD6" style="background:#001d2b">&#127925;<br><span style="font-size:9px">Groove</span></button>
    </div>
    <div style="margin-top:8px">
      <button class="abtn" id="bRps" style="width:100%;background:#7c3aed;color:#ede9fe;font-size:13px">&#9994; Play Rock Paper Scissors</button>
    </div>
  </div>

  <!-- Saved Moves -->
  <div class="card">
    <div class="card-title">
      <span>&#128302; Saved Moves</span>
      <button id="bMovesRefresh" style="font-size:11px;padding:4px 10px;min-height:28px;background:var(--surface);color:#94a3b8;border:1px solid var(--line);border-radius:6px;cursor:pointer">&#8635; Refresh</button>
    </div>
    <div style="font-size:11px;color:#94a3b8;margin-bottom:8px">Movements you saved in Motion Studio. Tap one to play it (joint moves).</div>
    <div id="savedMoves" class="dance-grid" style="grid-template-columns:1fr 1fr">Loading&#8230;</div>
  </div>

  <!-- Move Library -->
  <div class="card">
    <div class="card-title">&#128139; Move Library</div>
    <div style="font-size:11px;color:#94a3b8;margin-bottom:8px">Every move baked into Anvyxo. Tap to play. <code style="color:var(--mint)">curl /playmove?n=N</code></div>
    <div id="moveLib" class="dance-grid" style="grid-template-columns:1fr 1fr 1fr">Loading&#8230;</div>
  </div>

  <!-- Expression -->
  <div class="card">
    <div class="card-title">&#128512; Expression</div>
    <div class="expr-grid">
      <button class="xbtn xbtn-happy active" id="xBtn0" data-expr="0">&#128512;<span class="xl">Happy</span></button>
      <button class="xbtn xbtn-sad"          id="xBtn1" data-expr="1">&#128542;<span class="xl">Sad</span></button>
      <button class="xbtn xbtn-angry"        id="xBtn2" data-expr="2">&#128545;<span class="xl">Angry</span></button>
      <button class="xbtn xbtn-surprised"    id="xBtn3" data-expr="3">&#128562;<span class="xl">Surprised</span></button>
      <button class="xbtn xbtn-sleepy"       id="xBtn4" data-expr="4">&#128564;<span class="xl">Sleepy</span></button>
      <button class="xbtn xbtn-love"         id="xBtn5" data-expr="5">&#128525;<span class="xl">Love</span></button>
    </div>
    <button class="xbtn" id="xBtn6" data-expr="6" style="width:100%;background:var(--surface);margin-top:6px;font-size:16px;padding:10px 4px;border-radius:10px">
      &#128528;<span class="xl" style="display:inline;margin-left:8px;font-size:11px">Normal (default)</span>
    </button>
    <div id="exprStatus" style="font-size:10px;color:#64748b;text-align:center;margin-top:8px">Active: Normal</div>
  </div>

  <!-- Screen Ticker -->
  <div class="card">
    <div class="card-title">&#128241; Screen Ticker</div>
    <div style="font-size:11px;color:#94a3b8;margin-bottom:8px;line-height:1.6">
      Type a message and tap Show &#8212; it sits on the bottom bar for 5 s. Longer than 26 chars splits into pages that swap every 1.5 s.
    </div>
    <div class="speak-row">
      <input class="speak-input" type="text" id="tickerText" placeholder="e.g. Hello from Anvyxo" maxlength="108">
      <button class="speak-btn" id="bTicker">&#9654; Show</button>
    </div>
    <div id="tickerStatus" style="font-size:11px;color:#64748b;margin-top:7px;min-height:14px"></div>
  </div>

  <!-- Motion Tuning -->
  <div class="card">
    <div class="card-title">Motion Tuning</div>
    <div class="sl-row">
      <div class="sl-top">Step Speed (ms) <span id="vSpd">600</span></div>
      <input type="range" id="sSpd" min="200" max="1200" value="600" step="25">
    </div>
    <div class="sl-row">
      <div class="sl-top">Hip Swing (&#176;) <span id="vHip">25</span></div>
      <input type="range" id="sHip" min="5" max="45" value="25" step="1">
    </div>
    <div class="sl-row">
      <div class="sl-top">Foot Tilt (&#176;) <span id="vFt">20</span></div>
      <input type="range" id="sFt" min="5" max="40" value="20" step="1">
    </div>
  </div>

</div><!-- end tab-move -->

<!-- ══════════════════════════════════════════════════════
     TAB: MOTORS
══════════════════════════════════════════════════════ -->
<div id="tab-motors" class="tab-panel">

  <!-- Motor Control card -->
  <div class="card">
    <div class="card-title">&#9881; Motor Control</div>
    <div style="font-size:11px;color:#94a3b8;margin-bottom:12px;line-height:1.6">
      Move each motor independently. <strong style="color:var(--text)">90&#176; = natural centre</strong> &#8212;
      motors move equal angles clockwise and anti-clockwise from there.<br>
      <strong style="color:#fbbf24">Calibrate &#8594; 90&#176;</strong> sends all four to their true neutral instantly.
    </div>
    <div id="motorSliders"></div>
    <button class="sbtn" id="bServoCal" style="width:100%;background:#0f766e;color:#99f6e4;margin-top:4px;font-size:13px">
      &#9654; Calibrate &#8594; 90&#176; (all motors to natural centre)
    </button>
    <div id="motorCalStatus" style="font-size:11px;color:#64748b;text-align:center;margin-top:6px;min-height:14px"></div>
    <button class="sbtn" id="bServoDefault" style="width:100%;background:#7c2d12;color:#fed7aa;margin-top:8px;font-size:13px">
      &#128190; Save current positions as default (home pose)
    </button>
    <div style="font-size:10px;color:#94a3b8;margin-top:6px;line-height:1.5">
      Stores the four slider positions as Bob's calibrated home / idle pose. Boot holds it, and every gait, dance and IMU move centres on it. Does not change the IMU calibration.
    </div>
    <div id="servoDefaultStatus" style="font-size:11px;color:#64748b;text-align:center;margin-top:4px;min-height:14px"></div>
    <!-- Assembly sub-section -->
    <div style="margin-top:14px;padding-top:10px;border-top:1px solid var(--line)">
      <div style="font-size:10px;font-weight:700;color:#8a9ab0;text-transform:uppercase;letter-spacing:.07em;margin-bottom:7px">Horn Positioning (assembly only)</div>
      <div style="font-size:11px;color:#94a3b8;margin-bottom:8px;line-height:1.6">
        Press <strong style="color:#fbbf24">Hold Raw 90&#176;</strong> before attaching horns &#8212;
        all servos go to exactly 90&#176; with <em>no trim</em>, true mechanical centre.
      </div>
      <div style="display:flex;gap:8px">
        <button class="sbtn" style="flex:2;background:#78350f;color:#fde68a" id="bAssembly">&#9654; Hold Raw 90&#176;</button>
        <button class="sbtn" style="flex:1;background:#1e293b;color:#94a3b8" id="bAssemblyOff">&#9632; Release</button>
      </div>
      <div id="assemblyStatus" style="font-size:11px;color:#64748b;text-align:center;margin-top:6px;min-height:14px"></div>
    </div>
  </div>

  <!-- Servo Trim Calibration card -->
  <div class="card">
    <div class="card-title">Servo Trim Calibration</div>
    <div style="display:grid;grid-template-columns:1fr;gap:14px" id="trimGrid"></div>
    <div class="save-bar">
      <button class="sbtn sbtn-save" id="bSave">&#128190; Save to Flash</button>
      <button class="sbtn sbtn-reset" id="bReset">&#8635; Reset All Trims</button>
    </div>
    <div class="code-box" id="codeBox">int trim[4] = {0, 0, 0, 0};</div>
    <button class="copy-btn" id="bCopy">&#128203; Copy for sketch</button>
  </div>

  <!-- Servo GPIO Pin Map card -->
  <div class="card">
    <div class="card-title">
      <span>&#128268; Servo GPIO Pin Map</span>
      <span id="pinMapSaved" style="font-size:10px;color:#64748b;text-transform:none;letter-spacing:0">loading&#8230;</span>
    </div>
    <div style="font-size:11px;color:#94a3b8;margin-bottom:12px;line-height:1.6">
      Reassign which physical GPIO each joint uses. Only GPIOs <strong style="color:var(--text)">38, 39, 40, 41</strong>
      are safe for servos on this board &#8212; each must be used exactly once.
      <br><strong style="color:#fbbf24">Stop all motion before saving.</strong>
      Changes take effect immediately; no reflash needed.
    </div>
    <div style="display:grid;grid-template-columns:1fr 1fr;gap:10px;margin-bottom:14px" id="pinMapGrid"></div>
    <div id="pinMapConflict" style="font-size:11px;color:#f87171;min-height:14px;margin-bottom:8px;text-align:center"></div>
    <button class="sbtn sbtn-save" id="bPinMapSave" style="width:100%">&#128190; Save Pin Map to Flash</button>
    <div id="pinMapStatus" style="font-size:11px;color:#64748b;margin-top:7px;min-height:14px;text-align:center"></div>
  </div>

</div><!-- end tab-motors -->

<!-- ══════════════════════════════════════════════════════
     TAB: SPEAKER
══════════════════════════════════════════════════════ -->
<div id="tab-speaker" class="tab-panel">

  <!-- Audio card (quick record + SAM TTS) -->
  <div class="card">
    <div class="card-title">&#127908; Audio</div>
    <div class="audio-row">
      <select class="speak-input" id="recSecs" style="flex:0 0 72px">
        <option value="6">6 s</option><option value="3">3 s</option><option value="1">1 s</option>
      </select>
      <button class="rec-btn" id="bRecord">&#9679; Quick Record</button>
      <button class="speak-btn" id="bPlayRobot" title="Replay last recording on Anvyxo's speaker">&#128266; Robot</button>
    </div>
    <div style="font-size:10px;color:#fbbf24;margin-bottom:8px">
      Quick record uses RAM &#8212; 3 s max. For <strong>longer recordings</strong> use Sound Clips below (saved to flash).
    </div>
    <div id="recStatus" style="font-size:11px;color:#64748b;margin-bottom:10px;min-height:14px"></div>
    <audio id="recPlayer" controls style="display:none;width:100%;margin-bottom:10px"></audio>
    <div style="font-size:11px;color:#94a3b8;margin-bottom:7px">Type a sentence and tap &#128266; Speak:</div>
    <div style="display:flex;gap:6px;margin-bottom:7px">
      <select class="speak-input" id="speakMode" style="flex:1">
        <option value="voice">&#128483; Voice (SAM TTS)</option>
        <option value="beep">&#127925; Beeps (old)</option>
      </select>
      <select class="speak-input" id="speakVoice" style="flex:1">
        <option value="0">Sam</option>
        <option value="2">Robot</option>
        <option value="1">Elf</option>
        <option value="3">Stuffy</option>
        <option value="4">Old Lady</option>
        <option value="5">E.T.</option>
      </select>
    </div>
    <div class="sl-row" style="margin-bottom:7px">
      <div class="sl-top">Volume <span id="vVol">100</span></div>
      <input type="range" id="sVol" min="5" max="100" value="100" step="5">
    </div>
    <div class="sl-row" style="margin-bottom:7px">
      <div class="sl-top">Voice Speed (lower = faster) <span id="vVSpeed">72</span></div>
      <input type="range" id="sVSpeed" min="40" max="200" value="72" step="1">
    </div>
    <div class="sl-row" style="margin-bottom:7px">
      <div class="sl-top">Voice Pitch (lower = higher) <span id="vVPitch">64</span></div>
      <input type="range" id="sVPitch" min="30" max="120" value="64" step="1">
    </div>
    <div style="font-size:10px;color:#64748b;margin-bottom:7px">Tip: at 72 / 64 (defaults) each voice keeps its own preset sound.</div>
    <div class="speak-row">
      <input class="speak-input" type="text" id="speakText" placeholder="e.g. Hello I am Anvyxo" maxlength="80">
      <button class="speak-btn" id="bSpeak">&#128266; Speak</button>
    </div>
    <div id="speakStatus" style="font-size:11px;color:#64748b;margin-top:7px;min-height:14px"></div>
  </div>

  <!-- Mac TTS card -->
  <div class="card">
    <div class="card-title">&#127822; Mac TTS &#8212; speak via Mac voice (Hindi / English)</div>
    <div style="font-size:11px;color:#94a3b8;margin-bottom:8px;line-height:1.6">
      Your Mac synthesises the speech using a high-quality neural voice, then sends it to the robot at <strong style="color:var(--text)">full volume</strong>.
      <br>For <strong style="color:#fbbf24">Hindi</strong> pick <em>Lekha</em> or <em>Kanya</em>. For Hinglish pick <em>Rishi</em>.
      Needs <code style="color:#86efac">ffmpeg</code> on the Mac &#8212; or use <code style="color:#86efac">mac_tts.sh</code> directly.
    </div>
    <div style="display:grid;grid-template-columns:1fr 1fr;gap:6px;margin-bottom:6px">
      <select class="speak-input" id="macVoice">
        <option value="Samantha">Samantha (EN, natural)</option>
        <option value="Rishi">Rishi (Indian EN)</option>
        <option value="Karen">Karen (AU English)</option>
        <option value="Daniel">Daniel (British)</option>
        <option value="Lekha">Lekha (Hindi &#2361;&#2367;&#2306;&#2342;&#2368;)</option>
        <option value="Kanya">Kanya (Hindi newer)</option>
      </select>
      <select class="speak-input" id="macRate">
        <option value="22050">Quality: 22 kHz</option>
        <option value="16000">Quality: 16 kHz</option>
        <option value="11025">Quality: 11 kHz</option>
      </select>
    </div>
    <div class="speak-row" style="margin-bottom:6px">
      <input class="speak-input" type="text" id="macTtsText" placeholder="e.g. &#2344;&#2350;&#2360;&#2381;&#2340;&#2375; &#2350;&#2376;&#2306; &#2348;&#2377;&#2348; &#2361;&#2370;&#2305;" maxlength="200">
      <button class="speak-btn" id="bMacTts">&#128266; Speak</button>
    </div>
    <div id="macTtsStatus" style="font-size:11px;color:#64748b;min-height:14px"></div>
  </div>

  <!-- Audio Diagnostics card -->
  <div class="card">
    <div class="card-title">&#128269; Audio Diagnostics</div>
    <div style="display:flex;gap:8px;margin-bottom:10px">
      <button class="sbtn" style="background:#1e3a5f;color:#7dd3fc" id="bTone">&#128266; Speaker Test Tone</button>
      <button class="sbtn" style="background:#2d2008;color:#fbbf24" id="bMicMeter">&#127897; Mic Meter: OFF</button>
    </div>
    <button class="sbtn" style="width:100%;background:#14532d;color:#bbf7d0;margin-bottom:10px" id="bMicCal">&#127925; Mic Calibrate + Download</button>
    <div style="font-size:10px;color:#64748b;margin-bottom:10px">Plays a short reference tone from this phone/computer speaker while Anvyxo records it. Turn your device volume up.</div>
    <div style="background:#0a1118;border:1px solid var(--line);border-radius:6px;padding:8px;margin-bottom:10px">
      <div style="display:flex;justify-content:space-between;font-size:10px;color:#64748b;margin-bottom:4px">
        <span>MIC LEVEL (talk / clap to test)</span><span id="micVal">&#8212;</span>
      </div>
      <div style="height:14px;background:#1e293b;border-radius:7px;overflow:hidden">
        <div id="micBar" style="height:100%;width:0%;background:#22c55e;transition:width .2s"></div>
      </div>
      <div id="micVerdict" style="font-size:10px;color:#64748b;margin-top:5px;min-height:12px"></div>
    </div>
    <div style="font-size:10px;font-weight:700;color:#8a9ab0;text-transform:uppercase;letter-spacing:.07em;margin:12px 0 8px">Mic Fix &#8212; try other I2S configs</div>
    <button class="sbtn" style="width:100%;background:#581c87;color:#e9d5ff;margin-bottom:8px" id="bMicScan">&#9881; Auto-Scan All 8 Mic Configs (make noise!)</button>
    <div style="display:grid;grid-template-columns:1fr 1fr;gap:6px;margin-bottom:8px">
      <select class="speak-input" id="mCh"><option value="0">Channel: LEFT</option><option value="1">Channel: RIGHT</option></select>
      <select class="speak-input" id="mFmt"><option value="0">Format: I2S</option><option value="1">Format: MSB</option></select>
      <select class="speak-input" id="mFix"><option value="1">SPH fix: ON</option><option value="0">SPH fix: OFF</option></select>
      <select class="speak-input" id="mSh"><option value="14">Shift: 14 (SPH0645)</option><option value="16">Shift: 16 (INMP441)</option><option value="11">Shift: 11 (louder)</option></select>
    </div>
    <button class="sbtn" style="width:100%;background:#0f766e;color:#99f6e4;margin-bottom:10px" id="bMicApply">Apply Mic Config</button>
    <button class="sbtn" style="width:100%;background:#166534;color:#bbf7d0" id="bDiag">&#129514; Run Full Diagnostic (plays 1 s tone)</button>
    <pre id="diagOut" style="display:none;background:#0a1118;border:1px solid #1e3a5f;border-radius:6px;padding:9px;font-size:10px;color:#86efac;white-space:pre-wrap;word-break:break-all;margin-top:10px;max-height:260px;overflow-y:auto"></pre>
  </div>

  <!-- Sound Clips card -->
  <div class="card">
    <div class="card-title">&#127925; Sound Clips &#8212; long recording &amp; your own music</div>

    <div style="font-size:10px;font-weight:700;color:#8a9ab0;text-transform:uppercase;letter-spacing:.07em;margin-bottom:6px">
      Record to flash &#8212; any length up to <span id="recCap" style="color:#7dd3fc">&#8230;</span>
    </div>
    <div style="display:grid;grid-template-columns:repeat(5,1fr);gap:5px;margin-bottom:6px">
      <button class="dbtn" data-secs="5">5 s</button>
      <button class="dbtn" data-secs="10">10 s</button>
      <button class="dbtn" data-secs="20">20 s</button>
      <button class="dbtn" data-secs="30">30 s</button>
      <button class="dbtn" data-secs="60">60 s</button>
    </div>
    <div style="display:flex;gap:6px;margin-bottom:6px">
      <input class="speak-input" type="number" id="recFileSecs" value="10" min="1" max="120" style="flex:0 0 76px">
      <input class="speak-input" type="text" id="recFileName" value="rec.wav" placeholder="name.wav" style="flex:1">
      <button class="speak-btn" id="bRecFile">&#9679; Record</button>
    </div>
    <div id="recFileStatus" style="font-size:11px;color:#64748b;margin-bottom:12px;min-height:14px">Recording length is limited by free flash, not RAM.</div>

    <div style="font-size:10px;font-weight:700;color:#8a9ab0;text-transform:uppercase;letter-spacing:.07em;margin-bottom:6px">&#127911; Add music / MP3</div>
    <div style="font-size:11px;color:#94a3b8;margin-bottom:7px;line-height:1.6">
      Pick any <strong>MP3, M4A, OGG or WAV</strong>. Your browser decodes it and sends plain audio to Anvyxo.
      <br><strong style="color:#99f6e4">Play Now</strong> = streams straight to the speaker, no storage used.
      <br><strong style="color:#7dd3fc">Save to Robot</strong> = stored in flash, plays later on its own.
    </div>
    <input class="speak-input" type="file" id="clipFile" accept="audio/*,.mp3,.wav,.m4a,.ogg" style="width:100%;margin-bottom:6px">
    <select class="speak-input" id="clipRate" style="width:100%;margin-bottom:6px">
      <option value="16000">Quality: 16 kHz (32 KB/s)</option>
      <option value="22050">Quality: 22 kHz (44 KB/s)</option>
      <option value="8000">Quality: 8 kHz (16 KB/s, longest)</option>
    </select>
    <div style="display:flex;gap:6px;margin-bottom:6px">
      <button class="sbtn" style="background:#0f766e;color:#99f6e4" id="bClipStream">&#9654; Play Now (no storage)</button>
      <button class="sbtn" style="background:#1e3a5f;color:#7dd3fc" id="bClipUpload">&#128190; Save to Robot</button>
    </div>
    <div id="clipUpStatus" style="font-size:11px;color:#64748b;margin-bottom:12px;min-height:14px"></div>

    <div style="font-size:10px;font-weight:700;color:#8a9ab0;text-transform:uppercase;letter-spacing:.07em;margin-bottom:6px">Stored clips</div>
    <div style="background:#0a1118;border:1px solid var(--line);border-radius:6px;padding:8px;margin-bottom:8px">
      <div style="display:flex;justify-content:space-between;font-size:10px;color:#64748b;margin-bottom:4px">
        <span>FLASH STORAGE</span><span id="fsVal">&#8212;</span>
      </div>
      <div style="height:10px;background:#1e293b;border-radius:5px;overflow:hidden">
        <div id="fsBar" style="height:100%;width:0%;background:#22c55e;transition:width .3s"></div>
      </div>
    </div>
    <div id="clipList" style="font-size:11px;color:#64748b">Loading&#8230;</div>
    <button class="copy-btn" id="bClipRefresh">&#8635; Refresh clip list</button>
  </div>

</div><!-- end tab-speaker -->

<!-- ══════════════════════════════════════════════════════
     TAB: IMU
══════════════════════════════════════════════════════ -->
<div id="tab-imu" class="tab-panel">

  <!-- IMU calibration banner (IMU tab) -->
  <div id="calBannerImu" class="cal-banner">
    &#9888; IMU not calibrated this boot &#8212; place Anvyxo in his standing pose, then
    <button id="bBannerCalImu" style="margin-left:6px;background:#166534;color:#bbf7d0;border:none;border-radius:6px;padding:6px 12px;font-weight:700;cursor:pointer;min-height:34px">Calibrate Now</button>
  </div>

  <div class="card"><div class="card-title">Liquid eyes / screen demo</div>
    <p style="color:#94a3b8;font-size:12px;margin-bottom:12px">A scripted tilt left, tilt right, shake and angry comeback. Screen only; no motor commands or live IMU control.</p>
    <button class="sbtn" id="eyeDemoStart">Play liquid eyes</button>
    <button class="sbtn" id="eyeDemoStop">Stop eyes demo</button>
    <span id="eyeDemoStatus" role="status" style="font-size:11px;color:#94a3b8"></span>
  </div>
  <!-- Main IMU card -->
  <div class="card">
    <div class="card-title">&#127760; IMU &#8212; MPU-6500/9250 Tilt &amp; Lift Detect</div>
    <div id="imuOffline" style="font-size:11px;color:#f87171;margin-bottom:8px;display:none">
      &#10005; IMU not answering &#8212; check SDA&#8594;GPIO46, SCL&#8594;GPIO0, VCC&#8594;3.3V, GND, AD0&#8594;GND.<br>
      The robot keeps probing for it; everything else still works.
    </div>

    <!-- 3D cube + labels -->
    <div style="display:flex;gap:14px;align-items:center;margin-bottom:12px">
      <div class="imu-stage"><div class="imu-camera" id="imuCamera"><div id="imuCube">
        <div class="cube-face" style="transform:translateZ(48px);border:2px solid #76f6e7;background:linear-gradient(135deg,#174a57,#102839);color:#8affef"><div class="cube-eyes"><i></i><i></i></div>FRONT</div>
        <div class="cube-face" style="transform:rotateY(180deg) translateZ(48px);border:2px solid #ffc27a;background:linear-gradient(135deg,#503720,#261e25);color:#ffcf94"><div class="cube-vents"></div>BACK</div>
        <div class="cube-face" style="transform:rotateY(90deg) translateZ(48px)">RIGHT</div>
        <div class="cube-face" style="transform:rotateY(-90deg) translateZ(48px)">LEFT</div>
        <div class="cube-face" style="transform:rotateX(90deg) translateZ(48px)">ANVYXO<br>&#8595; FRONT</div>
        <div class="cube-face" style="transform:rotateX(-90deg) translateZ(48px)">BASE</div>
      </div></div></div>
      <div style="flex:1;font-size:10px">
        <div style="color:#8aa2b9;margin-bottom:8px">Smoothed pitch / roll (no yaw)<br><span style="color:#8affef">Cyan: FRONT</span> / <span style="color:#ffcf94">Amber: BACK</span></div>
        <label>Smoothing <select id="imuSmooth"><option value="0.20" selected>Balanced</option><option value="0.32">Cinematic</option><option value="0.10">Responsive</option></select></label>
        <div style="display:flex;gap:6px;flex-wrap:wrap;margin:10px 0"><button class="sbtn" id="imuFlipPitch" aria-pressed="false">Reverse pitch</button><button class="sbtn" id="imuFlipRoll" aria-pressed="false">Reverse roll</button><button class="sbtn" id="imuViewBack">View back</button><button class="sbtn" id="imuFocus">Recording view</button></div>
        <div style="display:flex;gap:8px;margin-bottom:3px">
          <span style="color:#94a3b8">Pitch</span>
          <span id="imuPitch" style="color:#7dd3fc;font-family:monospace;font-weight:700">0.0&#176;</span>
          <span style="color:#94a3b8">Roll</span>
          <span id="imuRoll" style="color:#fb923c;font-family:monospace;font-weight:700">0.0&#176;</span>
        </div>
        <div style="color:#64748b;margin-bottom:4px;margin-top:6px">Motion delta vs rest</div>
        <div style="height:10px;background:#1e293b;border-radius:5px;overflow:hidden;margin-bottom:3px">
          <div id="imuDeltaBar" style="height:100%;width:0%;background:#22c55e;transition:width .1s"></div>
        </div>
        <div id="imuDeltaVal" style="color:#94a3b8;font-family:monospace">delta=0 thresh=240</div>
      </div>
    </div>

    <!-- Lift banner -->
    <div id="imuLiftBanner" style="display:none;background:#ef4444;color:#fff;border-radius:8px;padding:7px 12px;text-align:center;font-size:13px;font-weight:700;margin-bottom:10px;animation:pulse .5s infinite alternate">
      &#9888; SCARED! &#8212; 5 foot flips, then it re-learns its resting pose
    </div>

    <!-- Calibration section -->
    <div style="background:#0f1e10;border:1px solid #166534;border-radius:8px;padding:10px;margin-bottom:10px">
      <div style="font-size:10px;font-weight:700;color:#22c55e;text-transform:uppercase;letter-spacing:.07em;margin-bottom:8px">&#127919; Lift Calibration</div>
      <div style="display:flex;align-items:center;justify-content:space-between;gap:8px;margin-bottom:10px;padding:8px;background:#0b1220;border:1px solid #1e293b;border-radius:6px">
        <div>
          <div style="font-size:12px;font-weight:700">Pick-up / shake reaction</div>
          <div style="font-size:10px;color:#94a3b8">On &#8594; auto-calibrates now, then gets scared &amp; kicks when lifted</div>
        </div>
        <button class="sbtn" id="bLiftToggle" style="min-width:76px;background:#334155;color:#cbd5e1">&#8230;</button>
      </div>
      <div style="display:flex;align-items:center;justify-content:space-between;gap:8px;margin-bottom:10px;padding:8px;background:#0b1220;border:1px solid #1e293b;border-radius:6px">
        <div>
          <div style="font-size:12px;font-weight:700">Help me sound</div>
          <div style="font-size:10px;color:#94a3b8">On &#8594; plays HelpmeSound.wav at full volume 2 s after the scared face</div>
        </div>
        <button class="sbtn" id="bHelpMeToggle" style="min-width:76px;background:#334155;color:#cbd5e1">&#8230;</button>
      </div>
      <div style="font-size:11px;color:#94a3b8;margin-bottom:8px;line-height:1.6">
        Place the robot in its <strong style="color:var(--text)">normal standing position</strong> and tap
        <strong style="color:#fbbf24">Calibrate Now</strong>. The robot samples ~3 s of readings to learn what
        &ldquo;at rest&rdquo; feels like &#8212; then <strong style="color:var(--text)">zeroes every axis against it</strong>.
        After every scare it <strong style="color:var(--text)">re-learns its resting pose automatically</strong>.
      </div>
      <div style="display:flex;gap:8px;margin-bottom:8px">
        <button class="sbtn" style="flex:2;background:#166534;color:#bbf7d0" id="bImuCal">&#9654; Calibrate Now</button>
        <div id="imuCalBadge" style="flex:1;border-radius:6px;padding:6px 8px;font-size:10px;font-weight:700;text-align:center;background:#1e293b;color:#475569">NOT CAL</div>
      </div>
      <button class="sbtn" style="width:100%;background:#7c2d12;color:#fed7aa;margin-bottom:8px" id="bSavePose">&#128190; Save Calibrated Position</button>
      <div style="font-size:10px;color:#94a3b8;margin-bottom:8px;line-height:1.6">
        Tap this to store the current servo positions as home / idle pose AND calibrate the IMU in one step.
        Boot holds this pose and every gait, dance and IMU move now centres on it.
      </div>
      <div id="imuCalStatus" style="font-size:10px;color:#64748b;margin-bottom:8px;min-height:12px"></div>
      <div id="imuCalDetail" style="display:none;font-size:10px;color:#64748b;font-family:monospace;margin-bottom:8px"></div>
      <div class="sl-row">
        <div class="sl-top" style="font-size:11px">Lift sensitivity (delta threshold) <span id="vImuThresh">240</span></div>
        <input type="range" id="sImuThresh" min="60" max="3000" value="240" step="30">
      </div>
      <div style="font-size:9px;color:#475569;margin-top:3px">Lower = more sensitive. Higher = requires bigger motion. 1 g &#8776; 981 display units.</div>
    </div>

    <!-- Raw accel + gyro panels -->
    <div style="display:grid;grid-template-columns:1fr 1fr;gap:6px;font-size:11px;margin-bottom:8px">
      <div style="background:#0a1118;border:1px solid #1e3a5f;border-radius:6px;padding:8px">
        <div style="color:#64748b;font-size:9px;text-transform:uppercase;letter-spacing:.06em;margin-bottom:5px">Accelerometer (scaled units)</div>
        <div style="display:grid;grid-template-columns:20px 1fr;gap:3px;align-items:center">
          <span style="color:#3b82f6;font-weight:700">X</span><div style="height:8px;border-radius:4px;background:#1e293b;overflow:hidden"><div id="imuAxFill" style="height:100%;width:50%;background:#3b82f6;transition:width .1s"></div></div>
          <span style="color:#22c55e;font-weight:700">Y</span><div style="height:8px;border-radius:4px;background:#1e293b;overflow:hidden"><div id="imuAyFill" style="height:100%;width:50%;background:#22c55e;transition:width .1s"></div></div>
          <span style="color:#f59e0b;font-weight:700">Z</span><div style="height:8px;border-radius:4px;background:#1e293b;overflow:hidden"><div id="imuAzFill" style="height:100%;width:50%;background:#f59e0b;transition:width .1s"></div></div>
        </div>
        <div id="imuAVals" style="color:#94a3b8;margin-top:5px;font-family:monospace;font-size:10px">ax=0 ay=0 az=0</div>
      </div>
      <div style="background:#0a1118;border:1px solid #1e3a5f;border-radius:6px;padding:8px">
        <div style="color:#64748b;font-size:9px;text-transform:uppercase;letter-spacing:.06em;margin-bottom:5px">Gyroscope (raw LSB)</div>
        <div style="display:grid;grid-template-columns:20px 1fr;gap:3px;align-items:center">
          <span style="color:#e879f9;font-weight:700">X</span><div style="height:8px;border-radius:4px;background:#1e293b;overflow:hidden"><div id="imuGxFill" style="height:100%;width:50%;background:#e879f9;transition:width .1s"></div></div>
          <span style="color:#fb923c;font-weight:700">Y</span><div style="height:8px;border-radius:4px;background:#1e293b;overflow:hidden"><div id="imuGyFill" style="height:100%;width:50%;background:#fb923c;transition:width .1s"></div></div>
          <span style="color:#38bdf8;font-weight:700">Z</span><div style="height:8px;border-radius:4px;background:#1e293b;overflow:hidden"><div id="imuGzFill" style="height:100%;width:50%;background:#38bdf8;transition:width .1s"></div></div>
        </div>
        <div id="imuGVals" style="color:#94a3b8;margin-top:5px;font-family:monospace;font-size:10px">gx=0 gy=0 gz=0</div>
      </div>
    </div>

    <div style="display:flex;justify-content:space-between;align-items:center">
      <div id="imuStatus" style="font-size:10px;color:#64748b">Polling starts when this tab is opened</div>
      <button class="sbtn" style="background:#1e3a5f;color:#7dd3fc;padding:6px 10px;font-size:11px" id="bImuToggle">&#9646;&#9646; Pause</button>
    </div>
  </div>

</div><!-- end tab-imu -->

<!-- ══════════════════════════════════════════════════════
     TAB: CAMERA
══════════════════════════════════════════════════════ -->
<div id="tab-camera" class="tab-panel">

  <div class="card">
    <div class="card-title">
      <span>&#128247; Camera</span>
      <span id="camState" style="text-transform:none;letter-spacing:0;color:#64748b">&#8230;</span>
    </div>
    <div id="camBox" style="background:radial-gradient(ellipse,#1c2d39,#0c141e);border:1px solid var(--line);border-radius:12px;aspect-ratio:4/3;display:flex;align-items:center;justify-content:center;overflow:hidden;text-align:center;padding:0">
      <span id="camMsg" style="color:#9aafbd;font-size:13px">Camera off</span>
      <img id="camImg" alt="camera" style="display:none;width:100%;height:100%;object-fit:contain">
    </div>
    <div class="save-bar">
      <button class="sbtn sbtn-save" id="bCam">&#9654; Start camera</button>
      <button class="sbtn" style="background:#1e3a5f;color:#7dd3fc" id="bSnap">&#128248; Snapshot</button>
    </div>
  </div>

</div><!-- end tab-camera -->

<!-- ══════════════════════════════════════════════════════
     TAB: SYSTEM
══════════════════════════════════════════════════════ -->
<div id="tab-system" class="tab-panel">

  <!-- Hardware Status card -->
  <div class="card">
    <div class="card-title">
      <span>&#129520; Hardware Status</span>
      <span id="hwStep" style="text-transform:none;letter-spacing:0;color:#64748b;font-size:10px"></span>
    </div>
    <div id="hwList" style="display:grid;gap:5px;font-size:12px;overflow-wrap:anywhere">Checking&#8230;</div>
    <div style="font-size:10px;color:#64748b;margin-top:9px;line-height:1.5">
      <span style="color:#22c55e">&#9679;</span> working &nbsp;
      <span style="color:#ef4444">&#9679;</span> not found / failed &nbsp;
      <span style="color:#64748b">&#9679;</span> can&#39;t be checked &nbsp;
      <span style="color:#eab308">&#9679;</span> starting
    </div>
  </div>

  <!-- WiFi Settings card -->
  <div class="card">
    <div class="card-title">&#128246; WiFi Settings</div>
    <div id="wifiStatusBar" style="font-size:11px;margin-bottom:10px;padding:7px 10px;border-radius:6px;background:#0a1118;border:1px solid var(--line);color:#94a3b8">
      Loading&#8230;
    </div>
    <div style="font-size:11px;color:#94a3b8;margin-bottom:10px;line-height:1.6">
      The robot always broadcasts <strong style="color:var(--text)">Anvyxo_robo</strong> /
      <strong style="color:var(--text)">bob12345</strong> so you can always reach it at
      <strong style="color:#86efac">192.168.4.1</strong>.<br>
      Enter your home WiFi below &#8212; the robot will join it and show the new IP on screen.
      <strong style="color:#fbbf24">2.4 GHz only</strong> &#8212; ESP32 cannot see 5 GHz networks.
    </div>
    <div style="display:flex;flex-direction:column;gap:7px;margin-bottom:8px">
      <input class="speak-input" type="text"     id="wifiSsid" placeholder="Home WiFi name (SSID)" maxlength="32" autocomplete="off" autocorrect="off" autocapitalize="none" spellcheck="false">
      <input class="speak-input" type="password" id="wifiPass" placeholder="Password (leave blank if open)" maxlength="63" autocomplete="new-password">
    </div>
    <button class="sbtn sbtn-save" style="width:100%" id="bWifiSave">&#128246; Save &amp; Connect</button>
    <div id="wifiSaveStatus" style="font-size:11px;color:#64748b;margin-top:7px;min-height:14px"></div>
  </div>

  <!-- Device Log card -->
  <div class="card">
    <div class="card-title">&#128203; Device Log &#8212; restart debugging</div>
    <div style="font-size:11px;color:#94a3b8;margin-bottom:8px;line-height:1.6">
      This log lives in RTC memory and <strong style="color:#fbbf24">survives restarts</strong>.
      After a reboot: reconnect to Anvyxo_robo WiFi, tap Refresh. The lines <em>above</em> the last &ldquo;===== BOOT&rdquo; marker show what the robot was doing when it died.
    </div>
    <div style="display:flex;gap:6px;margin-bottom:8px;flex-wrap:wrap">
      <button class="sbtn" style="background:#1e3a5f;color:#7dd3fc" id="bLogRefresh">&#8635; Refresh</button>
      <button class="sbtn" style="background:#2d2008;color:#fbbf24" id="bLogAuto">Auto: OFF</button>
      <button class="sbtn" style="background:#166534;color:#bbf7d0" id="bLogCopy">&#128203; Copy</button>
      <button class="sbtn" style="background:#450a0a;color:#fca5a5" id="bLogClear">Clear</button>
    </div>
    <pre id="devLog" style="background:#0a1118;border:1px solid #1e3a5f;border-radius:6px;padding:9px;font-size:10px;color:#86efac;white-space:pre-wrap;word-break:break-all;max-height:280px;overflow-y:auto;margin:0;line-height:1.7">Tap Refresh to load the device log.</pre>
  </div>

  <!-- UI Log card -->
  <div class="card">
    <div class="log-hdr"><span>UI Log</span><button id="bClearLog">clear</button></div>
    <div id="log"></div>
  </div>

</div><!-- end tab-system -->

<div id="toast"></div>

<script>
/* ─────────────────────────────────────────────────────────
   GLOBALS
───────────────────────────────────────────────────────── */
var trims=[0,0,0,0];
var CH=['Left Hip','Left Foot','Right Hip','Right Foot'];
var EXPR_NAMES=['Happy','Sad','Angry','Surprised','Sleepy','Love','Normal'];
var curExpr=6;
var samOK=true;
var imuStarted=false;  /* lazy-init: IMU poll only starts on first IMU tab visit */
var currentTab='move'; /* which tab is showing — IMU pollers pause when off-tab */

/* ─────────────────────────────────────────────────────────
   TAB SWITCHING
───────────────────────────────────────────────────────── */
function showTab(name){
  document.querySelectorAll('.tab-panel').forEach(function(p){p.classList.remove('active');});
  document.querySelectorAll('.tab-btn').forEach(function(b){b.classList.remove('active');});
  var panel=document.getElementById('tab-'+name);
  if(panel) panel.classList.add('active');
  document.querySelectorAll('.tab-btn').forEach(function(b){
    if(b.getAttribute('data-tab')===name) b.classList.add('active');
  });
  currentTab=name;
  localStorage.setItem('activeTab',name);
  /* Lazy-init IMU polling the first time the IMU tab is opened */
  if(name==='imu'&&!imuStarted){
    imuStarted=true;
    startImuPoll();
  }
}

document.querySelectorAll('.tab-btn').forEach(function(b){
  b.addEventListener('click',function(){ showTab(b.getAttribute('data-tab')); });
});

/* Restore last tab on page load */
(function(){
  var saved=localStorage.getItem('activeTab')||'move';
  showTab(saved);
})();

/* Dismiss the ANVYXO splash once the UI is wired up (min 500ms so it reads) */
(function(){
  var sp=document.getElementById('splash');
  if(!sp)return;
  function hide(){sp.classList.add('hide');setTimeout(function(){sp.remove();},500);}
  var t0=performance.now();
  var ready=function(){setTimeout(hide,Math.max(0,500-(performance.now()-t0)));};
  if(document.readyState==='complete')ready();
  else window.addEventListener('load',ready);
})();

/* ─────────────────────────────────────────────────────────
   UTILITIES
───────────────────────────────────────────────────────── */
function toast(m){
  var e=document.getElementById('toast');
  e.textContent=m;e.style.opacity=1;
  clearTimeout(e._t);e._t=setTimeout(function(){e.style.opacity=0;},1700);
}

function log(type,m){
  var b=document.getElementById('log');
  var cls={ok:'lk',warn:'lw',error:'le',head:'lh',info:'lo'}[type]||'lo';
  var pre={ok:'✓ ',warn:'⚠ ',error:'✗ ',head:'── ',info:'   '}[type]||'   ';
  var ts=new Date().toLocaleTimeString('en',{hour12:false,hour:'2-digit',minute:'2-digit',second:'2-digit'});
  var d=document.createElement('div');d.className=cls;d.textContent='['+ts+'] '+pre+m;
  b.appendChild(d);b.scrollTop=b.scrollHeight;
}

function get(url,ok,fail){
  fetch(url).then(function(r){return r.text();}).then(function(t){if(ok)ok(t);}).catch(function(){if(fail)fail();});
}

/* ─────────────────────────────────────────────────────────
   MOVEMENT
───────────────────────────────────────────────────────── */
function move(cmd){get('/move?cmd='+cmd,function(){log('info',cmd);},function(){log('error',cmd+' failed');});}
document.getElementById('bFwd').addEventListener('click',function(){move('fwd');toast('Forward');});
document.getElementById('bBwd').addEventListener('click',function(){move('bwd');toast('Backward');});
document.getElementById('bLft').addEventListener('click',function(){move('left');toast('Turn left');});
document.getElementById('bRgt').addEventListener('click',function(){move('right');toast('Turn right');});
document.getElementById('bStop').addEventListener('click',function(){move('stand');toast('Stopped');});
document.getElementById('bDance').addEventListener('click',function(){move('dance');toast('Auto Dance!');});
document.getElementById('bStand').addEventListener('click',function(){move('stand');toast('Standing');});

var DANCE_NAMES=['Bounce','Moonwalk','Shimmy','Spin/Twist','Wave Bow','Happy Dance','Groove'];
[0,1,2,3,4,5,6].forEach(function(i){
  document.getElementById('bD'+i).addEventListener('click',function(){
    move('dance'+i);toast(DANCE_NAMES[i]+' !');
  });
});

document.getElementById('bRps').addEventListener('click',function(){
  var btn=this; btn.disabled=true; btn.textContent='Playing\u2026';
  toast('Rock Paper Scissors! 1\u20262\u20263\u2026');
  get('/rps',
    function(){ btn.disabled=false; btn.innerHTML='&#9994; Play Rock Paper Scissors'; toast('Done!'); },
    function(){ btn.disabled=false; btn.innerHTML='&#9994; Play Rock Paper Scissors'; }
  );
});

/* Saved Moves gallery */
function loadSavedMoves(){
  var box=document.getElementById('savedMoves');
  fetch('/motion/library').then(function(r){return r.json();}).then(function(d){
    var slots=(d.slots||[]).filter(function(s){return s.sequence;});
    if(!slots.length){box.textContent='No saved moves yet. Save some in Motion Studio.';return;}
    box.replaceChildren();
    slots.forEach(function(s){
      var seq=s.sequence, tilt=(seq.mode==='tilt');
      var b=document.createElement('button');
      b.className='dbtn';
      b.style.cssText='min-height:48px;background:#12241a;border-color:#2a5a3a;color:#bbf7d0';
      b.innerHTML='&#9654; '+(seq.name||('Slot '+(s.slot+1)))+'<br><span style="font-size:9px;color:#7d9c88">slot '+(s.slot+1)+(tilt?' · tilt':'')+'</span>';
      if(tilt) b.style.opacity='0.5';
      b.addEventListener('click',function(){
        if(tilt){toast('Posture move — open Motion Studio');return;}
        get('/playslot?n='+s.slot,function(){log('info','play '+seq.name);toast(seq.name+' !');},function(){log('error','play '+seq.name+' failed');});
      });
      box.append(b);
    });
  }).catch(function(){box.textContent='Could not load saved moves.';});
}
document.getElementById('bMovesRefresh').addEventListener('click',loadSavedMoves);
loadSavedMoves();

/* Move Library gallery */
function loadMoveLib(){
  var box=document.getElementById('moveLib');
  fetch('/movelist').then(function(r){return r.json();}).then(function(list){
    if(!list.length){box.textContent='No moves.';return;}
    box.replaceChildren();
    list.forEach(function(m){
      var b=document.createElement('button');
      b.className='dbtn';
      b.style.cssText='min-height:44px;font-size:11px;background:#181528;border-color:#3a2a5a;color:#d6c8f7';
      b.textContent=m.name;
      b.addEventListener('click',function(){
        get('/playmove?n='+m.n,function(){log('info','play '+m.name);toast(m.name+' !');},function(){log('error','play '+m.name+' failed');});
      });
      box.append(b);
    });
  }).catch(function(){box.textContent='Could not load move library.';});
}
loadMoveLib();

/* ─────────────────────────────────────────────────────────
   EXPRESSION
───────────────────────────────────────────────────────── */
(function(){
  var btns=[];
  for(var i=0;i<7;i++) btns.push(document.getElementById('xBtn'+i));
  function setExpr(e){
    curExpr=e;
    btns.forEach(function(b,i){b.classList.toggle('active',i===e);});
    document.getElementById('exprStatus').textContent='Active: '+EXPR_NAMES[e];
    get('/eyes?m='+e,
      function(){log('ok','Expression \u2192 '+EXPR_NAMES[e]);toast(EXPR_NAMES[e]+'!');},
      function(){log('error','Expression change failed');});
  }
  btns.forEach(function(b,i){b.addEventListener('click',function(){setExpr(i);});});
})();

/* ─────────────────────────────────────────────────────────
   SCREEN TICKER
───────────────────────────────────────────────────────── */
(function(){
  var btn=document.getElementById('bTicker');
  var inp=document.getElementById('tickerText');
  var st=document.getElementById('tickerStatus');
  function send(){
    var t=inp.value.trim();
    if(!t){inp.focus();return;}
    btn.disabled=true;
    get('/ticker?text='+encodeURIComponent(t),
      function(){
        var pages=Math.ceil(t.length/26);
        st.textContent=pages>1?'Showing "'+t+'" over '+pages+' pages.':'Showing "'+t+'" for 5 s.';
        st.style.color='#4ade80';
        log('ok','Ticker: "'+t+'"');toast('On screen!');btn.disabled=false;
      },
      function(){
        st.textContent='Could not reach the robot.';st.style.color='#f87171';
        log('error','Ticker send failed');btn.disabled=false;
      });
  }
  btn.addEventListener('click',send);
  inp.addEventListener('keydown',function(e){if(e.key==='Enter')send();});
})();

/* ─────────────────────────────────────────────────────────
   MOTION TUNING SLIDERS
───────────────────────────────────────────────────────── */
function bindSlider(id,valId,param){
  var s=document.getElementById(id),v=document.getElementById(valId);
  s.addEventListener('input',function(){v.textContent=s.value;});
  s.addEventListener('change',function(){get('/param?'+param+'='+s.value,function(){log('info',param+'='+s.value);},null);toast(param+'='+s.value);});
}
bindSlider('sSpd','vSpd','speed');
bindSlider('sHip','vHip','swing');
bindSlider('sFt','vFt','tilt');

/* ─────────────────────────────────────────────────────────
   STATE LOAD ON PAGE OPEN
───────────────────────────────────────────────────────── */
fetch('/state').then(function(r){return r.json();}).then(function(d){
  trims=d.trim;refreshTrimUI();
  document.getElementById('sSpd').value=d.speed;document.getElementById('vSpd').textContent=d.speed;
  document.getElementById('sHip').value=d.swing;document.getElementById('vHip').textContent=d.swing;
  document.getElementById('sFt').value=d.tilt; document.getElementById('vFt').textContent=d.tilt;
  if(d.eye!==undefined){
    curExpr=d.eye;
    for(var i=0;i<7;i++) document.getElementById('xBtn'+i).classList.toggle('active',i===d.eye);
    document.getElementById('exprStatus').textContent='Active: '+EXPR_NAMES[Math.min(d.eye,6)];
  }
  if(d.mch!==undefined){
    document.getElementById('mCh').value=d.mch;
    document.getElementById('mFmt').value=d.mfmt;
    document.getElementById('mFix').value=d.mfix;
    document.getElementById('mSh').value=d.msh;
  }
  if(d.sam===0){
    samOK=false;
    document.getElementById('speakMode').value='beep';
    log('warn','Voice TTS not compiled in — install ESP8266Audio + ESP8266SAM, set USE_SAM_TTS 1');
  }
  log('head','Connected — trims loaded from flash');
  log('info','LH='+d.trim[0]+' LF='+d.trim[1]+' RH='+d.trim[2]+' RF='+d.trim[3]);
}).catch(function(){log('warn','Could not load state');});

/* ─────────────────────────────────────────────────────────
   TRIM CALIBRATION
───────────────────────────────────────────────────────── */
function buildTrim(){
  var g=document.getElementById('trimGrid');g.innerHTML='';
  for(var i=0;i<4;i++){
    (function(ch){
      var d=document.createElement('div');d.className='trim-item';
      d.innerHTML='<div class="tlabel">CH'+ch+' \u2014 '+CH[ch]+'</div>'+
        '<div class="trim-ctrl">'+
          '<button class="big" data-ch="'+ch+'" data-d="-5">-5</button>'+
          '<button data-ch="'+ch+'" data-d="-1">-</button>'+
          '<div class="trim-val" id="tv'+ch+'">0\u00B0</div>'+
          '<button data-ch="'+ch+'" data-d="+1">+</button>'+
          '<button class="big" data-ch="'+ch+'" data-d="+5">+5</button>'+
        '</div>';
      g.appendChild(d);
    })(i);
  }
  g.addEventListener('click',function(e){
    var ch=parseInt(e.target.getAttribute('data-ch'));
    var delta=parseInt(e.target.getAttribute('data-d'));
    if(isNaN(ch)||isNaN(delta)) return;
    adjTrim(ch,delta);
  });
}

function adjTrim(ch,delta){
  trims[ch]=Math.max(-45,Math.min(45,trims[ch]+delta));
  refreshTrimUI();
  get('/trim?ch='+ch+'&v='+trims[ch],
    function(){log('ok','CH'+ch+' ('+CH[ch]+') trim = '+(trims[ch]>=0?'+':'')+trims[ch]+'\u00B0');},
    function(){log('error','CH'+ch+' trim failed');});
}

function refreshTrimUI(){
  for(var i=0;i<4;i++) document.getElementById('tv'+i).textContent=(trims[i]>=0?'+':'')+trims[i]+'\u00B0';
  document.getElementById('codeBox').textContent='int trim[4] = {'+trims[0]+', '+trims[1]+', '+trims[2]+', '+trims[3]+'};';
}

buildTrim();

document.getElementById('bSave').addEventListener('click',function(){
  get('/save',function(){log('ok','Saved: '+document.getElementById('codeBox').textContent);toast('Saved to flash!');},function(){log('error','Save failed');});
});
document.getElementById('bReset').addEventListener('click',function(){
  trims=[0,0,0,0];refreshTrimUI();
  get('/resettrim',function(){log('warn','Trims reset to 0');toast('Trims reset');},null);
});
document.getElementById('bCopy').addEventListener('click',function(){
  var t=document.getElementById('codeBox').textContent;
  if(navigator.clipboard){navigator.clipboard.writeText(t).then(function(){toast('Copied!');});}
  else{toast('Select text above and copy manually');}
});
document.getElementById('bAssembly').addEventListener('click',function(){
  get('/assembly',function(){
    document.getElementById('assemblyStatus').textContent='Holding raw 90° — attach horns now.';
    document.getElementById('assemblyStatus').style.color='#fbbf24';
    log('warn','ASSEMBLY: raw 90° — no trim applied');toast('Holding raw 90°');
  },function(){log('error','Assembly failed');});
});
document.getElementById('bAssemblyOff').addEventListener('click',function(){
  get('/move?cmd=stand',function(){
    document.getElementById('assemblyStatus').textContent='Released — normal stand with trim.';
    document.getElementById('assemblyStatus').style.color='#64748b';
    log('ok','Assembly off — standing with trim');toast('Normal stand');
  },null);
});
document.getElementById('bClearLog').addEventListener('click',function(){document.getElementById('log').innerHTML='';});

/* ─────────────────────────────────────────────────────────
   MOTOR CONTROL SLIDERS
───────────────────────────────────────────────────────── */
(function(){
  var NAMES=['Left Hip','Left Foot','Right Hip','Right Foot'];
  var sliders=[],vals=[];
  var container=document.getElementById('motorSliders');
  for(var i=0;i<4;i++){
    (function(ch){
      var row=document.createElement('div');row.className='sl-row';row.style.marginBottom='10px';
      row.innerHTML=
        '<div class="sl-top">CH'+ch+' \u2014 '+NAMES[ch]+
          ' <span id="mval'+ch+'" style="color:#7dd3fc;font-weight:700;min-width:38px;text-align:right">90\u00B0</span>'+
        '</div>'+
        '<input type="range" id="msl'+ch+'" min="0" max="180" value="90" step="1">';
      container.appendChild(row);
      var sl=document.getElementById('msl'+ch);
      var vl=document.getElementById('mval'+ch);
      sliders.push(sl);vals.push(vl);
      var pending=null;
      function sendPos(deg){fetch('/servopos?ch='+ch+'&deg='+deg).catch(function(){log('error','servopos CH'+ch+' failed');});}
      sl.addEventListener('input',function(){
        var deg=parseInt(sl.value);vl.textContent=deg+'\u00B0';
        if(pending===null){sendPos(deg);pending=setTimeout(function(){pending=null;sendPos(parseInt(sl.value));},50);}
      });
      sl.addEventListener('change',function(){
        var deg=parseInt(sl.value);vl.textContent=deg+'\u00B0';sendPos(deg);
        log('info','CH'+ch+' ('+NAMES[ch]+') \u2192 '+deg+'\u00B0');
      });
    })(i);
  }
  fetch('/servopos').then(function(r){return r.json();}).then(function(d){
    if(d.pos) d.pos.forEach(function(deg,ch){sliders[ch].value=deg;vals[ch].textContent=deg+'\u00B0';});
  }).catch(function(){});
  document.getElementById('bServoCal').addEventListener('click',function(){
    var btn=this; btn.disabled=true;
    var st=document.getElementById('motorCalStatus');
    st.textContent='Moving all to 90°…';st.style.color='#fbbf24';
    fetch('/servocalibrate').then(function(r){return r.text();}).then(function(){
      sliders.forEach(function(sl,ch){sl.value=90;vals[ch].textContent='90\u00B0';});
      st.textContent='All motors at natural centre (90°)';st.style.color='#4ade80';
      toast('Motors \u2192 90\u00B0');log('ok','All 4 servos calibrated to 90\u00B0');
    }).catch(function(e){st.textContent='Calibrate failed';st.style.color='#f87171';log('error','servocalibrate: '+e.message);})
    .finally(function(){btn.disabled=false;});
  });
  document.getElementById('bServoDefault').addEventListener('click',function(){
    var btn=this; btn.disabled=true;
    var st=document.getElementById('servoDefaultStatus');
    st.textContent='Saving current positions as default…';st.style.color='#fbbf24';
    fetch('/savepose?nocal=1').then(function(r){
      if(!r.ok) return r.text().then(function(t){throw new Error(t||'HTTP '+r.status);});return r.json();
    }).then(function(d){
      st.textContent='Saved default pose ['+d.neutral.join(',')+']';st.style.color='#4ade80';
      toast('Default position saved');log('ok','savepose(default): ['+d.neutral.join(',')+'] radius='+d.radius);
    }).catch(function(e){st.textContent='Save failed: '+e.message;st.style.color='#f87171';log('error','savepose default: '+e.message);})
    .finally(function(){btn.disabled=false;});
  });
})();

/* ─────────────────────────────────────────────────────────
   SERVO GPIO PIN MAP
───────────────────────────────────────────────────────── */
(function(){
  var JOINT_NAMES=['Left Hip','Left Foot','Right Hip','Right Foot'];
  var sels=[],curPins=[40,38,39,41];
  var statusEl=document.getElementById('pinMapStatus');
  var conflictEl=document.getElementById('pinMapConflict');
  var savedEl=document.getElementById('pinMapSaved');
  var saveBtn=document.getElementById('bPinMapSave');
  var grid=document.getElementById('pinMapGrid');

  function buildGrid(allowed,current){
    grid.innerHTML='';sels=[];
    for(var i=0;i<4;i++){
      (function(ji){
        var label=document.createElement('div');
        label.style.cssText='font-size:11px;color:#94a3b8;display:flex;align-items:center;gap:6px';
        label.innerHTML='<span style="font-size:9px;color:#64748b;text-transform:uppercase;letter-spacing:.1em">CH'+ji+'</span> '+JOINT_NAMES[ji];
        var sel=document.createElement('select');sel.className='speak-input';
        sel.style.cssText='padding:8px 10px;border-radius:8px;font-size:13px;font-weight:700;background:#0d1520;color:#7dd3fc;border:1px solid var(--line);cursor:pointer';
        allowed.forEach(function(p){
          var opt=document.createElement('option');opt.value=p;opt.textContent='GPIO '+p;
          if(p===current[ji]) opt.selected=true;sel.appendChild(opt);
        });
        sel.addEventListener('change',validateConflicts);sels.push(sel);
        grid.appendChild(label);grid.appendChild(sel);
      })(i);
    }
    validateConflicts();
  }

  function validateConflicts(){
    var seen={},dup=false;
    sels.forEach(function(s){var v=s.value;if(seen[v])dup=true;seen[v]=true;});
    if(dup){conflictEl.textContent='\u26A0 Each GPIO must be used exactly once — resolve duplicates before saving.';saveBtn.disabled=true;}
    else{conflictEl.textContent='';saveBtn.disabled=false;}
  }

  function loadPinMap(){
    savedEl.textContent='loading\u2026';
    fetch('/servomap').then(function(r){return r.json();}).then(function(d){
      curPins=d.pins.slice();buildGrid(d.allowed,d.pins);
      savedEl.textContent=d.saved?'\u2713 saved':'unsaved (using defaults)';
      savedEl.style.color=d.saved?'#4ade80':'#fbbf24';
      statusEl.textContent='Current: LH=GPIO'+d.pins[0]+' LF=GPIO'+d.pins[1]+' RH=GPIO'+d.pins[2]+' RF=GPIO'+d.pins[3];
      statusEl.style.color='#64748b';
      log('info','Pin map loaded: LH='+d.pins[0]+' LF='+d.pins[1]+' RH='+d.pins[2]+' RF='+d.pins[3]+(d.saved?' (saved)':' (defaults)'));
    }).catch(function(e){
      savedEl.textContent='error';savedEl.style.color='#f87171';
      statusEl.textContent='Could not load pin map: '+e.message;statusEl.style.color='#f87171';
      log('error','Pin map load: '+e.message);
    });
  }

  saveBtn.addEventListener('click',function(){
    if(saveBtn.disabled) return;
    var newPins=sels.map(function(s){return parseInt(s.value);});
    saveBtn.disabled=true;statusEl.textContent='Saving\u2026';statusEl.style.color='#fbbf24';
    fetch('/servomap',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({pins:newPins})})
    .then(function(r){if(!r.ok)return r.text().then(function(t){throw new Error(t||'HTTP '+r.status);});return r.json();})
    .then(function(d){
      curPins=d.pins.slice();savedEl.textContent='\u2713 saved';savedEl.style.color='#4ade80';
      statusEl.textContent='Saved! LH=GPIO'+d.pins[0]+' LF=GPIO'+d.pins[1]+' RH=GPIO'+d.pins[2]+' RF=GPIO'+d.pins[3];
      statusEl.style.color='#4ade80';toast('Servo pin map saved!');
      log('ok','Pin map saved: LH=GPIO'+d.pins[0]+' LF=GPIO'+d.pins[1]+' RH=GPIO'+d.pins[2]+' RF=GPIO'+d.pins[3]);
    }).catch(function(e){
      statusEl.textContent='\u2717 Save failed: '+e.message;statusEl.style.color='#f87171';log('error','Pin map save: '+e.message);
    }).finally(function(){saveBtn.disabled=false;validateConflicts();});
  });

  loadPinMap();
})();


/* ─────────────────────────────────────────────────────────
   AUDIO: QUICK RECORD
───────────────────────────────────────────────────────── */
(function(){
  var btn=document.getElementById('bRecord');
  var st=document.getElementById('recStatus');
  var busy=false;
  document.getElementById('bPlayRobot').addEventListener('click',function(){
    fetch('/play').then(function(r){
      if(!r.ok) throw new Error(r.status===404?'record something first':'HTTP '+r.status);
      log('ok','Replaying last recording on robot speaker');toast('Playing on robot!');
    }).catch(function(e){log('error','Robot play: '+e.message);toast('No recording yet');});
  });
  btn.addEventListener('click',function(){
    if(busy) return;
    var secs=document.getElementById('recSecs').value;
    busy=true;btn.classList.add('active');btn.textContent='\u23FA Recording\u2026';
    st.textContent='Capturing '+secs+' s of audio\u2026';st.style.color='#fbbf24';
    fetch('/record?secs='+secs).then(function(r){
      if(!r.ok) throw new Error('HTTP '+r.status);return r.blob();
    }).then(function(blob){
      if(blob.size<=44){throw new Error('empty WAV ('+blob.size+' bytes) \u2014 mic gave no data');}
      var url=URL.createObjectURL(blob);
      var player=document.getElementById('recPlayer');
      if(player.src) URL.revokeObjectURL(player.src);
      player.src=url;player.style.display='block';
      var a=document.createElement('a');a.href=url;a.download='otto_recording.wav';a.click();
      st.textContent='Done ('+Math.round(blob.size/1024)+' KB) \u2014 press play below to listen.';
      st.style.color='#4ade80';log('ok','Recording done ('+blob.size+' bytes)');toast('Recording done!');
    }).catch(function(e){
      st.textContent='Record failed: '+e.message;st.style.color='#f87171';log('error','Record: '+e.message);
    }).finally(function(){busy=false;btn.classList.remove('active');btn.textContent='\u23FA Quick Record';});
  });
})();

/* ─────────────────────────────────────────────────────────
   AUDIO: SPEAK (SAM TTS)
───────────────────────────────────────────────────────── */
(function(){
  var btn=document.getElementById('bSpeak');
  var st=document.getElementById('speakStatus');
  var inp=document.getElementById('speakText');
  var vol=document.getElementById('sVol');
  vol.addEventListener('input',function(){document.getElementById('vVol').textContent=vol.value;});
  var vspd=document.getElementById('sVSpeed'),vpit=document.getElementById('sVPitch');
  vspd.addEventListener('input',function(){document.getElementById('vVSpeed').textContent=vspd.value;});
  vpit.addEventListener('input',function(){document.getElementById('vVPitch').textContent=vpit.value;});
  btn.addEventListener('click',function(){
    var txt=inp.value.trim();if(!txt){inp.focus();return;}
    var mode=document.getElementById('speakMode').value;
    if(mode==='voice'&&!samOK){
      st.textContent='Voice not available \u2014 firmware built without SAM (see log).';st.style.color='#f87171';
      log('error','Voice mode needs ESP8266Audio + ESP8266SAM libs and USE_SAM_TTS 1');return;
    }
    btn.disabled=true;st.textContent='Speaking\u2026';st.style.color='#7dd3fc';
    var body=new URLSearchParams();
    body.append('text',txt);body.append('mode',mode);
    body.append('voice',document.getElementById('speakVoice').value);
    body.append('vol',vol.value);
    if(vspd.value!=='72')body.append('spd',vspd.value);
    if(vpit.value!=='64')body.append('pit',vpit.value);
    fetch('/speak',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:body.toString()})
      .then(function(r){if(!r.ok) throw new Error('HTTP '+r.status);
        st.textContent='Done.';st.style.color='#4ade80';log('ok','Speak ('+mode+'): "'+txt+'"');toast('Speaking!');
      }).catch(function(e){st.textContent='Speak failed: '+e.message;st.style.color='#f87171';log('error','Speak: '+e.message);})
      .finally(function(){btn.disabled=false;});
  });
  inp.addEventListener('keydown',function(e){if(e.key==='Enter')btn.click();});
})();

/* ─────────────────────────────────────────────────────────
   MAC TTS
───────────────────────────────────────────────────────── */
(function(){
  var btn=document.getElementById('bMacTts');
  var inp=document.getElementById('macTtsText');
  var st=document.getElementById('macTtsStatus');
  btn.addEventListener('click',function(){
    var txt=inp.value.trim();if(!txt){inp.focus();return;}
    var voice=document.getElementById('macVoice').value;
    var rate=document.getElementById('macRate').value;
    btn.disabled=true;st.textContent='Synthesising on Mac\u2026';st.style.color='#7dd3fc';
    log('head','Mac TTS: "'+txt+'" voice='+voice+' rate='+rate+' Hz');
    var MAC_TTS_PORT=8756,robotIP=window.location.hostname;
    var macUrl='http://127.0.0.1:'+MAC_TTS_PORT+'/tts?voice='+encodeURIComponent(voice)+'&rate='+rate+'&text='+encodeURIComponent(txt);
    st.textContent='Connecting to Mac helper (port '+MAC_TTS_PORT+')\u2026';
    fetch(macUrl).then(function(r){if(!r.ok) throw new Error('Mac helper HTTP '+r.status);return r.blob();})
    .then(function(wavBlob){
      st.textContent='Sending to robot ('+Math.round(wavBlob.size/1024)+' KB)\u2026';st.style.color='#7dd3fc';
      log('ok','Mac TTS: WAV '+wavBlob.size+' bytes \u2192 robot');
      var fd=new FormData();fd.append('f',new File([wavBlob],'tts.wav',{type:'audio/wav'}));
      return fetch('http://'+robotIP+'/mactts',{method:'POST',body:fd});
    }).then(function(r){
      if(!r.ok) throw new Error('Robot HTTP '+r.status);
      st.textContent='Playing on robot! \uD83D\uDD0A';st.style.color='#4ade80';log('ok','Mac TTS: playing on robot');toast('Speaking!');
    }).catch(function(e){
      var msg=e.message||String(e);
      if(msg.indexOf('Failed to fetch')>=0||msg.indexOf('ERR_CONNECTION_REFUSED')>=0){
        st.innerHTML='<span style="color:#f87171">\u2717 Mac helper not running.</span>'
          +'<br><span style="color:#fbbf24">Run this in Terminal:</span>'
          +'<br><code style="color:#86efac;font-size:10px">python3 Bob_ep32Code/mac_tts_server.py</code>'
          +'<br><span style="color:#64748b">Or: <code style="color:#86efac">./Bob_ep32Code/mac_tts.sh "'+txt.replace(/"/g,"'")+'"</code></span>';
      } else {st.textContent='\u2717 '+msg;st.style.color='#f87171';}
      log('error','Mac TTS: '+msg);
    }).finally(function(){btn.disabled=false;});
  });
  inp.addEventListener('keydown',function(e){if(e.key==='Enter')btn.click();});
})();

/* ─────────────────────────────────────────────────────────
   AUDIO DIAGNOSTICS
───────────────────────────────────────────────────────── */
(function(){
  document.getElementById('bMicCal').addEventListener('click',function(){
    var btn=this,status=document.getElementById('micVerdict');
    var AudioCtx=window.AudioContext||window.webkitAudioContext;
    if(!AudioCtx){status.textContent='Calibration needs a browser with Web Audio support.';return;}
    btn.disabled=true;status.textContent='Recording 2 s — keep this device close to Anvyxo\u2026';
    var ctx=new AudioCtx(),req=fetch('/record?secs=2');
    ctx.resume().then(function(){
      var osc=ctx.createOscillator(),gain=ctx.createGain();
      osc.type='sine';osc.frequency.value=1000;
      gain.gain.setValueAtTime(0,ctx.currentTime);
      gain.gain.linearRampToValueAtTime(0.18,ctx.currentTime+0.04);
      gain.gain.setValueAtTime(0.18,ctx.currentTime+0.62);
      gain.gain.linearRampToValueAtTime(0,ctx.currentTime+0.70);
      osc.connect(gain);gain.connect(ctx.destination);
      osc.start(ctx.currentTime+0.30);osc.stop(ctx.currentTime+1.02);
    }).catch(function(e){status.textContent='Could not start reference tone: '+e.message;});
    req.then(function(r){if(!r.ok) throw new Error('HTTP '+r.status+' \u2014 '+r.statusText);return r.blob();})
    .then(function(blob){
      if(blob.size<=44) throw new Error('empty recording');
      var url=URL.createObjectURL(blob),a=document.createElement('a');
      a.href=url;a.download='bob_mic_calibration.wav';a.click();
      setTimeout(function(){URL.revokeObjectURL(url);},5000);
      status.textContent='Calibration captured ('+Math.round(blob.size/1024)+' KB). WAV downloaded; play it back to check the mic.';
      log('ok','Mic reference tone captured and downloaded');
    }).catch(function(e){status.textContent='Calibration failed: '+e.message;log('error','Mic calibration: '+e.message);})
    .finally(function(){btn.disabled=false;ctx.close();});
  });

  document.getElementById('bTone').addEventListener('click',function(){
    log('info','Playing 1 kHz test tone\u2026');
    get('/testtone?f=1000&ms=800',
      function(){log('ok','Tone sent. Heard nothing? Check MAX98357A VIN/GND, speaker +/- and DIN=GPIO14');},
      function(){log('error','Speaker output is disabled in the current mic-only bring-up profile.');});
  });

  var meterOn=false,meterTimer=null;
  var mBtn=document.getElementById('bMicMeter');
  var bar=document.getElementById('micBar'),val=document.getElementById('micVal'),verdict=document.getElementById('micVerdict');
  var zeroCount=0,lastMicState='';
  function pollMic(){
    fetch('/miclevel').then(function(r){return r.json();}).then(function(d){
      var pct=Math.min(100,Math.round(d.peak/32768*100*3));
      bar.style.width=pct+'%';
      bar.style.background=(d.peak===0)?'#ef4444':(d.peak<80?'#fbbf24':'#22c55e');
      val.textContent='peak='+d.peak+' rms='+d.rms+' n='+d.n;
      var state,msg,lvl;
      if(d.n===0){state='no_data';msg='MIC: i2s_read returned 0 samples \u2014 I2S driver not running';lvl='error';verdict.textContent='\u2717 i2s_read returned no data';verdict.style.color='#f87171';}
      else if(d.peak===0){zeroCount++;state='zero_'+(zeroCount>=5?'x5':'low');msg=zeroCount===1?'MIC: all-zero reads \u2014 check BCLK\u2192GPIO43, WS\u2192GPIO44, DOUT\u2192GPIO48, SEL\u2192GND':zeroCount===5?'MIC: 5x all-zero reads':null;lvl='error';verdict.textContent='\u2717 Dead silence \u2014 check BCLK\u2192GPIO43, WS\u2192GPIO44, DOUT\u2192GPIO48';verdict.style.color='#f87171';}
      else if(d.peak<80&&d.rms<30){zeroCount=0;state='weak';msg='MIC: near-silence (peak='+d.peak+' rms='+d.rms+')';lvl='warn';verdict.textContent='\u26A0 Near-silence \u2014 mic alive but very weak';verdict.style.color='#fbbf24';}
      else{zeroCount=0;state='ok';msg='MIC: signal OK (peak='+d.peak+' rms='+d.rms+')';lvl='ok';verdict.textContent='\u2713 Mic signal OK';verdict.style.color='#4ade80';}
      if(msg&&state!==lastMicState){log(lvl,msg);lastMicState=state;}
    }).catch(function(e){
      var errMsg='/miclevel failed'+(e&&e.message?' ('+e.message+')':'');
      verdict.textContent='\u2717 '+errMsg;verdict.style.color='#f87171';
      if(lastMicState!=='fetch_fail'){log('error','MIC: '+errMsg);lastMicState='fetch_fail';}
    }).finally(function(){if(meterOn)meterTimer=setTimeout(pollMic,400);});
  }
  mBtn.addEventListener('click',function(){
    meterOn=!meterOn;
    mBtn.textContent=meterOn?'\uD83C\uDF99 Mic Meter: ON':'\uD83C\uDF99 Mic Meter: OFF';
    mBtn.style.background=meterOn?'#a16207':'#2d2008';
    if(meterOn){zeroCount=0;lastMicState='';log('head','Mic meter started \u2014 talk or clap near the mic');pollMic();}
    else{clearTimeout(meterTimer);bar.style.width='0%';val.textContent='\u2014';verdict.textContent='';lastMicState='';}
  });

  document.getElementById('bMicScan').addEventListener('click',function(){
    var out=document.getElementById('diagOut');out.style.display='block';
    out.textContent='Scanning 8 mic configs (~4 s)\u2026 KEEP TALKING / CLAPPING near the mic NOW!';
    log('head','Mic auto-scan started \u2014 make noise near the mic!');
    fetch('/micscan').then(function(r){return r.text();}).then(function(t){
      out.textContent=t;
      if(t.indexOf('SIGNAL')>=0){
        log('ok','Scan found a working config \u2014 applied & saved. Turn on Mic Meter to confirm.');toast('Mic config found!');
        fetch('/state').then(function(r){return r.json();}).then(function(d){
          document.getElementById('mCh').value=d.mch;document.getElementById('mFmt').value=d.mfmt;
          document.getElementById('mFix').value=d.mfix;document.getElementById('mSh').value=d.msh;
        }).catch(function(){});
      }else{log('error','Scan: all 8 configs dead \u2014 100% hardware, see checklist in report');}
    }).catch(function(e){out.textContent='Scan failed: '+e.message;log('error','Mic scan: '+e.message);});
  });

  document.getElementById('bMicApply').addEventListener('click',function(){
    var q='/micconfig?ch='+document.getElementById('mCh').value
      +'&fmt='+document.getElementById('mFmt').value
      +'&fix='+document.getElementById('mFix').value
      +'&shift='+document.getElementById('mSh').value;
    get(q,function(){log('ok','Mic config applied & saved');toast('Mic config applied');},function(){log('error','Mic config apply failed');});
  });

  document.getElementById('bDiag').addEventListener('click',function(){
    var out=document.getElementById('diagOut');out.style.display='block';
    out.textContent='Running\u2026 (mic sample + 1 s speaker tone, ~2 s)';
    log('head','Running full audio diagnostic\u2026');
    fetch('/diag').then(function(r){return r.text();}).then(function(t){
      out.textContent=t;
      var micOK=t.indexOf('SIGNAL PRESENT')>=0,spkOK=t.indexOf('i2s_write OK')>=0;
      log(micOK?'ok':'error','Diag: mic '+(micOK?'signal present':'NO signal \u2014 see report'));
      log(spkOK?'ok':'error','Diag: speaker i2s_write '+(spkOK?'OK':'FAILED'));
    }).catch(function(e){out.textContent='Diagnostic failed: '+e.message;log('error','Diag: '+e.message);});
  });
})();

/* ─────────────────────────────────────────────────────────
   SOUND CLIPS
───────────────────────────────────────────────────────── */
(function(){
  var listEl=document.getElementById('clipList');
  var upSt=document.getElementById('clipUpStatus');
  var recSt=document.getElementById('recFileStatus');

  function fmtKB(b){return b<1024?b+' B':(b/1024).toFixed(b<102400?1:0)+' KB';}

  function refresh(){
    fetch('/files').then(function(r){return r.json();}).then(function(d){
      if(!d.ok){listEl.innerHTML='<span style="color:#f87171">Filesystem not mounted.</span>';return;}
      var pct=d.total?Math.round(d.used/d.total*100):0;
      document.getElementById('fsBar').style.width=pct+'%';
      document.getElementById('fsBar').style.background=pct>90?'#ef4444':(pct>70?'#fbbf24':'#22c55e');
      document.getElementById('fsVal').textContent=fmtKB(d.used)+' / '+fmtKB(d.total)+' \u2014 up to '+d.maxsecs+' s free';
      document.getElementById('recCap').textContent=d.maxsecs+' seconds';
      document.getElementById('recFileSecs').max=Math.max(1,d.maxsecs);
      if(!d.files.length){listEl.innerHTML='<span style="color:#64748b">No clips yet.</span>';return;}
      listEl.innerHTML='';
      d.files.forEach(function(f){
        var row=document.createElement('div');
        row.style.cssText='display:flex;align-items:center;gap:6px;padding:6px 0;border-bottom:1px solid #1e293b';
        var nm=document.createElement('div');nm.style.cssText='flex:1;color:var(--text);word-break:break-all';
        nm.innerHTML=f.n+' <span style="color:#64748b">('+fmtKB(f.s)+')</span>';row.appendChild(nm);
        function mk(label,bg,col,fn){
          var b=document.createElement('button');b.textContent=label;
          b.style.cssText='padding:6px 9px;border-radius:6px;border:none;font-size:11px;font-weight:700;cursor:pointer;background:'+bg+';color:'+col;
          b.addEventListener('click',fn);row.appendChild(b);
        }
        mk('\u25B6 Robot','#0f766e','#99f6e4',function(){
          fetch('/playfile?name='+encodeURIComponent(f.n)).then(function(r){
            if(!r.ok) throw new Error('HTTP '+r.status);
            log('ok','Playing '+f.n+' on robot');toast('Playing '+f.n);
          }).catch(function(e){log('error','Play '+f.n+': '+e.message);});
        });
        mk('\u2B07','#1e3a5f','#7dd3fc',function(){
          var a=document.createElement('a');a.href='/clip?name='+encodeURIComponent(f.n);a.download=f.n;a.click();
        });
        mk('\u2715','#450a0a','#fca5a5',function(){
          fetch('/delfile?name='+encodeURIComponent(f.n)).then(function(r){
            if(!r.ok) throw new Error('HTTP '+r.status);
            log('warn','Deleted '+f.n);toast('Deleted');refresh();
          }).catch(function(e){log('error','Delete: '+e.message);});
        });
        listEl.appendChild(row);
      });
    }).catch(function(){listEl.textContent='Could not read clip list.';});
  }
  document.getElementById('bClipRefresh').addEventListener('click',refresh);

  Array.prototype.forEach.call(document.querySelectorAll('[data-secs]'),function(b){
    b.addEventListener('click',function(){document.getElementById('recFileSecs').value=b.getAttribute('data-secs');});
  });

  document.getElementById('bRecFile').addEventListener('click',function(){
    var secs=parseInt(document.getElementById('recFileSecs').value)||5;
    var name=document.getElementById('recFileName').value.trim()||'rec.wav';
    var btn=this; btn.disabled=true;
    recSt.textContent='Recording '+secs+' s to flash\u2026 talk now!';recSt.style.color='#fbbf24';
    log('head','Recording '+secs+' s to flash as '+name);
    fetch('/recfile?secs='+secs+'&name='+encodeURIComponent(name))
    .then(function(r){return r.text().then(function(t){if(!r.ok)throw new Error(t||('HTTP '+r.status));return t;})})
    .then(function(t){recSt.textContent='Saved as '+t+' \u2014 play it on the robot below.';recSt.style.color='#4ade80';log('ok','Recorded to flash: '+t);toast('Recorded!');refresh();})
    .catch(function(e){recSt.textContent='Record failed: '+e.message;recSt.style.color='#f87171';log('error','Flash record: '+e.message);})
    .finally(function(){btn.disabled=false;});
  });

  function makeWav(samples,rate){
    var n=samples.length,buf=new ArrayBuffer(44+n*2),v=new DataView(buf);
    function str(o,s){for(var i=0;i<s.length;i++)v.setUint8(o+i,s.charCodeAt(i));}
    str(0,'RIFF');v.setUint32(4,36+n*2,true);str(8,'WAVE');
    str(12,'fmt ');v.setUint32(16,16,true);v.setUint16(20,1,true);
    v.setUint16(22,1,true);v.setUint32(24,rate,true);v.setUint32(28,rate*2,true);
    v.setUint16(32,2,true);v.setUint16(34,16,true);
    str(36,'data');v.setUint32(40,n*2,true);
    for(var i=0;i<n;i++){var s=Math.max(-1,Math.min(1,samples[i]));v.setInt16(44+i*2,s<0?s*0x8000:s*0x7FFF,true);}
    return new Blob([buf],{type:'audio/wav'});
  }

  function convertPicked(){
    var inp=document.getElementById('clipFile'),file=inp.files&&inp.files[0];
    if(!file) return Promise.reject(new Error('pick an audio file first'));
    var rate=parseInt(document.getElementById('clipRate').value);
    upSt.textContent='Decoding '+file.name+'\u2026';upSt.style.color='#7dd3fc';
    log('head','Converting '+file.name+' at '+rate+' Hz');
    return file.arrayBuffer().then(function(ab){
      var AC=window.AudioContext||window.webkitAudioContext;return new AC().decodeAudioData(ab);
    }).then(function(decoded){
      upSt.textContent='Resampling '+decoded.duration.toFixed(1)+' s to '+rate+' Hz\u2026';
      var OAC=window.OfflineAudioContext||window.webkitOfflineAudioContext;
      var off=new OAC(1,Math.max(1,Math.ceil(decoded.duration*rate)),rate);
      var src=off.createBufferSource();src.buffer=decoded;src.connect(off.destination);src.start();
      return off.startRendering();
    }).then(function(rendered){
      var base=file.name.replace(/\.[^.]+$/,'').replace(/[^A-Za-z0-9_-]/g,'_').slice(0,30);
      return {blob:makeWav(rendered.getChannelData(0),rate),name:(base||'clip')+'.wav',secs:rendered.length/rate};
    });
  }

  document.getElementById('bClipStream').addEventListener('click',function(){
    var btn=this; btn.disabled=true;
    convertPicked().then(function(c){
      upSt.textContent='Streaming '+c.secs.toFixed(1)+' s to the speaker ('+fmtKB(c.blob.size)+')\u2026';
      var fd=new FormData();fd.append('f',c.blob,c.name);
      return fetch('/streamplay',{method:'POST',body:fd}).then(function(r){if(!r.ok)throw new Error('HTTP '+r.status);return c;});
    }).then(function(c){
      upSt.textContent='Finished playing '+c.name+' (nothing stored).';upSt.style.color='#4ade80';
      log('ok','Streamed '+c.name+' to speaker');toast('Playing!');
    }).catch(function(e){upSt.textContent='Failed: '+e.message;upSt.style.color='#f87171';log('error','Stream play: '+e.message);})
    .finally(function(){btn.disabled=false;});
  });

  document.getElementById('bClipUpload').addEventListener('click',function(){
    var btn=this; btn.disabled=true;
    convertPicked().then(function(c){
      upSt.textContent='Uploading '+c.name+' ('+fmtKB(c.blob.size)+')\u2026';
      var fd=new FormData();fd.append('f',c.blob,c.name);
      return fetch('/upload',{method:'POST',body:fd}).then(function(r){
        return r.text().then(function(t){if(!r.ok)throw new Error(t||('HTTP '+r.status));return c.name;});
      });
    }).then(function(name){
      upSt.textContent='Saved '+name+' \u2014 tap \u25B6 Robot below to play it.';upSt.style.color='#4ade80';
      log('ok','Saved clip '+name);toast('Clip saved!');refresh();
    }).catch(function(e){upSt.textContent='Failed: '+e.message;upSt.style.color='#f87171';log('error','Clip save: '+e.message);})
    .finally(function(){btn.disabled=false;});
  });

  refresh();
})();


/* ─────────────────────────────────────────────────────────
   IMU: 3D CUBE + CAL + RAW VALUES + LIFT BANNER
   (lazy-inited: only starts when IMU tab is first opened)
───────────────────────────────────────────────────────── */
['eyeDemoStart','eyeDemoStop'].forEach(function(id){document.getElementById(id).onclick=function(){
  var label=document.getElementById('eyeDemoStatus');label.textContent='Sending...';
  fetch('/eyes/demo'+(id==='eyeDemoStop'?'?stop=1':'')).then(function(r){if(!r.ok)return r.text().then(function(t){throw new Error(t);});return r.text();}).then(function(t){label.textContent=t;}).catch(function(e){label.textContent='Could not run: '+e.message;});
};});
function startImuPoll(){
  var cube      = document.getElementById('imuCube');
  var lift      = document.getElementById('imuLiftBanner');
  var avals     = document.getElementById('imuAVals');
  var gvals     = document.getElementById('imuGVals');
  var status    = document.getElementById('imuStatus');
  var offline   = document.getElementById('imuOffline');
  var axFill    = document.getElementById('imuAxFill');
  var ayFill    = document.getElementById('imuAyFill');
  var azFill    = document.getElementById('imuAzFill');
  var gxFill    = document.getElementById('imuGxFill');
  var gyFill    = document.getElementById('imuGyFill');
  var gzFill    = document.getElementById('imuGzFill');
  var pitchEl   = document.getElementById('imuPitch');
  var rollEl    = document.getElementById('imuRoll');
  var deltaBar  = document.getElementById('imuDeltaBar');
  var deltaVal  = document.getElementById('imuDeltaVal');
  var calBadge  = document.getElementById('imuCalBadge');
  var calStatus = document.getElementById('imuCalStatus');
  var calDetail = document.getElementById('imuCalDetail');
  var thrSlider = document.getElementById('sImuThresh');
  var thrVal    = document.getElementById('vImuThresh');
  var liftBtn   = document.getElementById('bLiftToggle');
  var helpMeBtn = document.getElementById('bHelpMeToggle');
  var liftBusy=false, helpMeBusy=false, paused=false, imuTimer=null, lastThresh=240;

  function toBar(v,span){var pct=50+(v/span)*50;return Math.max(0,Math.min(100,Math.round(pct)));}

  // Median rejects isolated spikes; time-based easing is independent of FPS.
  // Only visualization is filtered. Diagnostics below retain raw readings.
  var curX=0,curY=0,tgtX=0,tgtY=0,velX=0,velY=0,rafOn=false,lastFrame=0,tau=.20;
  var histories=[[],[]],seen=false,backView=false;
  var pitchSign=1,rollSign=1;
  try{pitchSign=localStorage.getItem('imuPitchReverse')==='1'?-1:1;rollSign=localStorage.getItem('imuRollReverse')==='1'?-1:1;}catch(e){}
  function directionButtons(){['Pitch','Roll'].forEach(function(axis){var sign=axis==='Pitch'?pitchSign:rollSign;var b=document.getElementById('imuFlip'+axis);b.textContent='Reverse '+axis.toLowerCase()+': '+(sign<0?'ON':'OFF');b.setAttribute('aria-pressed',sign<0?'true':'false');});}
  function wakeCube(){if(!rafOn){rafOn=true;requestAnimationFrame(animCube);}}
  document.getElementById('imuFlipPitch').onclick=function(){pitchSign*=-1;try{localStorage.setItem('imuPitchReverse',pitchSign<0?'1':'0');}catch(e){}directionButtons();wakeCube();};
  document.getElementById('imuFlipRoll').onclick=function(){rollSign*=-1;try{localStorage.setItem('imuRollReverse',rollSign<0?'1':'0');}catch(e){}directionButtons();wakeCube();};
  directionButtons();

  document.getElementById('imuSmooth').onchange=function(){tau=Number(this.value);};
  document.getElementById('imuViewBack').onclick=function(){backView=!backView;document.getElementById('imuCamera').style.transform='rotateX(-18deg) rotateY('+(backView?152:-28)+'deg)';this.textContent=backView?'View front':'View back';};
  document.getElementById('imuFocus').onclick=function(){var on=document.body.classList.toggle('imu-focus');this.textContent=on?'Exit recording view':'Recording view';};
  document.addEventListener('keydown',function(e){if(e.key==='Escape'){document.body.classList.remove('imu-focus');document.getElementById('imuFocus').textContent='Recording view';}});
  // Exact critically damped spring: continuous velocity as packets arrive.
  function dampCube(value,velocity,target,dt){var w=2/tau,d=value-target,j=velocity+w*d,e=Math.exp(-w*dt);return [target+(d+j*dt)*e,(velocity-w*j*dt)*e];}
  function animCube(t){
    var dt=lastFrame?Math.min((t-lastFrame)/1000,.1):1/60;lastFrame=t;
    var x=dampCube(curX,velX,tgtX,dt),y=dampCube(curY,velY,tgtY,dt);
    curX=x[0];velX=x[1];curY=y[0];velY=y[1];
    cube.style.transform='rotateX('+(curX*pitchSign).toFixed(2)+'deg) rotateZ('+(curY*rollSign).toFixed(2)+'deg)';
    pitchEl.textContent=(-curX).toFixed(1)+'\u00B0';rollEl.textContent=(-curY).toFixed(1)+'\u00B0';
    if(Math.abs(tgtX-curX)>.005||Math.abs(tgtY-curY)>.005||Math.abs(velX)>.02||Math.abs(velY)>.02){requestAnimationFrame(animCube);}
    else{rafOn=false;lastFrame=0;}
  }
  function aimCube(pitch,roll){
    if(!Number.isFinite(pitch)||!Number.isFinite(roll))return;
    var values=[-pitch,-roll].map(function(v,i){var h=histories[i],prev=h.length?h[h.length-1]:v;v=prev+((v-prev+180)%360+360)%360-180;h.push(v);if(h.length>3)h.shift();return h.slice().sort(function(a,b){return a-b;})[Math.floor(h.length/2)];});
    if(!seen){curX=tgtX=values[0];curY=tgtY=values[1];seen=true;}
    if(Math.abs(values[0]-tgtX)>.12)tgtX=values[0];
    if(Math.abs(values[1]-tgtY)>.12)tgtY=values[1];
    if(!rafOn){rafOn=true;requestAnimationFrame(animCube);}
  }

  function updateCalBadge(calOk){
    if(calOk){calBadge.textContent='CALIBRATED';calBadge.style.background='#14532d';calBadge.style.color='#bbf7d0';}
    else{calBadge.textContent='NOT CAL';calBadge.style.background='#1e293b';calBadge.style.color='#475569';}
    var banners=[document.getElementById('calBanner'),document.getElementById('calBannerImu')];
    banners.forEach(function(b){if(b)b.style.display=calOk?'none':'block';});
  }

  function renderLift(on){liftBtn.textContent=on?'ON':'OFF';liftBtn.style.background=on?'#166534':'#334155';liftBtn.style.color=on?'#bbf7d0':'#cbd5e1';}
  function renderHelpMe(on){helpMeBtn.textContent=on?'ON':'OFF';helpMeBtn.style.background=on?'#166534':'#334155';helpMeBtn.style.color=on?'#bbf7d0':'#cbd5e1';}

  function poll(){
    if(paused) return;
    /* Pause the 5 Hz poll whenever the IMU tab isn't the one on screen (or the
       browser tab is hidden): the robot's single-threaded web server was being
       hammered by this even when nobody was looking at the cube. Re-check
       cheaply every 500 ms so it resumes the instant the tab comes back. */
    if(document.hidden||currentTab!=='imu'){imuTimer=setTimeout(poll,500);return;}
    fetch('/imu').then(function(r){return r.json();}).then(function(d){
      if(!d.ok){
        offline.style.display='block';cube.style.opacity='0.25';updateCalBadge(false);
        var secs=Math.round((d.lastOkMs||0)/1000);
        status.textContent='IMU offline \u2014 retrying'+(secs?' (last seen '+secs+'s ago)':'')+(d.reconnects?' \u00B7 '+d.reconnects+' reconnect(s)':'');
        status.style.color='#f87171';lift.style.display='none';imuTimer=setTimeout(poll,1000);return;
      }
      offline.style.display='none';cube.style.opacity='1';
      updateCalBadge(d.calOk);
      if(!liftBusy&&typeof d.liftEn!=='undefined') renderLift(!!d.liftEn);
      if(d.thresh&&d.thresh!==lastThresh){lastThresh=d.thresh;thrSlider.value=d.thresh;thrVal.textContent=d.thresh;}
      var pitch=(typeof d.pitch==='number')?d.pitch:0, roll=(typeof d.roll==='number')?d.roll:0;
      aimCube(pitch,roll);
      // Angle readouts follow the rendered pose; raw accel/gyro remain below.
      var thresh=d.thresh||4000, delta=d.delta||0, pct=Math.min(100,Math.round(delta/thresh*100));
      deltaBar.style.width=pct+'%';deltaBar.style.background=pct>85?'#ef4444':(pct>55?'#fbbf24':'#22c55e');
      deltaVal.textContent='delta='+delta+' / thresh='+thresh;
      var cal=!!d.calOk;
      var ax=d.rax,ay=d.ray,az=d.raz,gx=d.rgx,gy=d.rgy,gz=d.rgz;
      axFill.style.width=toBar(ax,cal?981:1962)+'%';ayFill.style.width=toBar(ay,cal?981:1962)+'%';azFill.style.width=toBar(az,cal?981:1962)+'%';
      avals.textContent='ax='+ax+' ay='+ay+' az='+az+(cal?'  (raw '+d.ax+','+d.ay+','+d.az+')':'  (uncalibrated \u2014 raw)');
      gxFill.style.width=toBar(gx,cal?800:4000)+'%';gyFill.style.width=toBar(gy,cal?800:4000)+'%';gzFill.style.width=toBar(gz,cal?800:4000)+'%';
      gvals.textContent='gx='+gx+' gy='+gy+' gz='+gz+(cal?'  (raw '+d.gx+','+d.gy+','+d.gz+')':'  (uncalibrated \u2014 raw)');
      if(d.lift||d.react){
        lift.style.display='block';
        status.textContent=d.react===1?'SCARED \u2014 flipping feet':d.react===2?'Settling\u2026':d.react===3?'Re-learning resting pose\u2026':'LIFTED';
        status.style.color='#ef4444';
      } else {
        lift.style.display='none';
        status.textContent=(d.chip||'MPU')+(d.calOk?' \u2014 polling 5 Hz':' \u2014 calibrate after boot');
        status.style.color=d.calOk?'#4ade80':'#fbbf24';
      }
      imuTimer=setTimeout(poll,200);
    }).catch(function(){status.textContent='IMU fetch failed';status.style.color='#f87171';imuTimer=setTimeout(poll,1000);});
  }

  thrSlider.addEventListener('input',function(){thrVal.textContent=thrSlider.value;});

  document.getElementById('bImuCal').addEventListener('click',function(){
    var btn=this, thresh=parseInt(thrSlider.value)||4000;
    btn.disabled=true;calStatus.textContent='Sampling ~3 s \u2026 keep robot still!';calStatus.style.color='#fbbf24';calDetail.style.display='none';
    fetch('/imucal?thresh='+thresh).then(function(r){if(!r.ok)return r.text().then(function(t){throw new Error(t||'HTTP '+r.status);});return r.json();})
    .then(function(d){
      updateCalBadge(true);calStatus.textContent='Calibrated \u2014 all axes zeroed ('+d.n+' samples)';calStatus.style.color='#4ade80';
      calDetail.style.display='block';
      calDetail.textContent='baseline a='+d.ax+','+d.ay+','+d.az+' g='+d.gx+','+d.gy+','+d.gz+' restMag='+d.restMag+' thresh='+d.delta;
      lastThresh=d.delta;thrSlider.value=d.delta;thrVal.textContent=d.delta;
      log('ok','IMU calibrated: restMag='+d.restMag+' thresh='+d.delta);toast('IMU calibrated!');
    }).catch(function(e){calStatus.textContent='Calibration failed: '+e.message;calStatus.style.color='#f87171';log('error','IMU cal: '+e.message);})
    .finally(function(){btn.disabled=false;});
  });

  /* Both banner buttons trigger the same calibrate action */
  [document.getElementById('bBannerCal'),document.getElementById('bBannerCalImu')].forEach(function(b){
    if(b) b.addEventListener('click',function(){document.getElementById('bImuCal').click();});
  });

  document.getElementById('bSavePose').addEventListener('click',function(){
    var btn=this; btn.disabled=true;
    calStatus.textContent='Saving pose + calibrating \u2026 keep robot still!';calStatus.style.color='#fbbf24';
    fetch('/savepose').then(function(r){if(!r.ok)return r.text().then(function(t){throw new Error(t||'HTTP '+r.status);});return r.json();})
    .then(function(d){
      var pose='['+d.neutral.join(',')+']';
      if(d.imuCalibrated){updateCalBadge(true);calStatus.textContent='Saved home pose '+pose+' + IMU calibrated ('+d.n+' samples)';calStatus.style.color='#4ade80';toast('Calibrated position saved!');}
      else{calStatus.textContent='Saved home pose '+pose+'. IMU not calibrated'+(d.imuReady?'':' (IMU offline)')+' \u2014 tap Calibrate Now.';calStatus.style.color='#fbbf24';toast('Pose saved (calibrate IMU next)');}
      log('ok','savepose: neutral='+pose+' radius='+d.radius+' imucal='+d.imuCalibrated);
    }).catch(function(e){calStatus.textContent='Save failed: '+e.message;calStatus.style.color='#f87171';log('error','savepose: '+e.message);})
    .finally(function(){btn.disabled=false;});
  });

  liftBtn.addEventListener('click',function(){
    if(liftBusy) return;
    var turnOn=liftBtn.textContent.indexOf('ON')<0;
    liftBusy=true;liftBtn.disabled=true;
    var url='/liftdetect?on='+(turnOn?1:0);
    if(turnOn){url+='&thresh='+(parseInt(thrSlider.value)||4000);calStatus.textContent='Enabling \u2014 calibrating ~3 s, keep robot still!';calStatus.style.color='#fbbf24';}
    fetch(url).then(function(r){return r.json();}).then(function(d){
      renderLift(!!d.enabled);updateCalBadge(!!d.calOk);
      if(!turnOn){calStatus.textContent='Pick-up reaction OFF';calStatus.style.color='#94a3b8';toast('Reaction OFF');}
      else if(d.calOk){calStatus.textContent='Reaction ON \u2014 armed ('+d.n+' samples)';calStatus.style.color='#4ade80';toast('Reaction ON');}
      else{calStatus.textContent='Reaction ON, but calibration failed'+(d.imu?'':' (IMU offline)');calStatus.style.color='#f87171';}
      log('ok','liftdetect on='+(d.enabled?1:0)+' calOk='+(d.calOk?1:0));
    }).catch(function(e){calStatus.textContent='Toggle failed: '+e.message;calStatus.style.color='#f87171';})
    .finally(function(){liftBusy=false;liftBtn.disabled=false;});
  });
  fetch('/liftdetect').then(function(r){return r.json();}).then(function(d){renderLift(!!d.enabled);}).catch(function(){});

  helpMeBtn.addEventListener('click',function(){
    if(helpMeBusy) return;
    var turnOn=helpMeBtn.textContent.indexOf('ON')<0;
    helpMeBusy=true;helpMeBtn.disabled=true;
    fetch('/helpme?on='+(turnOn?1:0)).then(function(r){return r.json();}).then(function(d){
      renderHelpMe(!!d.enabled);toast('Help me sound '+(d.enabled?'ON':'OFF'));
    }).catch(function(e){log('error','helpme: '+e.message);})
    .finally(function(){helpMeBusy=false;helpMeBtn.disabled=false;});
  });
  fetch('/helpme').then(function(r){return r.json();}).then(function(d){renderHelpMe(!!d.enabled);}).catch(function(){});

  document.getElementById('bImuToggle').addEventListener('click',function(){
    paused=!paused;this.textContent=paused?'\u25B6 Resume':'\u2016 Pause';
    if(!paused) poll(); else clearTimeout(imuTimer);
  });

  poll();
}

/* ─────────────────────────────────────────────────────────
   IMU ORIENTATION CARD (Move tab) + DRIVE-BY-TILT
───────────────────────────────────────────────────────── */
(function(){
  var tiltBall=document.getElementById('tiltBall');
  var oPitch=document.getElementById('oPitch'),oRoll=document.getElementById('oRoll');
  var oPitchBar=document.getElementById('oPitchBar'),oRollBar=document.getElementById('oRollBar');
  var orientSt=document.getElementById('imuOrientStatus'),orientHint=document.getElementById('imuOrientHint');
  var moveBadge=document.getElementById('moveOrientBadge');
  var tiltBtn=document.getElementById('bTiltDrive');
  var tiltOn=false, tiltTimer=null;

  function updateOrient(d){
    var pitch=typeof d.pitch==='number'?d.pitch:0, roll=typeof d.roll==='number'?d.roll:0;
    oPitch.textContent=pitch.toFixed(1)+'\u00B0';oRoll.textContent=roll.toFixed(1)+'\u00B0';
    oPitchBar.style.width=(50+Math.max(-50,Math.min(50,pitch/45*50)))+'%';
    oRollBar.style.width=(50+Math.max(-50,Math.min(50,roll/45*50)))+'%';
    tiltBall.style.left=(50+Math.max(-90,Math.min(90,roll/45*90)))+'%';
    tiltBall.style.top=(50-Math.max(-90,Math.min(90,pitch/45*90)))+'%';
    if(moveBadge) moveBadge.textContent='P:'+pitch.toFixed(1)+'\u00B0 R:'+roll.toFixed(1)+'\u00B0';
    if(d.calOk){orientHint.textContent='Live \u2014 zeroed to calibrated pose';orientHint.style.color='#4ade80';orientSt.textContent=d.chip||'MPU';orientSt.style.color='#4ade80';}
    else{orientHint.textContent='Calibrate IMU first for zeroed values';orientHint.style.color='#64748b';orientSt.textContent='not calibrated';orientSt.style.color='#fbbf24';}
  }

  var poseTimer=null;
  function pollPose(){
    /* Only poll the orientation endpoint when its card is actually visible (the
       Move tab) or tilt-drive needs it, and never while the browser tab is
       hidden. Previously this ran 5×/s forever and shared the robot's slow
       single-threaded web server with everything else — the main cause of the
       laggy UI. Cheap 500 ms re-check so it resumes as soon as you come back. */
    if(document.hidden||(currentTab!=='move'&&!tiltOn)){poseTimer=setTimeout(pollPose,500);return;}
    fetch('/imupose').then(function(r){return r.json();}).then(function(d){updateOrient(d);})
    .catch(function(){if(orientSt)orientSt.textContent='offline';})
    .finally(function(){poseTimer=setTimeout(pollPose,200);});
  }
  pollPose();

  /* IMU move buttons (Move tab) */
  [['bImuLean','imu_lean','Lean Follow'],['bImuBalance','imu_balance','Balance'],
   ['bImuTiltWalk','imu_tiltwalk','Tilt Walk'],['bImuSwagger','imu_swagger','Swagger'],
   ['bImuSway','imu_sway','Sway']].forEach(function(m){
    var btn=document.getElementById(m[0]);if(!btn)return;
    btn.addEventListener('click',function(){
      btn.disabled=true;
      get('/move?cmd='+m[1],function(){log('ok',m[2]+' started');toast(m[2]+'!');},function(){log('error',m[2]+' failed');});
      setTimeout(function(){btn.disabled=false;},5000);
    });
  });

  function driveByTilt(){
    if(!tiltOn) return;
    fetch('/imupose').then(function(r){return r.json();}).then(function(d){
      if(!d.calOk||!tiltOn) return;
      var pitch=typeof d.pitch==='number'?d.pitch:0, roll=typeof d.roll==='number'?d.roll:0;
      var DEAD=8, cmd=null;
      if(Math.abs(pitch)>DEAD||Math.abs(roll)>DEAD){
        if(Math.abs(pitch)>=Math.abs(roll)){cmd=pitch>0?'imu_lean':'imu_balance';}
        else{cmd=roll>0?'imu_swagger':'imu_sway';}
      }
      if(cmd) get('/move?cmd='+cmd,function(){log('info','tilt-drive: '+cmd);},null);
    }).catch(null).finally(function(){if(tiltOn)tiltTimer=setTimeout(driveByTilt,800);});
  }

  tiltBtn.addEventListener('click',function(){
    tiltOn=!tiltOn;tiltBtn.textContent=tiltOn?'ON':'OFF';
    tiltBtn.style.background=tiltOn?'#166534':'#334155';tiltBtn.style.color=tiltOn?'#bbf7d0':'#cbd5e1';
    if(tiltOn){toast('Drive by Tilt ON \u2014 tilt to move!');log('head','Drive by Tilt enabled');driveByTilt();}
    else{clearTimeout(tiltTimer);toast('Drive by Tilt OFF');log('info','Drive by Tilt disabled');}
  });
})();

/* ─────────────────────────────────────────────────────────
   RECOVER SCREEN BUTTON
───────────────────────────────────────────────────────── */
(function(){
  var b=document.getElementById('bRecover');if(!b)return;
  b.addEventListener('click',function(){
    b.disabled=true;var old=b.innerHTML;b.innerHTML='Recovering\u2026';
    fetch('/recover').then(function(r){return r.text();}).then(function(t){
      toast('Screen recovered \u2192 Normal face');log('ok','Recover: '+t);
    }).catch(function(e){
      toast('Recover failed \u2014 if the robot is hung, power-cycle it');log('error','Recover failed: '+(e&&e.message?e.message:e));
    }).finally(function(){b.disabled=false;b.innerHTML=old;});
  });
})();

/* ─────────────────────────────────────────────────────────
   DEVICE LOG
───────────────────────────────────────────────────────── */
(function(){
  var out=document.getElementById('devLog');
  var autoBtn=document.getElementById('bLogAuto');
  var autoOn=false,logTimer=null;
  function refreshLog(){
    fetch('/logs').then(function(r){return r.text();}).then(function(t){out.textContent=t;out.scrollTop=out.scrollHeight;})
    .catch(function(){out.textContent='Could not reach robot \u2014 reconnect to Anvyxo_robo WiFi and tap Refresh.';})
    .finally(function(){if(autoOn)logTimer=setTimeout(refreshLog,3000);});
  }
  document.getElementById('bLogRefresh').addEventListener('click',refreshLog);
  autoBtn.addEventListener('click',function(){
    autoOn=!autoOn;autoBtn.textContent='Auto: '+(autoOn?'ON':'OFF');autoBtn.style.background=autoOn?'#a16207':'#2d2008';
    if(autoOn)refreshLog();else clearTimeout(logTimer);
  });
  document.getElementById('bLogCopy').addEventListener('click',function(){
    var t=out.textContent;
    var ta=document.createElement('textarea');ta.value=t;ta.style.position='fixed';ta.style.opacity='0';
    document.body.appendChild(ta);ta.focus();ta.select();
    var ok=false;try{ok=document.execCommand('copy');}catch(e){}
    document.body.removeChild(ta);
    if(ok){toast('Log copied \u2014 paste it to Claude!');log('ok','Device log copied to clipboard');}
    else{toast('Copy blocked \u2014 long-press the log text and copy manually');}
  });
  document.getElementById('bLogClear').addEventListener('click',function(){
    fetch('/logs?clear=1').then(function(){out.textContent='(cleared)';toast('Device log cleared');}).catch(function(){toast('Clear failed');});
  });
  refreshLog();
})();

/* ─────────────────────────────────────────────────────────
   HARDWARE STATUS + CAMERA
───────────────────────────────────────────────────────── */
(function(){
  var COL=['#eab308','#22c55e','#ef4444','#64748b'];
  var LBL=['starting\u2026','OK','NOT FOUND','no feedback'];
  var list=document.getElementById('hwList'),stepEl=document.getElementById('hwStep');
  var camState=document.getElementById('camState'),camMsg=document.getElementById('camMsg');
  var camImg=document.getElementById('camImg'),bCam=document.getElementById('bCam');
  var camS=0,camOn=false;
  function esc(t){return String(t).replace(/[&<>]/g,function(c){return{'&':'&amp;','<':'&lt;','>':'&gt;'}[c];});}
  function camUI(){
    camState.style.color=COL[camS];
    camState.textContent=camS===1?'ready':camS===2?'NOT FOUND':camS===0?'starting\u2026':'?';
    bCam.disabled=(camS!==1);bCam.style.opacity=bCam.disabled?.5:1;
    document.getElementById('bSnap').disabled=bCam.disabled;
    if(camS===2){camMsg.style.color='#ef4444';camMsg.textContent='Camera not detected \u2014 check the ribbon cable';}
    else if(!camOn){camMsg.style.color='#64748b';camMsg.textContent=camS===1?'Camera off':'Waiting for camera\u2026';}
  }
  function pollHw(){
    fetch('/hw').then(function(r){return r.json();}).then(function(d){
      list.innerHTML=d.items.map(function(it){
        return '<div style="display:flex;gap:8px;align-items:baseline">'+
          '<span style="color:'+COL[it.s]+';font-size:14px">&#9679;</span>'+
          '<span style="min-width:96px;color:var(--text);font-weight:700">'+esc(it.n)+'</span>'+
          '<span style="color:'+(it.s===2?'#f87171':'#94a3b8')+'">'+esc(it.i||LBL[it.s])+'</span></div>';
      }).join('');
      stepEl.textContent=d.ready?'':('starting: '+d.step);
      d.items.forEach(function(it){if(it.k==='cam')camS=it.s;});
      camUI();setTimeout(pollHw,d.ready?10000:1000);
    }).catch(function(){stepEl.textContent='robot not answering';stepEl.style.color='#ef4444';setTimeout(pollHw,3000);});
  }
  bCam.addEventListener('click',function(){
    camOn=!camOn;
    if(camOn){
      camImg.onerror=function(){camOn=false;camImg.style.display='none';camMsg.style.display='';camMsg.style.color='#ef4444';camMsg.textContent='Stream failed (port 81)';bCam.innerHTML='&#9654; Start camera';};
      camImg.src='http://'+location.hostname+':81/stream';
      camImg.style.display='';camMsg.style.display='none';bCam.innerHTML='&#9632; Stop camera';
    } else {
      camImg.onerror=null;camImg.removeAttribute('src');camImg.style.display='none';camMsg.style.display='';
      bCam.innerHTML='&#9654; Start camera';camUI();
    }
  });
  document.getElementById('bSnap').addEventListener('click',function(){window.open('/cam/snapshot?t='+Date.now(),'_blank');});
  pollHw();
})();

/* ─────────────────────────────────────────────────────────
   WIFI SETTINGS
───────────────────────────────────────────────────────── */
(function(){
  var ssidInp=document.getElementById('wifiSsid'),passInp=document.getElementById('wifiPass');
  var saveBtn=document.getElementById('bWifiSave'),statusEl=document.getElementById('wifiSaveStatus');
  var barEl=document.getElementById('wifiStatusBar'),hdrIp=document.getElementById('hdrIp');
  function renderStatus(d){
    var html='<span style="color:#4ade80">&#9679; AP: </span><strong style="color:var(--text)">'+(d.ap||'Anvyxo_robo')+'</strong> &nbsp;&middot;&nbsp; <span style="color:#86efac">'+(d.apIp||'192.168.4.1')+'</span>';
    if(d.homeOk){
      html+='&ensp;<span style="color:#4ade80">&#9679; Home: </span><strong style="color:var(--text)">'+d.homeSsid+'</strong> &nbsp;&middot;&nbsp; <span style="color:#7dd3fc">'+d.homeIp+'</span>';
      hdrIp.textContent=d.homeIp;hdrIp.style.color='#4ade80';ssidInp.value=d.homeSsid;
    } else if(d.homeSsid&&d.homeSsid.length){
      html+='&ensp;<span style="color:#fbbf24">&#9679; Home: </span><strong style="color:#fbbf24">'+d.homeSsid+'</strong> <span style="color:#f87171">(not connected)</span>';
      ssidInp.value=d.homeSsid;
    } else {
      html+='&ensp;<span style="color:#475569">&#9675; No home WiFi saved</span>';
    }
    barEl.innerHTML=html;
  }
  fetch('/wifistatus').then(function(r){return r.json();}).then(renderStatus).catch(function(){barEl.textContent='Could not reach robot';});
  saveBtn.addEventListener('click',function(){
    var ssid=ssidInp.value.trim(),pass=passInp.value;
    if(!ssid){ssidInp.focus();toast('Enter a WiFi name');return;}
    saveBtn.disabled=true;statusEl.textContent='Saving and connecting\u2026 (up to 15 s)';statusEl.style.color='#fbbf24';
    var body=new URLSearchParams();body.append('ssid',ssid);body.append('pass',pass);
    fetch('/wifisave',{method:'POST',body:body}).then(function(r){return r.text();}).then(function(t){
      statusEl.textContent=t;statusEl.style.color='#4ade80';toast('Saved!');
      setTimeout(function(){
        fetch('/wifistatus').then(function(r){return r.json();}).then(function(d){
          renderStatus(d);
          if(d.homeOk){statusEl.textContent='Connected! Home IP: '+d.homeIp;statusEl.style.color='#4ade80';passInp.value='';toast('Connected to '+d.homeSsid);log('ok','WiFi: joined '+d.homeSsid+' \u2192 '+d.homeIp);}
          else{statusEl.textContent='Join failed \u2014 check SSID / password (AP still up)';statusEl.style.color='#f87171';log('error','WiFi join failed for: '+ssid);}
        }).catch(function(){statusEl.textContent='Connected (AP may have changed IP)';statusEl.style.color='#fbbf24';});
      },3000);
    }).catch(function(e){statusEl.textContent='Request failed: '+e.message;statusEl.style.color='#f87171';log('error','wifisave: '+e.message);})
    .finally(function(){saveBtn.disabled=false;});
  });
  [ssidInp,passInp].forEach(function(el){el.addEventListener('keydown',function(e){if(e.key==='Enter')saveBtn.click();});});
})();

</script>
</body>
</html>
)rawhtml";
