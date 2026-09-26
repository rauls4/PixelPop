#include "WebUI.h"
#include "Display.h"
#include "Transition.h"
#include <WiFi.h>
#include <ESPmDNS.h>
#include "App.h"
#include "Config.h"
#include "TimeService.h"
#include "LogoAssets.h"
#include "GuideAssets.h"
#include "Advanced.h"
#include "FwUpdate.h"
#include "Sleep.h"
#include "Net.h"

WebServer gServer(80);

static const char CSS[] PROGMEM =
  "body{font-family:-apple-system,Helvetica,Arial,sans-serif;margin:0;padding:20px;background:#f4f4f6;color:#111}"
  ".ver{font-size:13px;font-weight:400;color:#777;margin-left:8px;white-space:nowrap}"
  ".brand{margin-bottom:8px}.brand img.wordmark{display:block;width:min(100%,320px);height:auto;margin-bottom:8px}"
  ".brand p{margin:2px 0 0}"
  "nav{max-width:440px;margin:0 auto 14px;display:flex;flex-wrap:wrap;gap:6px}"
  "nav a{padding:7px 12px;border-radius:16px;background:#e6e6ea;color:#111;text-decoration:none;font-size:14px}"
  "nav a.on{background:#0a66ff;color:#fff}"
  ".card{max-width:440px;margin:0 auto 16px;background:#fff;padding:20px;border-radius:12px;box-shadow:0 1px 4px #0002}"
  "h2{margin-top:0}h3{margin:26px 0 0;padding-top:16px;border-top:1px solid #e3e3e8;font-size:17px}"
  "h3.first{margin-top:14px;border-top:0;padding-top:0}"
  "label{display:block;margin:14px 0 4px;font-weight:600}"
  "input[type=text],input[type=password],input[type=number],input[type=date],input[type=url],select{width:100%;box-sizing:border-box;padding:12px;font-size:16px;border:1px solid #bbb;border-radius:8px;background:#fff}"
  "input[type=color]{width:100%;height:46px;padding:3px;box-sizing:border-box;border:1px solid #bbb;border-radius:8px;background:#fff}"
  "input[type=range]{width:100%}"
  "button{margin-top:18px;width:100%;padding:14px;font-size:17px;border:0;border-radius:8px;background:#0a66ff;color:#fff}"
  "button.sec{background:#e6e6ea;color:#111;margin-top:10px}"
  ".s{font-weight:400;font-size:15px;margin-top:10px}.s input{margin-right:8px}.m{color:#555;font-size:14px}"
  ".ok{background:#e3f6e8;color:#14612c;padding:10px;border-radius:8px;margin-bottom:12px}"
  ".err{background:#fdeaea;color:#8a1c1c;padding:10px;border-radius:8px;margin:10px 0}"
  ".row{display:flex;align-items:center;gap:14px;padding:12px 0;border-bottom:1px solid #eee;will-change:transform}"
  ".row:last-child{border-bottom:0}.rt{flex:1;min-width:0}.rt a{color:#111;font-weight:600;text-decoration:none}"
  ".dh{cursor:grab;color:#8e8e93;font-size:22px;line-height:1;padding:6px 2px;touch-action:none;user-select:none;-webkit-user-select:none}"
  ".row.drag{position:relative;z-index:2;background:#eef3ff;box-shadow:0 5px 18px rgba(0,0,0,.22);outline:2px solid #78adff;border-radius:8px}"
  ".row:hover{background:#f7f9ff}"
  ".eye{flex:none;width:20px;height:20px;display:flex;align-items:center;justify-content:center}"
  ".eye svg{width:16px;height:16px;display:block}"
  ".eye.st-on{color:#171717}.eye.st-off{color:#c7c7cc}.eye.st-hide{color:#171717}"
  ".cz{color:#0a66ff;text-decoration:none;font-size:14px;white-space:nowrap}"
  "label.sw{position:relative;display:inline-block;width:50px;height:30px;margin:0;flex:none}"
  ".sw input{opacity:0;width:0;height:0}"
  ".sl{position:absolute;inset:0;background:#c7c7cc;border-radius:30px;transition:.2s;cursor:pointer}"
  ".sl:before{content:'';position:absolute;height:24px;width:24px;left:3px;top:3px;background:#fff;border-radius:50%;transition:.2s}"
  ".sw input:checked+.sl{background:#34c759}"
  ".sw input:checked+.sl:before{transform:translateX(20px)}";

String uiHead(const char *title) {
  String h = F("<!DOCTYPE html><html><head><meta charset='utf-8'>"
               "<meta name='viewport' content='width=device-width,initial-scale=1'>"
               "<link rel='icon' type='image/png' href='/icon.png'><link rel='apple-touch-icon' href='/icon.png'><title>");
  h += title;
  h += F("</title><style>");
  h += FPSTR(CSS);
  h += F("</style></head><body>");
  return h;
}

String htmlEscape(const String &s) {
  String o;
  o.reserve(s.length() + 8);
  for (size_t i = 0; i < s.length(); i++) {
    char c = s[i];
    if (c == '&') o += "&amp;";
    else if (c == '<') o += "&lt;";
    else if (c == '>') o += "&gt;";
    else if (c == '"') o += "&quot;";
    else if (c == '\'') o += "&#39;";
    else o += c;
  }
  return o;
}

// ---------- form helpers ----------
String uiSection(const char *title) {
  return String("<h3>") + title + "</h3>";
}

String uiCheckbox(const char *name, const char *label, bool on) {
  return String("<label class='s'><input type='checkbox' name='") + name + "'" +
         (on ? " checked" : "") + ">" + label + "</label>";
}

