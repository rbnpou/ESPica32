#pragma once
// Panel de control de la pagina web (sintonia, modo, volumen). Se muestra solo con la radio en tu WiFi.

static const char CTRL_CSS[] PROGMEM = R"KIWI(
#big{font-size:34px;font-weight:bold;font-family:monospace;text-align:center;margin:4px 0}
#sub{text-align:center;color:#444;margin-bottom:8px;min-height:1.3em}
.chips{display:flex;flex-wrap:wrap;gap:4px;margin:6px 0}
.chips button{flex:1 1 auto}
button.on{background:#2a6df4;color:#fff;border-color:#2a6df4}
#fi{flex:1;min-width:0;text-align:center;font-size:20px}
input[type=range]{width:100%;box-sizing:border-box;margin:6px 0;padding:0}
.lbl{font-size:13px;color:#555;margin-top:8px}
pre#dg{white-space:pre-wrap;font-size:12px;background:#f4f4f4;padding:8px;border-radius:6px;overflow-x:auto}
)KIWI";

static const char CTRL_HTML[] PROGMEM = R"KIWI(
<fieldset><legend>Control</legend>
<div id=big>---</div><div id=sub>...</div>
<div class=row><button onclick="d(-1)">&minus;</button>
<input type=text inputmode=decimal id=fi onkeydown="if(event.key=='Enter')go()">
<button onclick="d(1)">+</button><button onclick="go()">Ir</button></div>
<div class=lbl>Paso</div><div class=chips id=ss></div>
<div class=lbl>Modo</div><div class=chips id=mm></div>
<div class=lbl>Volumen: <b id=vl></b></div>
<input type=range id=vol min=0 max=100 oninput="vch()">
<div class=lbl>Bandas</div><div class=chips id=pp></div>
<div class=lbl>Buffer de audio: m&iacute;nimo (no baja de este valor; m&aacute;s retardo si es alto)</div><div class=chips id=bb></div>
<div class=chips><button id=ba onclick="bauto()">Subir solo si hay cortes</button>
<button id=ad onclick="adp()">Audio comprimido ADPCM (Kiwis lejanos)</button></div>
<div class=lbl id=bi></div>
</fieldset>
<fieldset><legend>Diagn&oacute;stico</legend><details id=dd><summary>Cortes de audio: red, ESP32 o buffer</summary>
<pre id=dg>...</pre><button onclick="dr()">Reiniciar contadores</button></details></fieldset>
<script>
var S=[10,100,1000,5000,10000],SL=['10 Hz','100 Hz','1 kHz','5 kHz','10 kHz'],
M=['LSB','USB','AM','CW','FM'],BF=[100,200,300,400,500,600,800,1000,1500,2000],
cur={f:0,m:0,s:1,v:0,ok:0,d:-127,st:''},vt=0,busy=0;
function $(i){return document.getElementById(i)}
function chips(id,a,fn){var h='';a.forEach(function(x,i){h+='<button id='+id+i+' onclick="'+fn+'('+i+')">'+x+'</button>'});$(id).innerHTML=h}
chips('ss',SL,'ssel');chips('mm',M,'msel');chips('bb',BF.map(function(v){return v+' ms'}),'bsel');chips('pp',P.map(function(p){return p[0]}),'pr');
function send(o){
  return fetch('/api/set',{method:'POST',body:new URLSearchParams(o)})
    .then(function(r){return r.json()}).then(show).catch(function(){});
}
function show(s){
  cur=s;
  $('big').textContent=(s.f/1000).toFixed(2)+' kHz';
  $('sub').textContent=M[s.m]+'  |  '+Math.round(s.d)+' dBm  |  '+(s.ok?s.n:(s.st||'sin conexión'));
  if(document.activeElement!==$('fi'))$('fi').value=(s.f/1000).toFixed(2);
  if(!busy)$('vol').value=s.v;
  $('vl').textContent=$('vol').value;
  S.forEach(function(_,i){$('ss'+i).className=(i==s.s)?'on':''});
  M.forEach(function(_,i){$('mm'+i).className=(i==s.m)?'on':''});
  BF.forEach(function(v,i){$('bb'+i).className=(v==s.bf)?'on':''});
  $('ba').className=s.ba?'on':'';
  $('ad').className=s.ad?'on':'';
  $('bi').textContent='Buffer: '+s.bm+' / '+s.bt+' ms \u00b7 cortes: '+s.u;
}
function d(k){send({df:k*S[cur.s]})}
function go(){var v=parseFloat($('fi').value.replace(',','.'));if(v>0)send({f:Math.round(v*1000)});$('fi').blur()}
function ssel(i){send({s:i})}
function msel(i){send({m:i})}
function bsel(i){send({bf:BF[i]})}
function bauto(){send({ba:cur.ba?0:1})}
function adp(){send({ad:cur.ad?0:1})}
function pr(i){send({f:P[i][1]*1000,m:P[i][2]})}
function vch(){
  busy=1;$('vl').textContent=$('vol').value;clearTimeout(vt);
  vt=setTimeout(function(){send({v:$('vol').value}).then(function(){busy=0})},80);
}
var dgt=0;
function dg(){fetch('/api/debug').then(function(r){return r.text()}).then(function(t){$('dg').textContent=t}).catch(function(){})}
$('dd').ontoggle=function(){clearInterval(dgt);if(this.open){dg();dgt=setInterval(dg,2000)}};
function dr(){send({dr:1}).then(dg)}
function poll(){fetch('/api/state').then(function(r){return r.json()}).then(show).catch(function(){})}
poll();setInterval(poll,1000);
</script>
)KIWI";
