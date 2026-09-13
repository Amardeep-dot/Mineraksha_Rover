#pragma once
#include <Arduino.h>
const char DASHBOARD_HTML[] PROGMEM = R"MRKDASH(<!doctype html>
<html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>MINERAKSHA · Rescue Console</title>
<style>
:root{color-scheme:dark;--bg:#101317;--panel:#1a1f25;--edge:#303842;--text:#f1f4f7;--muted:#a7b2be;--accent:#ffbb55}
*{box-sizing:border-box}body{margin:0;background:var(--bg);color:var(--text);font:15px system-ui,sans-serif}main{max-width:1100px;margin:auto;padding:28px 20px}header{display:flex;align-items:center;justify-content:space-between;gap:16px;margin-bottom:24px}h1{font-size:28px;letter-spacing:.07em;margin:0}h2{font-size:17px;margin:0 0 18px}p{line-height:1.55}small,.muted{color:var(--muted)}.eyebrow{color:var(--accent);letter-spacing:.16em;font-size:11px;margin-bottom:9px}.grid{display:grid;grid-template-columns:repeat(3,1fr);gap:16px}.card{background:var(--panel);border:1px solid var(--edge);border-radius:12px;padding:20px}.wide{grid-column:1/-1}.half{grid-column:span 1}.big{font-size:30px;font-weight:650;margin:12px 0}.row{display:flex;align-items:center;gap:10px;flex-wrap:wrap}.row>*{min-width:0}input,button{font:inherit;border-radius:7px;padding:10px 12px;border:1px solid var(--edge)}input{background:#101317;color:var(--text);max-width:100%}input[type=number]{width:82px}input[type=range]{padding:0;accent-color:var(--accent);width:170px}button{background:#313a45;color:var(--text);cursor:pointer}button.primary{background:var(--accent);color:#19140d;font-weight:650}button:disabled{cursor:default;opacity:.45}button.status{opacity:1;font-weight:700}.warn{color:var(--accent)}.alert{color:#ff7878}.good{color:#75dbac}.pill{border:1px solid var(--edge);border-radius:40px;padding:7px 12px;font-size:12px}label{display:block;margin:12px 0}pre{white-space:pre-wrap;overflow-wrap:anywhere;color:#b7c7d7;font-size:12px;max-height:280px;overflow:auto}#log{max-height:160px;overflow:auto;white-space:pre-wrap;font:12px monospace}meter{width:180px}.stale .reading{opacity:.5}a{color:var(--accent)}.field{margin:12px 0}.note{font-size:13px;color:var(--muted)}#endpoint{flex:1;min-width:210px}@media(max-width:760px){.grid{grid-template-columns:1fr}header{align-items:flex-start;flex-direction:column}h1{font-size:24px}.wide{grid-column:auto}}
</style></head><body><main class="stale" id="console">
<header><div><div class="eyebrow">POLYGENESIS / SAFETY & RESCUE</div><h1>MINERAKSHA</h1><p class="muted">ESP32 #2 · Revision 4 · Bench console</p></div><span class="pill" id="connection">Disconnected</span></header>
<div class="grid">
<section class="card wide"><div class="row"><input id="endpoint" aria-label="WebSocket address" placeholder="ws://192.168.1.100:81/"><button id="connect" class="primary">Connect</button></div><p class="note">One connection carries readings, commands and audio. Use the ESP32 address printed in Serial Monitor. Keep this console on a trusted local network.</p><div id="network" class="muted">Waiting for telemetry</div></section>
<section class="card"><h2>MQ-135</h2><div class="big reading" id="mq135">— V</div><div class="note reading" id="mq135detail">ADS1115 A0</div><p class="warn note">Uncalibrated · no gas ppm</p></section>
<section class="card"><h2>MQ-2</h2><div class="big reading" id="mq2">— V</div><div class="note reading" id="mq2detail">ADS1115 A1</div><p class="warn note">Uncalibrated · no gas ppm</p></section>
<section class="card"><h2>MQ-7 / MQ-7B</h2><div class="big reading" id="mq7">— V</div><div class="note reading" id="mq7detail">ADS1115 A2</div><p class="warn note">Heater cycle unverified · CO ppm unavailable</p></section>
<section class="card"><h2>Temperature & humidity</h2><div class="big reading" id="temperature">— °C</div><div class="reading" id="humidity">— % RH</div><p class="note">DHT22 · refresh every 2.5 seconds</p></section>
<section class="card"><h2>Human presence indication</h2><button class="status" disabled id="presence">UNKNOWN</button><div class="big reading" id="distance">— m</div><p class="note">Radar target indication only. It does not identify a person or distinguish a victim. Distance is to a reported moving target; no X/Y/Z or target count.</p></section>
<section class="card"><h2>Victim status</h2><div id="victimStatus" class="big">Unconfirmed</div><div class="row"><button data-online id="markVictim" class="primary">Mark victim confirmed</button><button data-online id="clearVictim">Clear</button></div><p class="note">Operator confirmation from visual/audio evidence. The mark stays until cleared or the ESP32 restarts.</p></section>
<section class="card wide"><h2>Radar range & sensitivity</h2><div id="radarStatus" class="muted">Waiting for LD2420</div><div class="row">
<label>Min gate <input id="minGate" type="number" min="0" max="14" value="1"></label>
<label>Max gate <input id="maxGate" type="number" min="1" max="15" value="8"></label>
<label>Presence hold (s) <input id="hold" type="number" min="1" max="120" value="5"></label>
<label>Sensitivity <input id="sensitivity" type="range" min="0" max="100" value="50"> <span id="senseValue">50</span></label>
</div><p class="note">Each gate is about 0.7 m; gates are coarse detection zones, not a precise cutoff. Higher sensitivity lowers the module's move/still thresholds. 50 uses thresholds from the last Read radar or startup. Changes may affect false alarms and require room testing.</p>
<div class="row"><button data-online class="primary" id="applyRadar">Apply & verify</button><button data-online id="readRadar">Read radar</button></div><p id="actualRadar" class="note">Settings have not been read.</p>
<label class="muted">RF transmit power <input type="range" disabled aria-label="RF transmit power unavailable"></label><small>Unavailable: no verified power-control command for this LD2420 implementation. Sensitivity does not change RF transmit power.</small>
</section>
<section class="card wide"><h2>Rescue audio</h2><div class="row"><button data-online id="listen" class="primary">Listen to rover</button><button data-online id="talk">Start talking</button><button data-online id="stop">Stop audio</button><button data-online id="tone">Test speaker</button></div>
<p id="audioStatus">Audio stopped</p><div class="row"><label>Speaker volume <input id="volume" type="range" min="0" max="100" value="35"> <span id="volumeValue">35</span>%</label><label>Rover mic <meter id="micLevel" min="0" max="32768" value="0"></meter></label></div>
<div class="row"><input id="audioFile" type="file" accept="audio/*" aria-label="Choose an audio file"><button data-online id="playFile">Play file on rover</button></div>
<p class="note">Listen and Talk operate one at a time to reduce feedback. The rover uses its INMP441 microphone and MAX98357 speaker. Talk uses this computer's microphone. Allow microphone and local-network permissions when prompted.</p>
<p id="micContext" class="note"></p><p class="note">All audio uses this same WebSocket: 16 kHz mono PCM, not a second stream. Use headphones when the operator is near the rover.</p></section>
<section class="card wide"><h2>System status</h2><p class="warn">Bench measurements only · gas calibration unverified · motor run permission remains false.</p><div id="faults" class="muted">Waiting for device</div><details><summary>Latest complete telemetry</summary><pre id="raw">{}</pre></details><details open><summary>Command log</summary><div id="log"></div></details></section>
</div></main><script>
'use strict';
const $=id=>document.getElementById(id), RATE=16000, BLOCK=256;
let socket=null, wantedURL='', reconnectTimer=0, lastTelemetry=0, formLoaded=false;
let context=null, localMode='idle', stream=null, source=null, processor=null, mute=null;
let audioGeneration=0, playAt=0, actionBusy=false, lastRadar=null, linkFresh=false;
const pending=new Map(), scheduled=new Set();
function log(s){$('log').textContent=(new Date().toLocaleTimeString()+' '+s+'\n'+$('log').textContent).slice(0,6000)}
function connected(){return socket&&socket.readyState===WebSocket.OPEN}
function send(o){if(!connected())throw Error('WebSocket disconnected');socket.send(JSON.stringify(o))}
function command(cmd,values={}){return new Promise((resolve,reject)=>{
 if(pending.has(cmd)){reject(Error('Please wait for the previous '+cmd+' command'));return}
 const timer=setTimeout(()=>{pending.delete(cmd);reject(Error(cmd+' acknowledgement timed out'))},3000);
 pending.set(cmd,{resolve,reject,timer});try{send({cmd,...values})}catch(e){clearTimeout(timer);pending.delete(cmd);reject(e)}
})}
function setAvailable(ok){linkFresh=ok;document.querySelectorAll('[data-online]').forEach(b=>b.disabled=!ok);
 if(ok&&lastRadar){$('applyRadar').disabled=lastRadar.busy||!lastRadar.config_valid;$('readRadar').disabled=lastRadar.busy}
}
function stale(message){$('console').classList.add('stale');$('connection').textContent=message;setAvailable(false);
 $('presence').textContent='UNKNOWN';$('presence').className='status';$('distance').textContent='— m';
 ['mq135','mq2','mq7'].forEach(id=>{$(id).textContent='— V';$(id+'detail').textContent='No fresh reading'});
 $('temperature').textContent='— °C';$('humidity').textContent='— % RH';$('micLevel').value=0;$('victimStatus').textContent='Unknown';
}
function cleanupAudio(){audioGeneration++;localMode='idle';
 if(processor){processor.onaudioprocess=null;processor.disconnect();processor=null}
 if(source){source.disconnect();source=null}if(mute){mute.disconnect();mute=null}
 if(stream){stream.getTracks().forEach(t=>t.stop());stream=null}
 scheduled.forEach(s=>{try{s.stop()}catch(e){}});scheduled.clear();
 playAt=0;$('audioStatus').textContent='Audio stopped';$('talk').textContent='Start talking';
}
function connect(){
 const v=$('endpoint').value.trim();let u;try{u=new URL(v)}catch(e){log('Enter ws://ESP32-IP:81/');return}
 if(u.protocol!=='ws:'){log('Use ws://ESP32-IP:81/ on this local bench network');return}
 wantedURL=u.href;clearTimeout(reconnectTimer);cleanupAudio();
 if(socket){socket.onclose=null;socket.close()}
 for(const p of pending.values()){clearTimeout(p.timer);p.reject(Error('Reconnecting'))}pending.clear();
 stale('Connecting…');formLoaded=false;lastTelemetry=0;
 socket=new WebSocket(wantedURL);socket.binaryType='arraybuffer';const current=socket;
 socket.onopen=()=>{if(current!==socket)return;log('Connected to '+wantedURL);$('connection').textContent='Waiting for telemetry'};
 socket.onerror=()=>{if(current===socket)log('Connection error. Check IP, Wi-Fi and local-network permission.')};
 socket.onclose=()=>{if(current!==socket)return;cleanupAudio();stale('Disconnected');
  for(const p of pending.values()){clearTimeout(p.timer);p.reject(Error('Disconnected'))}pending.clear();
  reconnectTimer=setTimeout(connect,2500);
 };
 socket.onmessage=e=>{
  if(current!==socket)return;
  if(e.data instanceof ArrayBuffer){playMicrophone(e.data);return}
  let j;try{j=JSON.parse(e.data)}catch(err){return}
  if(j.type==='ack'){log((j.ok?'OK ':'ERROR ')+j.command+': '+j.message);
   const p=pending.get(j.command);if(p){clearTimeout(p.timer);pending.delete(j.command);j.ok?p.resolve(j):p.reject(Error(j.message))}
  }else if(j.type==='telemetry')render(j);
 };
}
function render(j){
 lastTelemetry=performance.now();lastRadar=j.radar;$('console').classList.remove('stale');$('connection').textContent='LIVE';setAvailable(true);
 $('network').textContent='ESP32 '+j.wifi.ip+' · Wi-Fi '+j.wifi.rssi_dbm+' dBm · uptime '+Math.floor(j.uptime_ms/1000)+' s';
 ['mq135','mq2','mq7'].forEach(id=>{const g=j.gas[id];$(id).textContent=g.valid_adc_reading?g.adc_v.toFixed(3)+' V':'Unavailable';
  $(id+'detail').textContent=g.valid_adc_reading?'ADC raw '+g.raw+' · estimated AO '+g.ao_estimate_v.toFixed(3)+' V'+(g.input_near_rail?' · INPUT NEAR RAIL':''):'ADC unavailable';
 });
 $('temperature').textContent=j.climate.valid?j.climate.temperature_c.toFixed(1)+' °C':'Unavailable';
 $('humidity').textContent=j.climate.valid?j.climate.humidity_pct.toFixed(1)+' % RH':'Unavailable';
 const r=j.radar;$('presence').textContent=r.presence===null?'UNKNOWN':r.presence?'PRESENCE DETECTED':'NO TARGET REPORTED';
 $('presence').className='status '+(r.presence===true?'warn':'');$('distance').textContent=r.distance_cm===null?'— m':(r.distance_cm/100).toFixed(2)+' m';
 $('radarStatus').textContent='LD2420 '+r.firmware+' · '+r.status+' · '+(r.fresh?'reports fresh':'no fresh presence report');
 if(r.config_valid){
  $('actualRadar').textContent='Verified: gates '+r.min_gate+'–'+r.max_gate+' · hold '+r.hold_s+' s · relative sensitivity '+r.sensitivity;
  if(!formLoaded){$('minGate').value=r.min_gate;$('maxGate').value=r.max_gate;$('hold').value=r.hold_s;$('sensitivity').value=r.sensitivity;$('senseValue').textContent=r.sensitivity;formLoaded=true}
 }else $('actualRadar').textContent='Configuration not verified; the displayed input fields are proposed values only.';
 $('victimStatus').textContent=j.victim.operator_confirmed?'CONFIRMED':'Unconfirmed';
 $('micLevel').value=j.audio.mic_peak;
 $('faults').textContent='ADC '+(j.ads1115_online?'online':'offline')+' · I2S '+(j.audio.initialized?'initialized':'failed')+' · free heap '+j.free_heap+' bytes · ADC errors '+j.adc_errors+' · audio errors '+j.audio.i2s_errors+' · dropped mic/speaker blocks '+j.audio.mic_dropped_blocks+'/'+j.audio.speaker_dropped_blocks;
 $('raw').textContent=JSON.stringify(j,null,2);
 // If ESP32 expired our audio lease, stop the computer microphone too.
 if(localMode!=='idle'&&!actionBusy&&j.audio.mode===0)cleanupAudio();
}
async function ensureContext(){
 if(!context)context=new (window.AudioContext||window.webkitAudioContext)({sampleRate:RATE});
 if(context.state==='suspended')await context.resume();return context;
}
function playMicrophone(buffer){
 if(localMode!=='listen'||!context||buffer.byteLength!==513)return;
 const view=new DataView(buffer);if(view.getUint8(0)!==1)return;
 // Drop old queued sound on a slow/hidden tab instead of accumulating latency.
 if(playAt>context.currentTime+0.25)return;
 const b=context.createBuffer(1,BLOCK,RATE), samples=b.getChannelData(0);
 for(let i=0;i<BLOCK;i++)samples[i]=view.getInt16(1+2*i,true)/32768;
 const s=context.createBufferSource();s.buffer=b;s.connect(context.destination);
 if(playAt<context.currentTime)playAt=context.currentTime+0.045;
 scheduled.add(s);s.onended=()=>{scheduled.delete(s);s.disconnect()};s.start(playAt);playAt+=BLOCK/RATE;
}
function sendSamples(samples){
 if(!connected()||localMode!=='talk'||socket.bufferedAmount>8192)return;
 const bytes=new ArrayBuffer(513), view=new DataView(bytes);view.setUint8(0,2);
 for(let i=0;i<BLOCK;i++){const s=Math.max(-1,Math.min(1,samples[i]||0));view.setInt16(1+2*i,s<0?s*32768:s*32767,true)}socket.send(bytes);
}
async function action(fn){if(actionBusy)return;actionBusy=true;try{await fn()}catch(e){log(e.message);cleanupAudio();if(connected())command('audio',{mode:'idle'}).catch(()=>{})}finally{actionBusy=false}}
async function stop(){cleanupAudio();if(connected())await command('audio',{mode:'idle'})}
async function listen(){await stop();await ensureContext();await command('audio',{mode:'listen'});localMode='listen';$('audioStatus').textContent='Listening to rover microphone'}
async function talk(){
 if(!window.isSecureContext||!navigator.mediaDevices?.getUserMedia)throw Error('Talk needs the local dashboard launcher (localhost) or a browser that permits the downloaded HTML file. See README.');
 await stop();await ensureContext();const generation=audioGeneration;
 const acquired=await navigator.mediaDevices.getUserMedia({audio:{channelCount:1,echoCancellation:true,noiseSuppression:true,autoGainControl:true}});
 if(generation!==audioGeneration){acquired.getTracks().forEach(t=>t.stop());return}
 stream=acquired;await command('audio',{mode:'talk'});localMode='talk';
 source=context.createMediaStreamSource(stream);processor=context.createScriptProcessor(1024,1,1);mute=context.createGain();mute.gain.value=0;
 // Fractional resampling state persists between callbacks if the device ignores 16kHz.
 let carry=[],position=0,out=[];const ratio=context.sampleRate/RATE;
 processor.onaudioprocess=e=>{
  if(localMode!=='talk')return;carry.push(...e.inputBuffer.getChannelData(0));
  while(position+ratio<=carry.length){
   let sum=0,weight=0,p=position,end=position+ratio;
   while(p<end){const k=Math.floor(p),w=Math.min(k+1,end)-p;sum+=carry[k]*w;weight+=w;p+=w}
   out.push(sum/weight);position=end;
   if(out.length===BLOCK){sendSamples(out);out=[]}
  }
  const consumed=Math.floor(position);carry=carry.slice(consumed);position-=consumed;
 };
 source.connect(processor);processor.connect(mute);mute.connect(context.destination);
 $('audioStatus').textContent='Talking to rover · click Stop audio to release microphone';$('talk').textContent='Talking…';
}
const pause=ms=>new Promise(resolve=>setTimeout(resolve,ms));
async function playFile(){
 const file=$('audioFile').files[0];if(!file)throw Error('Choose an audio file first');
 if(file.size>10*1024*1024)throw Error('Choose a file smaller than 10 MB');
 await stop();await ensureContext();const generation=audioGeneration;
 const decoded=await context.decodeAudioData(await file.arrayBuffer());
 if(decoded.duration>120)throw Error('Use an audio clip shorter than two minutes');
 const offline=new OfflineAudioContext(1,Math.ceil(decoded.duration*RATE),RATE), node=offline.createBufferSource();
 node.buffer=decoded;node.connect(offline.destination);node.start();const result=await offline.startRendering();
 if(generation!==audioGeneration)return;
 await command('audio',{mode:'talk'});localMode='talk';$('audioStatus').textContent='Playing '+file.name+' on rover';
 const samples=result.getChannelData(0), begin=performance.now();
 for(let offset=0;offset<samples.length;offset+=BLOCK){
  if(generation!==audioGeneration||!connected()||localMode!=='talk')return;
  const due=begin+1000*offset/RATE,wait=due-performance.now();if(wait>0)await pause(wait);
  if(generation!==audioGeneration||localMode!=='talk')return;
  // Skip excessively late blocks rather than send a burst after tab suspension.
  if(performance.now()-due<120)sendSamples(samples.subarray(offset,offset+BLOCK));
 }
 await pause(250);if(generation===audioGeneration)await stop();
}
$('connect').onclick=connect;
$('listen').onclick=()=>action(listen);$('talk').onclick=()=>action(talk);
$('stop').onclick=()=>{cleanupAudio();if(connected())command('audio',{mode:'idle'}).catch(e=>log(e.message))};
$('tone').onclick=()=>action(async()=>{await stop();await command('audio',{mode:'tone'});$('audioStatus').textContent='Playing a short 660 Hz speaker test'});
$('playFile').onclick=()=>action(playFile);
$('markVictim').onclick=()=>command('victim',{confirmed:true}).catch(e=>log(e.message));
$('clearVictim').onclick=()=>command('victim',{confirmed:false}).catch(e=>log(e.message));
$('sensitivity').oninput=()=>{$('senseValue').textContent=$('sensitivity').value};
$('volume').oninput=()=>{$('volumeValue').textContent=$('volume').value};
$('volume').onchange=()=>command('volume',{value:Number($('volume').value)}).catch(e=>log(e.message));
$('applyRadar').onclick=()=>{
 const lo=Number($('minGate').value),hi=Number($('maxGate').value),hold=Number($('hold').value),s=Number($('sensitivity').value);
 if(![lo,hi,hold,s].every(Number.isInteger)||lo<0||lo>=hi||hi>15||hold<1||hold>120){log('Use 0 ≤ min gate < max gate ≤ 15 and hold 1–120 s');return}
 command('radar_config',{min_gate:lo,max_gate:hi,hold_s:hold,sensitivity:s}).then(()=>{formLoaded=false}).catch(e=>log(e.message));
};
$('readRadar').onclick=()=>command('radar_read').then(()=>{formLoaded=false}).catch(e=>log(e.message));
setInterval(()=>{if(connected()){try{send({cmd:'ping'})}catch(e){}}if(lastTelemetry&&performance.now()-lastTelemetry>3000){cleanupAudio();stale('Telemetry stale')}},2000);
document.addEventListener('visibilitychange',()=>{if(document.hidden){cleanupAudio();if(connected())command('audio',{mode:'idle'}).catch(()=>{})}});
window.addEventListener('pagehide',()=>{cleanupAudio();if(connected())socket.close()});
$('micContext').textContent=window.isSecureContext?'Browser context supports microphone permission.':'This ESP32 HTTP page supports Listen and file playback. To Talk, run start_dashboard.py on your computer and use the localhost page.';
const urlParam=new URLSearchParams(location.search).get('ws');
if(urlParam)$('endpoint').value=urlParam;
else if(location.hostname&&location.hostname!=='localhost'&&location.hostname!=='127.0.0.1')$('endpoint').value='ws://'+location.hostname+':81/';
stale('Disconnected');if($('endpoint').value)connect();
</script></body></html>
)MRKDASH";