String uiNumber(const char *name, const char *label, long value, long lo, long hi) {
  return String("<label>") + label + "</label><input type='number' name='" + name +
         "' min='" + lo + "' max='" + hi + "' value='" + value + "'>";
}

String uiText(const char *name, const char *label, const String &value, int maxLen) {
  return String("<label>") + label + "</label><input type='text' name='" + name +
         "' maxlength='" + maxLen + "' value='" + htmlEscape(value) + "'>";
}

String uiDate(const char *name, const char *label, const String &value) {
  return String("<label>") + label + "</label><input type='date' name='" + name +
         "' value='" + htmlEscape(value) + "'>";
}

String uiColor(const char *name, const char *label, uint32_t c) {
  char hex[8];
  snprintf(hex, sizeof(hex), "#%06x", (unsigned)(c & 0xFFFFFF));
  return String("<label>") + label + "</label><input type='color' name='" + name +
         "' value='" + hex + "'>";
}

String uiSelect(const char *name, const char *label, const char *const *options, int count, int selected) {
  String h = String("<label>") + label + "</label><select name='" + name + "'>";
  for (int i = 0; i < count; i++) {
    h += String("<option value='") + i + "'" + (i == selected ? " selected" : "") + ">" + options[i] + "</option>";
  }
  h += "</select>";
  return h;
}

