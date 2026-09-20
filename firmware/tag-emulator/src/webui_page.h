#pragma once
#include <pgmspace.h>

static const char INDEX_HTML[] PROGMEM = R"HTML(
<!doctype html><meta charset=utf-8>
<meta name=viewport content="width=device-width,initial-scale=1">
<title>UWB link</title>
<style>
:root{--bg:#0f1115;--fg:#e6e8ee;--dim:#8b93a7;--ok:#3ddc84;--bad:#ff5f56;--acc:#5b9dff;--card:#171a21}
body{margin:0;background:var(--bg);color:var(--fg);font:14px/1.5 system-ui,sans-serif;padding:16px}
h1{font-size:15px;margin:0 0 12px;color:var(--dim);font-weight:500}
.wrap{max-width:820px;margin:0 auto}
.card{background:var(--card);border-radius:10px;padding:14px;margin-bottom:12px}
.big{font-size:44px;font-weight:600;letter-spacing:-1px}
.big small{font-size:16px;color:var(--dim);font-weight:400}
.grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(110px,1fr));gap:12px}
.k{color:var(--dim);font-size:11px;text-transform:uppercase;letter-spacing:.06em}
.v{font-size:19px;font-variant-numeric:tabular-nums}
canvas{width:100%;height:170px;display:block}
label{display:block;margin:10px 0 4px;color:var(--dim);font-size:12px}
input,select,button{background:#0f1115;color:var(--fg);border:1px solid #2a2f3a;border-radius:6px;padding:7px 9px;font:inherit}
input[type=range]{padding:0;width:100%}
button{cursor:pointer;border-color:#3a4152}
button:hover{border-color:var(--acc)}
.row{display:flex;gap:10px;flex-wrap:wrap;align-items:flex-end}
.pill{display:inline-block;padding:2px 8px;border-radius:99px;font-size:12px}
.on{background:rgba(61,220,132,.15);color:var(--ok)}
.off{background:rgba(255,95,86,.15);color:var(--bad)}
.note{color:var(--dim);font-size:12px;margin-top:8px}
table{width:100%;border-collapse:collapse;font-size:13px}
td{padding:3px 0}td:last-child{text-align:right;font-variant-numeric:tabular-nums}
</style>
<div class=wrap>
<h1>UWB link test — <span id=host></span> · <span id=role></span> · <span id=radio class=pill></span></h1>

<div class=card>
  <div class=big><span id=dist>—</span><small> m</small></div>
  <div class=note id=age></div>
</div>

<div class=card><canvas id=chart></canvas>
  <div class=note>Last 120 exchanges. Gaps are failed exchanges. RSSI, first path and
  clock offset are averaged over the window — single exchanges are far too noisy to calibrate against.
  <b>First path · live</b> averages only the last 16 exchanges, so it responds within a
  couple of seconds — use that one when aiming or repositioning a board.</div>
</div>

<div class="card grid">
  <div><div class=k>Mean</div><div class=v id=mean>—</div></div>
  <div><div class=k>Std dev</div><div class=v id=sd>—</div></div>
  <div><div class=k>Min</div><div class=v id=min>—</div></div>
  <div><div class=k>Max</div><div class=v id=max>—</div></div>
  <div><div class=k>Success</div><div class=v id=succ>—</div></div>
  <div><div class=k>Rate</div><div class=v id=rate>—</div></div>
  <div><div class=k>RSSI</div><div class=v id=rssi>—</div></div>
  <div><div class=k>First path</div><div class=v id=fp>—</div></div>
  <div><div class=k>First path · live</div><div class=v id=fpfast style=color:var(--acc)>—</div></div>
  <div><div class=k>Clock offset</div><div class=v id=ppm>—</div></div>
</div>

<div class=card>
  <div class=k style=margin-bottom:6px>Counters</div>
  <table>
    <tr><td>Successful</td><td id=c_ok>0</td></tr>
    <tr><td>Timeout (no reply)</td><td id=c_to>0</td></tr>
    <tr><td>RX error</td><td id=c_rx>0</td></tr>
    <tr><td>Bad / unexpected frame</td><td id=c_bf>0</td></tr>
  </table>
  <div class=note id=nlos></div>
  <div style=margin-top:10px><button onclick=reset()>Reset counters</button>
  <button onclick="if(confirm(&quot;Reboot this board?&quot;))fetch(&quot;/api/reboot&quot;,{method:&quot;POST&quot;})">Reboot</button></div>
</div>

<div class=card>
  <div class=k style=margin-bottom:6px>Settings</div>
  <div class=row>
    <div><label>Role</label>
      <select id=s_role><option value=0>Initiator</option><option value=1>Responder</option></select></div>
    <div><label>PHY profile</label>
      <select id=s_phy><option value=0>Short — 6.8M / 128</option><option value=1>Long — 850k / 1024</option><option value=2>Max — 850k / 2048</option></select></div>
    <div><label>Interval (ms)</label><input id=s_int type=number min=20 max=5000 style=width:90px></div>
    <div><label>Hostname</label><input id=s_host style=width:130px></div>
    <button onclick=save()>Apply</button>
  </div>
  <label>Antenna delay: <b id=s_adv></b> <span class=note>(changing this shifts all distances)</span></label>
  <input id=s_ad type=range min=15800 max=17000 step=1 oninput="s_adv.textContent=this.value">
  <div class=note>Put the boards a known distance apart, then trim until the reading matches.
  Measured on this rig: 1 unit &asymp; 9.5 mm when changed on <em>both</em> boards
  (~4.8 mm each). Recalibrate after changing PHY profile — the group delay differs.
  Applying a change resets the counters.</div>
</div>

<div class=card>
  <div class=k style=margin-bottom:6px>Simulated bins</div>
  <div class=note>Stands in for the LIS3DH interrupt. A moving tag ranges on the
  fast tick; otherwise it only proves it is alive on the slow tick.</div>
  <table id=t_tags style=margin-top:10px><tbody></tbody></table>
  <div class=row style=margin-top:12px>
    <div><label>Motion tick (ms)</label><input id=t_mtick type=number style=width:100px></div>
    <div><label>Idle tick (ms)</label><input id=t_itick type=number style=width:110px></div>
    <div><label>Motion hold (ms)</label><input id=t_mhold type=number style=width:110px></div>
    <div><label>Battery drain &times;</label><input id=t_drain type=number style=width:90px></div>
    <button onclick=saveMotion()>Apply</button>
    <button onclick=resetBatt()>Reset batteries</button>
  </div>
</div>

<div class=card>
  <div class=k style=margin-bottom:6px>Firmware update</div>
  <form method=POST action=/update enctype=multipart/form-data>
    <input type=file name=f accept=.bin required>
    <button type=submit>Upload</button>
  </form>
  <div class=note>Ranging halts during the update and the board reboots afterwards.</div>
</div>
</div>

<script>
const $=i=>document.getElementById(i);
const f=(v,d,u)=>v==null?'—':v.toFixed(d)+(u||'');
let touched=false;
['s_role','s_int','s_host','s_ad','s_phy'].forEach(i=>$(i).addEventListener('input',()=>touched=true));

async function tick(){
  let r; try{ r=await (await fetch('/api/stats')).json(); }catch(e){ return; }
  $('host').textContent=r.host; $('role').textContent=r.role?'Responder':'Initiator';
  const rad=$('radio'); rad.textContent=r.radio?'radio ok':'radio fault';
  rad.className='pill '+(r.radio?'on':'off');

  $('dist').textContent = r.last_dist==null ? '—' : r.last_dist.toFixed(2);
  $('age').textContent = r.last_age==null?'no successful exchange yet'
      : r.role? 'responder does not compute distance — see RSSI'
      : 'updated '+(r.last_age/1000).toFixed(1)+'s ago';

  $('mean').textContent=f(r.mean,2,' m'); $('sd').textContent=f(r.sd,3,' m');
  $('min').textContent=f(r.min,2,' m');   $('max').textContent=f(r.max,2,' m');
  $('succ').textContent=f(r.success,1,'%'); $('rate').textContent=f(r.rate,1,' Hz');
  $('rssi').textContent=f(r.rssi,1,' dBm'); $('fp').textContent=f(r.fp,1,' dBm');
  $('ppm').textContent=f(r.ppm,2,' ppm');
  $('fpfast').textContent=f(r.fp_fast,1,' dBm');
  $('c_ok').textContent=r.ok; $('c_to').textContent=r.timeout;
  $('c_rx').textContent=r.rx_error; $('c_bf').textContent=r.bad_frame;

  if(r.rssi!=null&&r.fp!=null){const d=r.rssi-r.fp;
    $('nlos').textContent='RSSI − first path = '+d.toFixed(1)+' dB — '+
      (d>6?'suggests obstructed / non-line-of-sight':'consistent with line of sight');}

  if(!touched){$('s_role').value=r.role;$('s_int').value=r.interval;$('s_phy').value=r.phy;
    $('s_host').value=r.host;$('s_ad').value=r.antdly;$('s_adv').textContent=r.antdly;}
  renderTags(r);
  draw(r.chart);
}
function draw(d){
  const c=$('chart'),x=c.getContext('2d'),W=c.width=c.clientWidth*2,H=c.height=340;
  x.clearRect(0,0,W,H); if(!d||!d.length)return;
  const v=d.filter(p=>p!=null); if(!v.length)return;
  let lo=Math.min(...v),hi=Math.max(...v); if(hi-lo<0.1){hi+=.05;lo-=.05;}
  const pad=(hi-lo)*.1; lo-=pad; hi+=pad;
  const X=i=>i/(d.length-1)*W, Y=p=>H-(p-lo)/(hi-lo)*H;
  x.strokeStyle='#2a2f3a';x.lineWidth=1;
  for(let i=0;i<=4;i++){const y=i/4*H;x.beginPath();x.moveTo(0,y);x.lineTo(W,y);x.stroke();
    x.fillStyle='#8b93a7';x.font='18px system-ui';x.fillText((hi-(hi-lo)*i/4).toFixed(2),4,y+18);}
  x.strokeStyle='#5b9dff';x.lineWidth=3;x.beginPath();let up=false;
  d.forEach((p,i)=>{if(p==null){up=false;return;}
    if(!up){x.moveTo(X(i),Y(p));up=true;}else x.lineTo(X(i),Y(p));});
  x.stroke();
  x.fillStyle='#ff5f56';
  d.forEach((p,i)=>{if(p==null)x.fillRect(X(i)-1.5,0,3,H);});
}
async function save(){
  const q=new URLSearchParams({role:$('s_role').value,interval:$('s_int').value,
    antdly:$('s_ad').value,host:$('s_host').value,phy:$('s_phy').value});
  await fetch('/api/config?'+q,{method:'POST'}); touched=false; tick();
}
async function reset(){ await fetch('/api/reset',{method:'POST'}); tick(); }
let ttouched=false;
['t_mtick','t_itick','t_mhold','t_drain'].forEach(i=>$(i).addEventListener('input',()=>ttouched=true));
async function motion(id,settle){
  await fetch('/api/motion?id='+id+(settle?'&settle=1':''),{method:'POST'});
  tick();
}
async function resetBatt(){
  await fetch('/api/config?battery_reset=1',{method:'POST'}); tick();
}
async function saveMotion(){
  const q=new URLSearchParams({motion_tick_ms:$('t_mtick').value,
    idle_tick_ms:$('t_itick').value,motion_hold_ms:$('t_mhold').value,
    drain_mult:$('t_drain').value});
  await fetch('/api/config?'+q,{method:'POST'}); ttouched=false; tick();
}
function renderTags(r){
  if(!r.tags) return;
  const b=$('t_tags').querySelector('tbody');
  b.innerHTML = r.tags.map(t=>
    '<tr><td>'+t.id+'</td>'+
    '<td><span class="pill '+(t.moving?'on':'off')+'">'+(t.moving?'moving':'still')+'</span></td>'+
    '<td>'+t.wakes+' wakes</td>'+
    '<td>'+(t.batt_mv/1000).toFixed(3)+' V</td>'+
    '<td style=text-align:right>'+
      '<button onclick="motion(\''+t.id+'\',false)">Move</button> '+
      '<button onclick="motion(\''+t.id+'\',true)">Settle</button></td></tr>').join('');
  if(!ttouched){
    $('t_mtick').value=r.motion_tick_ms; $('t_itick').value=r.idle_tick_ms;
    $('t_mhold').value=r.motion_hold_ms; $('t_drain').value=r.drain_mult;
  }
}
setInterval(tick,500); tick();
</script>
)HTML";
