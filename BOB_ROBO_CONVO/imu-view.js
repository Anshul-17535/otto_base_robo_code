/* Display-only filter. Unwrap angles before median filtering to avoid a
   358-degree sweep at the roll seam. Sensor values and control loops unchanged. */
class OrientationFilter {
  constructor(){this.history=[[],[]];this.target=[0,0];this.value=[0,0];this.ready=false;this.velocity=[0,0];}
  push(pitch,roll){
    if(!Number.isFinite(pitch)||!Number.isFinite(roll))return false;
    [pitch,roll].forEach((v,i)=>{
      const h=this.history[i];
      const previous=h.length?h[h.length-1]:v;
      v=previous+((v-previous+180)%360+360)%360-180;
      h.push(v);if(h.length>3)h.shift();
      const median=[...h].sort((a,b)=>a-b)[Math.floor(h.length/2)];
      if(!this.ready||Math.abs(median-this.target[i])>0.12)this.target[i]=median;
    });
    if(!this.ready){this.value=[...this.target];this.ready=true;}
    return true;
  }
  step(dt,tau){dt=Math.min(Math.max(dt,0),.1);const w=2/tau,e=Math.exp(-w*dt);this.value=this.value.map((v,i)=>{const d=v-this.target[i],j=this.velocity[i]+w*d;this.velocity[i]=(this.velocity[i]-w*j*dt)*e;return this.target[i]+(d+j*dt)*e;});return this.value;}
}
if(typeof module!=='undefined')module.exports={OrientationFilter};
if(typeof document!=='undefined'){
  const filter=new OrientationFilter(),el=id=>document.getElementById(id);
  let last=0,lastSample=0,tau=.20,back=false,valid=false;
  let pitchSign=1,rollSign=1;
  try{pitchSign=localStorage.getItem('imuPitchReverse')==='1'?-1:1;rollSign=localStorage.getItem('imuRollReverse')==='1'?-1:1;}catch(_){}
  function directions(){['Pitch','Roll'].forEach(axis=>{const b=el('flip'+axis),on=(axis==='Pitch'?pitchSign:rollSign)<0;b.textContent='Reverse '+axis.toLowerCase()+': '+(on?'ON':'OFF');b.setAttribute('aria-pressed',String(on));});}
  ['Pitch','Roll'].forEach(axis=>{el('flip'+axis).onclick=()=>{if(axis==='Pitch')pitchSign*=-1;else rollSign*=-1;try{localStorage.setItem('imu'+axis+'Reverse',(axis==='Pitch'?pitchSign:rollSign)<0?'1':'0');}catch(_){}directions();};});directions();
  el('smooth').onchange=()=>{tau=Number(el('smooth').value);};
  el('view').onclick=()=>{back=!back;el('camera').style.transform=`rotateX(-18deg) rotateY(${back?152:-28}deg)`;el('view').textContent=back?'View front':'View back';};
  el('clean').onclick=()=>document.body.classList.add('clean');
  el('exit').onclick=()=>document.body.classList.remove('clean');
  document.addEventListener('keydown',e=>{if(e.key==='Escape')document.body.classList.remove('clean');});
  function frame(t){
    const [p,r]=filter.step(last?(t-last)/1000:0,tau);last=t;
    // Pitch tilts forward/back; roll banks side-to-side. No invented yaw.
    el('cube').style.transform=`rotateX(${-p*pitchSign}deg) rotateZ(${-r*rollSign}deg)`;
    if(filter.ready){el('pitch').textContent=p.toFixed(1)+'°';el('roll').textContent=r.toFixed(1)+'°';}
    if(valid&&performance.now()-lastSample>2500){el('status').textContent='Signal stale · holding pose';valid=false;}
    requestAnimationFrame(frame);
  }
  async function poll(){
    if(!document.hidden){
      try{
        const response=await fetch('/api/imu',{signal:AbortSignal.timeout(3500),cache:'no-store'});
        if(!response.ok)throw new Error('unavailable');
        const d=await response.json();
        if(d.busy){el('status').textContent='Robot busy · holding pose';valid=false;}
        else if(d.ok&&filter.push(d.pitch,d.roll)){lastSample=performance.now();valid=true;el('status').textContent=d.calOk?'LIVE · calibrated':'LIVE · uncalibrated';}
        else {valid=false;el('status').textContent='IMU offline · holding pose';}
      }catch(_){valid=false;el('status').textContent='Disconnected · holding pose';}
    }
    setTimeout(poll,200);
  }
  requestAnimationFrame(frame);poll();
}