String uiMatrixPreview(const char *moduleId) {
  String h = "<h3>PixelPop Matrix Simulator</h3><p class='m'>Live visual approximation of this module. Changes are previewed locally until you save.</p>"
             "<canvas class='mxp' data-module='" + String(moduleId) + "' width='320' height='160' style='width:100%;height:auto;background:#05070b;border-radius:8px;image-rendering:pixelated'></canvas>";
  h += F("<script>(function(){const c=document.currentScript.previousElementSibling,x=c.getContext('2d'),m=c.dataset.module,"
         "q=n=>document.querySelector('[name=\"'+n+'\"]'),v=(n,d='')=>q(n)?q(n).value:d,on=n=>!!q(n)&&q(n).checked,col=(n,d)=>v(n,d),txt=(n,d)=>v(n,d)||d;"
         "function t(s,a,b,z=18,k='#fff'){x.fillStyle=k;x.font='bold '+z+'px monospace';x.fillText(s,a,b)}"
         "function box(a,b,w,h,k){x.fillStyle=k;x.fillRect(a,b,w,h)}"
         "function lava(now){let sp=+v('spd',100)/100,n=+v('cnt',8),sz=+v('size',100)/100,stick=+v('coh',35)/100,conv=+v('conv',100)/100,variation=+v('var',35)/100,glow=col('glow','#100018'),random=on('rnd'),cs=[col('wax','#ff1400'),col('liq','#ff0037'),col('base','#6400ff')],rc=['#ff1744','#ff6d00','#ffd600','#63e61b','#00d5c8','#178bff','#7448ff','#cf38ff','#ff3da7','#fff'],dt=Math.min(.05,(now-(c._lt||now))/1e3)*sp;c._lt=now;"
         "let bs=c._blobs||(c._blobs=[]);if(c._random!==random){bs.length=0;c._random=random}while(bs.length<n){let rising=bs.length%2===0,y=rising?134:26,r=26*sz*(1+(Math.random()*2-1)*variation);bs.push({x:30+Math.random()*260,y:y,vx:0,vy:rising?-40:22,t:rising?.78:.20,r:r,color:random?rc[Math.floor(Math.random()*rc.length)]:cs[bs.length%3]})}bs.length=n;"
         "for(let i=0;i<n;i++)for(let j=i+1;j<n;j++){let a=bs[i],b=bs[j],dx=b.x-a.x,dy=b.y-a.y,d=Math.hypot(dx,dy),reach=52*sz;if(d<1||d>=reach)continue;let p=stick*(1-d/reach)*75*dt;a.vx+=dx/d*p;a.vy+=dy/d*p;b.vx-=dx/d*p;b.vy-=dy/d*p}"
         "x.fillStyle=glow;x.globalAlpha=1-(+v('trail',188))/255;x.fillRect(0,0,320,160);x.globalAlpha=1;for(let i=0;i<n;i++){let b=bs[i],r=b.r,heat=Math.max(0,(b.y-104)/56),cool=Math.max(0,(48-b.y)/48),turb=Math.sin(now/588+i*2.17)*.1*conv;b.t=Math.max(0,Math.min(1,b.t+(heat*(2.8+conv)-cool*(1.9+conv*.6)-(b.t-.48)*.12+turb)*dt));b.vy=Math.max(-100,Math.min(100,(b.vy+(.5-b.t)*600*dt+Math.sin(now/435+i*1.13)*18*conv*dt)*.97));b.vx=Math.max(-45,Math.min(45,(b.vx+(Math.sin(b.y/160*6.283+now/1250+i*1.7)*90+Math.sin(now/244+i*3.1)*75*conv)*dt)*.96));b.y+=b.vy*dt;b.x+=b.vx*dt;if(b.y<r){b.y=r;b.vy=Math.abs(b.vy)*.25}if(b.y>160-r){b.y=160-r;b.t=Math.max(b.t,.7);b.vy=-Math.max(40,Math.abs(b.vy)*.25)}if(b.x<r){b.x=r;b.vx=Math.abs(b.vx)*.35}if(b.x>320-r){b.x=320-r;b.vx=-Math.abs(b.vx)*.35}let g=x.createRadialGradient(b.x,b.y,0,b.x,b.y,r);g.addColorStop(0,b.color);g.addColorStop(1,'transparent');x.fillStyle=g;x.beginPath();x.arc(b.x,b.y,r,0,7);x.fill()}if(on('clk')){box(0,140,58,20,'#08030b');t('12:34',4,154,12,'#fff0d0')}requestAnimationFrame(lava)}"
         "function draw(){if(m==='lavalamp'){if(!c._lava){c._lava=1;requestAnimationFrame(lava)}return}x.fillStyle='#05070b';x.fillRect(0,0,320,160);let a=col('cAcc','#00c8ff'),b=col('cTime',col('cTemp','#ffffff')),d=col('cBand',col('cWall','#4a78ff'));"
         "if(m==='weather'){box(46,18,70,118,'#10212a');box(54,22,13,96,'#ef2929');x.fillStyle='#ef2929';x.beginPath();x.arc(60,122,18,0,7);x.fill();x.strokeStyle='#8ccfe8';x.lineWidth=4;x.beginPath();x.roundRect(46,18,70,118,26);x.stroke();x.beginPath();x.arc(60,122,20,0,7);x.stroke();x.strokeStyle='#e4faff';x.lineWidth=4;x.beginPath();x.moveTo(51,31);x.lineTo(51,107);x.stroke();t('72',150,66,42,'#ef2929');t('F',226,62,18,a);t('SUNNY',148,121,19,col('cCond','#ffd200'));}"
         "else if(m==='clock'){box(120,4,80,152,'#5d2e18');x.fillStyle='#f4e4c1';x.beginPath();x.arc(160,45,31,0,7);x.fill();x.strokeStyle='#000';x.lineWidth=3;x.beginPath();x.moveTo(160,45);x.lineTo(160,24);x.moveTo(160,45);x.lineTo(178,52);x.stroke();x.strokeStyle='#e7b43a';x.beginPath();x.moveTo(160,84);x.lineTo(160,119);x.stroke();x.fillStyle='#e7b43a';x.beginPath();x.arc(160,122,7,0,7);x.fill();x.fillStyle='#f4e4c1';x.beginPath();x.roundRect(133,132,54,18,4);x.fill();x.strokeStyle='#9a552a';x.stroke();t('12:34PM',137,145,10,'#5d2e18');}"
         "else if(m==='countdown'){t(txt('label','COUNTDOWN').slice(0,18),126,20,12,col('cLab','#ffd200'));box(124,28,72,2,col('cAcc','#00c8ff'));t('12',136,108,64,col('cNum','#fff'));t('DAYS  HOURS 05',130,143,11,col('cAcc','#00c8ff'));}"
         "else if(m==='forecast'){box(122,4,76,152,'#08101b');t('TODAY',137,23,13,col('cDay','#00c8ff'));box(129,28,62,2,col('cDay','#00c8ff'));t('72',132,65,34,col('cHi','#ff9040'));t('F',174,61,12,'#aab4c5');x.fillStyle='#ffd24a';x.beginPath();x.arc(160,88,13,0,7);x.fill();t('LOW 54',139,119,13,col('cLo','#40a0ff'));t('RAIN 20%',133,138,12,'#fff');for(let i=0;i<4;i++)box(153+i*5,147,3,3,i===0?col('cDay','#00c8ff'):'#8893a6');}"
         "else if(m==='calendar'){box(122,8,76,66,d);t('TODAY 10:30',128,22,10,'#fff');t('Team planning meeting',126,47,10,'#fff');box(122,82,76,66,d);t('TOMORROW',129,96,10,'#fff');t('Dinner reservation',129,121,10,'#fff')}"
         "else if(m==='air'){box(122,4,76,152,'#07101b');t('TEMP',143,24,11,col('cLbl','#8a93a6'));t('72',136,56,30,col('cTmp','#ff9a3c'));t('F',176,52,11,col('cTmp','#ff9a3c'));t('HUMID',139,82,11,col('cLbl','#8a93a6'));t('46%',137,112,27,col('cHum','#40c8ff'));t('COMFORTABLE',129,140,9,'#48d17b');}"
         "else if(m==='fireworks'){for(let i=0;i<18;i++){x.fillStyle=['#ffd24a','#ff4a6c','#66caff'][i%3];x.fillRect(155+Math.cos(i)*((i%4)*11+16),65+Math.sin(i)*((i%4)*11+16),4,4)}box(0,132,320,28,'#111827');}"
         "else if(m==='chomper'){box(122,3,76,154,'#03060d');t('AUTO 120',137,18,10,'#fff');x.strokeStyle=col('cWall','#2030ff');x.lineWidth=3;x.strokeRect(130,26,60,122);for(let r=0;r<12;r++)for(let q=0;q<8;q++)if((r>8&&((q+r)%3))||(r===11&&q<6)){x.fillStyle=['#35c8ff','#ffd142','#b768ff','#4fe274'][(q+r)%4];x.fillRect(132+q*7,28+r*10,6,9)}x.fillStyle='#ff8b3d';x.fillRect(153,48,6,9);x.fillRect(160,48,6,9);x.fillRect(167,48,6,9);x.fillRect(167,38,6,9);}"
         "else if(m==='orbitaleye'){box(0,0,320,160,'#030305');x.strokeStyle='#ff2b24';x.lineWidth=10;x.beginPath();x.arc(160,80,46,0,7);x.stroke();x.fillStyle='#ff4a3a';x.beginPath();x.arc(160,80,22,0,7);x.fill();t('TERM',225,26,14,'#61b8ff');}"
         "else if(m==='photos'){let g=x.createLinearGradient(0,0,320,160);g.addColorStop(0,'#ff7b55');g.addColorStop(.5,'#6136a7');g.addColorStop(1,'#1ec6d1');box(0,0,320,160,g);}"
         "else if(m==='flight'){box(122,3,76,154,'#07101b');x.fillStyle='#fff';x.fillRect(122,3,76,76);t('UA',148,45,22,'#1769aa');t('UAL482',138,98,10,col('cCall','#ffd200'));t('FL350',143,116,10,col('cData','#fff'));t('450KT',145,134,10,col('cData','#fff'));t('12NM',147,152,10,col('cAcc','#00c8ff'));}"
         "else if(m==='chime'){x.strokeStyle='#d6a84d';x.lineWidth=5;x.beginPath();x.arc(160,75,48,0,7);x.stroke();t('12',150,45,16,'#fff');}"
         "else if(m==='urgent'){box(0,0,320,160,'#7e1212');t('URGENT',72,92,42,'#fff');}"
         "else {const L='00001b000000000000019b20000000000001800c00000000001c000c00000000001e0000f0000000003e0000f0000000ff1c0000f7f00000ffc80000f7fc0000f3c00000f73c0000f3ddce38f73cf8fcf3ddfe7ef73dfcfeffdcfdeff7ffdeffff1c7dc6f7f38ee7ff1c79fff7f38ee7f01cfdc0f7038ee7f01dfdcff703defff01dfefff701fcfef01dce3cf700f8fc00000000000000e000000000000000e000000000000000e000000000000000e0';const LW=64,LH=22,SC=5,ox=(320-LW*SC)/2,oy=(160-LH*SC)/2;x.fillStyle='#fff';for(let r=0;r<LH;r++)for(let c=0;c<LW;c++){const bi=r*8+(c>>3),bit=7-(c&7);if((parseInt(L.slice(bi*2,bi*2+2),16)>>bit)&1)x.fillRect(ox+c*SC,oy+r*SC,SC,SC)}t('No live preview for this module yet',60,150,12,'#6b7686')}"
         "}document.querySelectorAll('input,select,textarea').forEach(e=>e.addEventListener('input',()=>{if(['cnt','size','var','rnd'].includes(e.name))c._blobs=[];let o=document.getElementById(e.name+'Value');if(o)o.textContent=e.value+(e.name==='spd'||e.name==='coh'||e.name==='conv'||e.name==='var'?'%':'');draw()}));draw()})()</script>");
  return h;
}

