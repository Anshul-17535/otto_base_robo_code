// Run against the editable firmware UI; no browser, network or robot access.
const fs=require('node:fs'),vm=require('node:vm'),assert=require('node:assert/strict');
const file=process.argv[2]||'../BOB_V2/Otto_ESP32S3/html_page.h';
const source=fs.readFileSync(file,'utf8');
const begin=source.indexOf('  // Median rejects isolated spikes;');
const end=source.indexOf('  function updateCalBadge',begin);
assert.ok(begin>=0&&end>begin);
function setup(){const elements={};const context={cube:{style:{}},pitchEl:{},rollEl:{},localStorage:{getItem:()=>null,setItem:()=>{}},document:{getElementById:id=>(elements[id]??={style:{},setAttribute:()=>{}}),addEventListener:()=>{}},requestAnimationFrame:()=>{}};vm.createContext(context);vm.runInContext(source.slice(begin,end),context);return context;}
const f=setup();f.aimCube(0,0);f.aimCube(0,0);f.aimCube(0,0);f.aimCube(70,90);assert.equal(f.tgtX,0);assert.equal(f.tgtY,0);f.aimCube(.05,-.05);assert.equal(f.tgtX,0);f.aimCube(NaN,0);assert.equal(f.tgtX,0);
const seam=setup();seam.aimCube(0,179);seam.aimCube(0,-179);assert.ok(Math.abs(seam.tgtY+179)<3);
function simulate(hz){const c=setup();c.aimCube(0,0);c.aimCube(30,20);c.aimCube(30,20);c.lastFrame=1000;for(let i=1;i<=hz;i++)c.animCube(1000+i*1000/hz);return c.curX;}
assert.ok(Math.abs(simulate(60)-simulate(120))<1e-8);
const flipped=setup();flipped.aimCube(12,15);flipped.animCube(1000);const before=flipped.cube.style.transform;flipped.document.getElementById('imuFlipRoll').onclick();flipped.animCube(1017);assert.notEqual(flipped.cube.style.transform,before);assert.equal(flipped.rollSign,-1);assert.equal(flipped.pitchSign,1);assert.match(flipped.cube.style.transform,/rotateZ\(15.00deg\)/);
console.log('Firmware cube: spike, deadband, invalid input, wraparound and FPS checks passed.');