bool uiReadColor(WebServer &s, const char *name, uint32_t &out) {
  if (!s.hasArg(name)) return false;
  String h = s.arg(name);
  h.trim();
  if (h.startsWith("#")) h.remove(0, 1);
  if (h.length() != 6) return false;
  char *end = nullptr;
  unsigned long v = strtoul(h.c_str(), &end, 16);
  if (end == nullptr || *end != '\0') return false;
  out = (uint32_t)v;
  return true;
}

long uiReadLong(WebServer &s, const char *name, long def, long lo, long hi) {
  if (!s.hasArg(name)) return def;
  long v = s.arg(name).toInt();
  if (v < lo) v = lo;
  if (v > hi) v = hi;
  return v;
}

void webRedirect(const char *where) {
  gServer.sendHeader("Location", where, true);
  gServer.send(303, "text/plain", "");
}

// ---------- pages ----------
// Home has no bar: every function is reached from its list. Each function page gets one link back.
static String navBar(const char *active) {
  if (strcmp(active, "home") == 0) return "";
  return "<nav><a href='/'>&#8249; Home</a></nav>";
}

static const char TOGGLE_JS[] PROGMEM =
  "<script>"
  "function tog(el){"
  "var m=document.getElementById('msg');m.style.display='none';"
  "fetch('/toggle',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},"
  "body:'id='+encodeURIComponent(el.dataset.id)+'&on='+(el.checked?1:0)})"
  ".then(function(r){if(r.ok)return;return r.text().then(function(t){el.checked=!el.checked;m.textContent=t;m.style.display='block';});})"
  ".catch(function(){el.checked=!el.checked;m.textContent='Could not reach the display.';m.style.display='block';});"
  "}"
  "(function(){"
  "var fl=document.getElementById('fl'),cur=null;"
  "function ids(){return Array.prototype.map.call(fl.children,function(r){return r.dataset.id;}).join(',');}"
  "function places(){var p={};Array.prototype.forEach.call(fl.children,function(r){p[r.dataset.id]=r.getBoundingClientRect().top;});return p;}"
  "function animateTo(p){Array.prototype.forEach.call(fl.children,function(r){if(r===cur)return;var d=p[r.dataset.id]-r.getBoundingClientRect().top;if(!d)return;"
  "r.style.transition='none';r.style.transform='translateY('+d+'px)';requestAnimationFrame(function(){r.style.transition='transform .18s cubic-bezier(.2,.8,.2,1)';r.style.transform='';"
  "setTimeout(function(){r.style.transition='';},190);});});}"
  "fl.addEventListener('pointerdown',function(e){"
  "var h=e.target.closest('.dh');if(!h)return;"
  "cur=h.closest('.row');cur.classList.add('drag');cur.setAttribute('aria-grabbed','true');fl.setPointerCapture(e.pointerId);e.preventDefault();});"
  "fl.addEventListener('pointermove',function(e){"
  "if(!cur)return;var y=e.clientY,rows=fl.children;"
  "for(var i=0;i<rows.length;i++){var r=rows[i];if(r===cur)continue;var b=r.getBoundingClientRect();"
  "if(y>b.top&&y<b.bottom){var p=places();if(y<b.top+b.height/2)fl.insertBefore(cur,r);else fl.insertBefore(cur,r.nextSibling);animateTo(p);break;}}});"
  "function end(){if(!cur)return;cur.classList.remove('drag');cur.removeAttribute('aria-grabbed');cur=null;"
  "var m=document.getElementById('msg');"
  "fetch('/order',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:'ids='+encodeURIComponent(ids())})"
  ".then(function(r){if(!r.ok){m.textContent='Could not save the order.';m.style.display='block';}})"
  ".catch(function(){m.textContent='Could not reach the display.';m.style.display='block';});}"
  "fl.addEventListener('pointerup',end);fl.addEventListener('pointercancel',end);"
  "})();"
  "(function(){"
  "var fl=document.getElementById('fl'),pt=null,curId=null,curRow=null,NS='http://www.w3.org/2000/svg';"
  "function ping(id){fetch('/preview',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:'id='+encodeURIComponent(id)}).catch(function(){});}"
  "function mkSvg(){"
  "var s=document.createElementNS(NS,'svg');s.setAttribute('viewBox','0 0 24 24');s.setAttribute('fill','none');"
  "s.setAttribute('stroke','currentColor');s.setAttribute('stroke-width','2');"
  "s.setAttribute('stroke-linecap','round');s.setAttribute('stroke-linejoin','round');return s;"
  "}"
  "function mkPath(d){var p=document.createElementNS(NS,'path');p.setAttribute('d',d);return p;}"
  "function eyeOpen(){"
  "var s=mkSvg();s.appendChild(mkPath('M1 12s4-8 11-8 11 8 11 8-4 8-11 8-11-8-11-8z'));"
  "var c=document.createElementNS(NS,'circle');c.setAttribute('cx','12');c.setAttribute('cy','12');c.setAttribute('r','3');"
  "s.appendChild(c);return s;"
  "}"
  "function eyeClosed(){var s=mkSvg();s.appendChild(mkPath('M1 12s4-6 11-6 11 6 11 6'));return s;}"
  "function eyeSlash(){"
  "var s=mkSvg();s.appendChild(mkPath('M17.94 17.94A10.94 10.94 0 0 1 12 20c-7 0-11-8-11-8a21.86 21.86 0 0 1 5.06-6.06M9.9 4.24A10.94 10.94 0 0 1 12 4c7 0 11 8 11 8a21.86 21.86 0 0 1-4.22 5.94M1 1l22 22'));"
  "return s;"
  "}"
  "function setEye(r,open){"
  "var el=r.querySelector('.eye');if(!el)return;"
  "var pages=r.dataset.pages==='1',cb=r.querySelector('input[type=checkbox]'),on=!!(cb&&cb.checked);"
  "el.innerHTML='';"
  "if(!pages){el.className='eye st-hide';el.appendChild(eyeSlash());}"
  "else if(!on){el.className='eye st-off';el.appendChild(eyeOpen());}"
  "else{el.className='eye st-on';el.appendChild(open?eyeOpen():eyeClosed());}"
  "}"
  "function sync(r,hovering){"
  "var pages=r.dataset.pages==='1',cb=r.querySelector('input[type=checkbox]'),on=!!(cb&&cb.checked);"
  "setEye(r,hovering);"
  "if(pt){clearInterval(pt);pt=null;}"
  "if(hovering&&pages&&on){var id=r.dataset.id;ping(id);pt=setInterval(function(){ping(id);},500);}"
  "}"
  "Array.prototype.forEach.call(fl.children,function(r){setEye(r,false);});"
  "fl.addEventListener('mouseenter',function(e){"
  "var r=e.target.closest&&e.target.closest('.row');if(!r)return;var id=r.dataset.id;"
  "if(id===curId)return;if(curRow)setEye(curRow,false);curId=id;curRow=r;sync(r,true);"
  "},true);"
  "fl.addEventListener('mouseleave',function(e){"
  "var r=e.target.closest&&e.target.closest('.row');if(!r)return;"
  "if(r.contains(e.relatedTarget))return;"
  "if(pt){clearInterval(pt);pt=null;}setEye(r,false);curRow=null;curId=null;"
  "},true);"
  "fl.addEventListener('change',function(e){"
  "var cb=e.target;if(!cb||cb.type!=='checkbox')return;"
  "var r=cb.closest('.row');if(!r)return;"
  "sync(r,r===curRow);"
  "});"
  "})();"
  "</script>";

// Home-page "update available" banner: checks once on load, then only installs when the button
// is clicked — reuses /advanced/fstatus (already public, no confirmation code) for progress.
static const char UPDATE_JS[] PROGMEM =
  "<script>"
  "(function(){"
  "function $(i){return document.getElementById(i);}"
  "var card=$('upd'),msg=$('updMsg'),btn=$('updBtn'),pr=$('updProg');"
  "if(!card)return;"
  "var c=new XMLHttpRequest();c.open('GET','/update/check');"
  "c.onload=function(){if(c.status!=200)return;var r=c.responseText,i=r.indexOf('|'),ok=r.substring(0,i)=='1',m=r.substring(i+1);"
  "if(ok){msg.textContent=m;card.style.display='';}};"
  "c.send();"
  "btn.onclick=function(){btn.disabled=true;pr.style.display='';pr.textContent='Starting...';"
  "var p=new XMLHttpRequest();p.open('POST','/update/install');"
  "p.onload=function(){if(p.status!=200){btn.disabled=false;pr.textContent=p.responseText||'Could not start.';return;}"
  "function reload(){location.href='/';}"
  "function poll(){var s=new XMLHttpRequest();s.open('GET','/advanced/fstatus');"
  "s.onload=function(){var r=s.responseText,i=r.indexOf('|'),j=r.indexOf('|',i+1),st=r.substring(0,i),m=r.substring(j+1);"
  "if(st=='run'){pr.textContent=m;setTimeout(poll,1000);}"
  "else if(st=='done'){pr.textContent=m+' This page reloads in a moment.';setTimeout(reload,25000);}"
  "else if(st=='fail'){btn.disabled=false;pr.textContent=m;}"
  "else{pr.textContent='The display stopped answering. If it restarted, the update is installed; this page reloads in a moment.';setTimeout(reload,20000);}};"
  "s.onerror=function(){pr.textContent='The display stopped answering. If it restarted, the update is installed; this page reloads in a moment.';setTimeout(reload,20000);};"
  "s.send();}"
  "setTimeout(poll,1000);};"
  "p.onerror=function(){btn.disabled=false;pr.textContent='Could not reach the display.';};"
  "p.send();};"
  "})();"
  "</script>";

// The pages are sent in small pieces instead of being built as one big String first. A big String needs
// one large free block of memory, which can be missing while the board is busy downloading (weather,
// calendar...); when that happened, the longest rows of the function list silently went missing.
static void pageBegin() {
  gServer.setContentLength(CONTENT_LENGTH_UNKNOWN);
  gServer.send(200, "text/html", "");
}
static void pageSend(String &p) {              // send what has been built so far and start over
  if (p.length()) gServer.sendContent(p);
  p = "";
}
static void pageEnd() { gServer.sendContent(""); }

static void handleHome() {
  pageBegin();
  String p;
  p.reserve(1400);
  p = uiHead("PixelPop");
  pageSend(p);
  p += navBar("home");

  // Hidden until /update/check (in UPDATE_JS below) finds a different official build than the
  // one last installed over Wi-Fi. This only offers to install — unlike the Advanced page's
  // automatic-updates toggle, it never installs anything without this button being clicked.
  p += F("<div class='card' id='upd' style='display:none'><h2>Update available</h2>"
         "<p class='m' id='updMsg'></p><button id='updBtn' type='button'>Download and install</button>"
         "<p class='m' id='updProg' style='display:none'></p></div>");
  pageSend(p);

  p += "<div class='card'><div class='brand'><img class='wordmark' src='/mark.png' width='320' height='120' alt='PixelPop'>"
       "<p class='m'>" + WiFi.localIP().toString() + " &middot; " + netHostname() + ".local</p></div>";
  if (gServer.hasArg("saved")) p += F("<div class='ok'>Saved.</div>");
  pageSend(p);

  p += F("<h3 class='first'>Functions</h3><div id='msg' class='err' style='display:none'></div>"
         "<p class='m'>Drag the &#9776; handle to change the order the pages appear in.</p><div id='fl'>");
  pageSend(p);
  for (int i = 0; i < app.count(); i++) {
    Module *m = app.module(i);
    String id = m->id();
    p += "<div class='row' data-id='" + id + "' data-pages='" + (m->hasPages() ? "1" : "0") + "'><span class='dh' title='Drag to reorder'>&#9776;</span><span class='eye'></span><label class='sw'><input type='checkbox' data-id='" + id + "'" +
         (m->enabled() ? " checked" : "") + " onchange='tog(this)'><span class='sl'></span></label>"
         "<div class='rt'><a href='/" + id + "'>" + m->title() + "</a><div class='m'>" +
         htmlEscape(m->summary()) + "</div></div>"
         "<a class='cz' href='/" + id + "'>Customize &rsaquo;</a></div>";
    pageSend(p);                                          // one row at a time
  }
  p += "</div></div>";
  pageSend(p);

  // General settings
  p += "<div class='card'><h2>General</h2><form method='POST' action='/general'>";
  p += "<label>Brightness: <b id='bv'>" + String(app.brightness()) + "</b></label>"
       "<input type='range' name='bright' min='5' max='" + String(MAX_BRIGHTNESS) + "' value='" + String(app.brightness()) + "' "
       "oninput=\"document.getElementById('bv').textContent=this.value\">";
  p += "<label>Time zone</label><select name='tz'>";
  for (int i = 0; i < timeZoneCount(); i++) {
    p += "<option value='" + String(i) + "'" + String(i == app.tzIndex() ? " selected" : "") + ">" +
         timeZoneName(i) + "</option>";
  }
  p += "</select>";
  p += uiCheckbox("time24", "Use 24-hour time everywhere", app.use24Hour());
  p += uiText("host", "Device network name", netHostname(), 24);
  p += "<p class='m'>Use lower-case letters, numbers, and hyphens. This device will be available as <b>http://" +
       htmlEscape(netHostname()) + ".local</b> after it restarts.</p>";
  pageSend(p);
  p += "<label>Screen padding: <b id='pv'>" + String(app.padding()) + "</b> px</label>"
       "<input type='range' name='pad' min='0' max='" + String(MAX_PADDING) + "' value='" + String(app.padding()) + "' "
       "oninput=\"document.getElementById('pv').textContent=this.value\">"
       "<p class='m'>Blank pixels kept all around the edge of the screen. Content that reaches the edge is trimmed.</p>";
  p += uiCheckbox("autoOri", "Automatically rotate with the onboard IMU", app.autoOrientation());
  p += "<p class='m'>" + String(app.imuAvailable() ? "IMU detected. All four upright orientations are supported." : "IMU will be detected after the next restart; manual orientation remains available.") + "</p>";
  static const char *const rotations[] = {
    "Landscape — bottom down (0 degrees)", "Portrait — bottom left (90 degrees)",
    "Landscape — bottom up (180 degrees)", "Portrait — bottom right (270 degrees)"
  };
  p += uiSelect("rot", "Manual orientation", rotations, 4, app.rotation());
  p += "<label>Screen transition</label><select name='trn'>";
  for (int i = 0; i < transitionCount(); i++) {
    p += "<option value='" + String(i) + "'" + String(i == app.transition() ? " selected" : "") + ">" +
         transitionName(i) + "</option>";
  }
  p += "</select>";
  pageSend(p);
  p += "<label>Transition speed</label><select name='trs'>";
  for (int i = 0; i < 3; i++) {
    p += "<option value='" + String(i) + "'" + String(i == app.transitionSpeed() ? " selected" : "") + ">" +
         transitionSpeedName(i) + "</option>";
  }
  p += "</select>"
       "<p class='m'>The effect played when the screen changes to the next page. Random picks a different one each time.</p>";
  p += "<button type='submit'>Save</button></form></div>";

  p += "<div class='card'><p class='m'>" + htmlEscape(sleepStatus()) + "</p>";
  p += sleepIsAsleep() ? F("<form method='POST' action='/sleep/now'><input type='hidden' name='back' value='/'><button name='state' value='on' type='submit'>Turn display on</button></form>")
                       : F("<form method='POST' action='/sleep/now'><input type='hidden' name='back' value='/'><button name='state' value='off' type='submit'>Turn display off</button></form>");
  p += F("<form method='GET' action='/sleep'><button class='sec' type='submit'>Sleep &amp; schedule</button></form></div>");
  p += F("<div class='card'>"
         "<form method='POST' action='/refresh'><button class='sec' type='submit'>Refresh data now</button></form>"
         "<form method='GET' action='/guide' target='_blank'><button class='sec' type='submit'>User guide</button></form>"
         "<form method='GET' action='/advanced'><button class='sec' type='submit'>Advanced</button></form>"
         "<form method='POST' action='/wifi' onsubmit=\"return confirm('Restart and choose a different Wi-Fi network?')\">"
         "<button class='sec' type='submit'>Change Wi-Fi network</button></form></div>");

  pageSend(p);
  gServer.sendContent_P(TOGGLE_JS);
  gServer.sendContent_P(UPDATE_JS);
  gServer.sendContent("</body></html>");
  pageEnd();
}

static void handleModulePage(Module *m) {
  pageBegin();
  String p;
  p.reserve(1400);
  p = uiHead(m->title());
  p += navBar(m->id());
  p += String("<div class='card'><h2>") + m->title() + "<span class='ver'>v" + m->version() + "</span></h2>";
  if (gServer.hasArg("saved")) p += F("<div class='ok'>Saved.</div>");
  String sm = m->summary();
  if (sm.length()) p += "<p class='m'>" + htmlEscape(sm) + "</p>";
  pageSend(p);
  { String body = m->settingsHtml(); gServer.sendContent(body); }
  gServer.sendContent("</div>");
  { String a = m->actionsHtml(); if (a.length()) { gServer.sendContent("<div class='card'>"); gServer.sendContent(a); gServer.sendContent("</div>"); } }
  gServer.sendContent("</body></html>");
  pageEnd();
}

static void handleGeneralSave() {
  uint8_t b = (uint8_t)uiReadLong(gServer, "bright", app.brightness(), 5, MAX_BRIGHTNESS);
  uint8_t tz = (uint8_t)uiReadLong(gServer, "tz", app.tzIndex(), 0, timeZoneCount() - 1);
  uint8_t pad = (uint8_t)uiReadLong(gServer, "pad", app.padding(), 0, MAX_PADDING);
  uint8_t trn = (uint8_t)uiReadLong(gServer, "trn", app.transition(), 0, transitionCount() - 1);
  uint8_t trs = (uint8_t)uiReadLong(gServer, "trs", app.transitionSpeed(), 0, 2);
  uint8_t cord = (uint8_t)uiReadLong(gServer, "cord", app.colorOrder(), 0, COLOR_ORDERS - 1);
  uint8_t rot = (uint8_t)uiReadLong(gServer, "rot", app.rotation(), 0, 3);
  String hostname = gServer.arg("host");
  hostname.trim();
  hostname.toLowerCase();
  const bool hostnameChanged = hostname != netHostname();
  if (!netSetHostname(hostname)) {
    gServer.send(400, "text/html", "<html><body style='font-family:sans-serif;padding:20px'><h3>Invalid device network name.</h3><p>Use 1-24 lower-case letters, numbers, or hyphens; it cannot start or end with a hyphen.</p><p><a href='/'>Go back</a></p></body></html>");
    return;
  }
  app.setGeneral(b, tz, pad, trn, trs, cord, gServer.hasArg("time24"));
  app.setRotation(rot);
  app.setAutoOrientation(gServer.hasArg("autoOri"));
  if (!hostnameChanged) {
    webRedirect("/?saved=1");
    return;
  }
  gServer.send(200, "text/html", "<html><head><meta name='viewport' content='width=device-width,initial-scale=1'></head><body style='font-family:sans-serif;padding:20px'><h3>Saved.</h3><p>Restarting to apply the new network name.</p></body></html>");
  delay(800);
  ESP.restart();
}

// Instant on/off from the main page.
static void handleToggle() {
  Module *m = app.find(gServer.arg("id").c_str());
  if (!m) { gServer.send(404, "text/plain", "Unknown function."); return; }
  bool on = (gServer.arg("on") == "1");

  // Keep at least one function that shows something on the panel.
  if (!on && m->hasPages()) {
    int others = 0;
    for (int i = 0; i < app.count(); i++) {
      Module *o = app.module(i);
      if (o != m && o->hasPages() && o->enabled()) others++;
    }
    if (others == 0) {
      gServer.send(409, "text/plain", "At least one display function has to stay on.");
      return;
    }
  }
  if (!m->setEnabled(on)) {
    gServer.send(500, "text/plain", "The setting could not be saved. Open Advanced and check Settings storage and Settings not saved.");
    return;
  }
  app.requestRedraw();
  gServer.send(200, "text/plain", "ok");
}

// New order of the modules, sent when a row is dropped on the main page.
static void handleOrder() {
  app.setOrder(gServer.arg("ids"));
  gServer.send(200, "text/plain", "ok");
}

// Settings-site hover preview: the page pings this repeatedly (~500ms) while the mouse stays
// over a module's row; App::previewModule() re-arms a short auto-expiry each time, so a closed
// tab or a dropped request just lets the preview lapse back to the normal rotation on its own.
static void handlePreview() {
  String id = gServer.arg("id");
  if (!app.find(id.c_str())) { gServer.send(404, "text/plain", "Unknown function."); return; }
  app.previewModule(id.c_str());
  gServer.send(200, "text/plain", "ok");
}

static void handleRefresh() {
  app.refreshAll();
  webRedirect("/");
}

static void handleWifiReset() {
  gServer.send(200, "text/html", F("<html><head><meta name='viewport' content='width=device-width,initial-scale=1'></head>"
    "<body style='font-family:sans-serif;padding:20px'><h3>Restarting in setup mode.</h3>"
    "<p>Join the <b>PixelPop</b> Wi-Fi network on your phone to pick a new network.</p></body></html>"));
  delay(500);
  app.resetWifi();
}

void webBegin() {
  if (MDNS.begin(netHostname().c_str())) MDNS.addService("http", "tcp", 80);

  gServer.on("/", HTTP_GET, handleHome);
  gServer.on("/mark.png", HTTP_GET, []() {                  // the logo, kept in flash; browsers keep a copy for a day
    gServer.sendHeader("Cache-Control", "max-age=86400");
    gServer.send_P(200, "image/png", (PGM_P)LOGO_MARK_PNG, LOGO_MARK_PNG_LEN);
  });
  gServer.on("/guide", HTTP_GET, []() {                     // the User Guide, kept in flash as gzip; sent in slices so the panel keeps drawing
    gServer.sendHeader("Content-Encoding", "gzip");
    gServer.sendHeader("Cache-Control", "max-age=3600");
    gServer.setContentLength(GUIDE_HTML_GZ_LEN);
    gServer.send(200, "text/html; charset=utf-8", "");
    for (size_t off = 0; off < GUIDE_HTML_GZ_LEN; off += 2048) {
      const size_t n = (GUIDE_HTML_GZ_LEN - off) > 2048 ? 2048 : (GUIDE_HTML_GZ_LEN - off);
      gServer.sendContent_P((PGM_P)GUIDE_HTML_GZ + off, n);
      yield();
    }
  });
  gServer.on("/icon.png", HTTP_GET, []() {
    gServer.sendHeader("Cache-Control", "max-age=86400");
    gServer.send_P(200, "image/png", (PGM_P)LOGO_ICON_PNG, LOGO_ICON_PNG_LEN);
  });
  sleepRegister(gServer);                                   // /sleep: turn the display off by hand or on a schedule
  advancedRegister(gServer);                                // /advanced: firmware update and diagnostics
  gServer.on("/general", HTTP_POST, handleGeneralSave);
  gServer.on("/toggle", HTTP_POST, handleToggle);
  gServer.on("/order", HTTP_POST, handleOrder);
  gServer.on("/preview", HTTP_POST, handlePreview);
  gServer.on("/refresh", HTTP_POST, handleRefresh);
  gServer.on("/wifi", HTTP_POST, handleWifiReset);

  // Home-page "update available" banner: offer only, never installs without this POST.
  gServer.on("/update/check", HTTP_GET, []() {
    String msg;
    const bool available = fwCheckOfficial(msg);
    gServer.send(200, "text/plain", String(available ? "1|" : "0|") + msg);
  });
  gServer.on("/update/install", HTTP_POST, []() {
    String why;
    if (!fwStartOfficial(why)) { gServer.send(400, "text/plain", why); return; }
    gServer.send(200, "text/plain", "started");
  });

  // One customization page per function
  for (int i = 0; i < app.count(); i++) {
    Module *m = app.module(i);
    String path = String("/") + m->id();
    gServer.on(path, HTTP_GET, [m]() { handleModulePage(m); });
    gServer.on(path, HTTP_POST, [m]() {
      m->save(gServer);
      app.requestRedraw();
      String to = String("/") + m->id() + "?saved=1";
      webRedirect(to.c_str());
    });
    m->registerRoutes(gServer);
  }

  gServer.onNotFound([]() { gServer.send(404, "text/plain", "Not found"); });
  gServer.begin();
}

void webLoop() {
  gServer.handleClient();
  fwPanelTick();                       // progress of an internet update, if one is running
}
