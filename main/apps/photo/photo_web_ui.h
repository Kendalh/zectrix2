#ifndef APPS_PHOTO_PHOTO_WEB_UI_H_
#define APPS_PHOTO_PHOTO_WEB_UI_H_

// The album page. The browser does the picture work: crop/fit to 400x300, gray, auto-levels, contrast and 16-level
// Floyd-Steinberg (the same steps as the AI ink painter's conversion), shows the exact 16-gray result, and uploads the
// packed 4 bpp bytes (60000: high nibble = left pixel, 0 black .. 15 white).
namespace photos {
namespace web {
const char kPageHtml[] = R"HTML(<!doctype html>
<html lang="zh-CN"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>相册</title>
<style>
body{font:15px system-ui,sans-serif;max-width:640px;margin:0 auto;padding:12px;background:#f4f4f4;color:#111}
h1{font-size:20px}.card{background:#fff;border:1px solid #ccc;border-radius:6px;padding:12px;margin:12px 0}
.card h2{font-size:16px;margin:0 0 8px}a{color:#06c;text-decoration:none}
label{display:block;margin:10px 0 2px;font-weight:600}select,input[type=text]{width:100%;box-sizing:border-box;padding:6px;font-size:15px}
input[type=range]{width:100%}button{padding:8px 14px;font-size:15px;background:#111;color:#fff;border:0;border-radius:4px;margin:8px 8px 0 0}
button.s{background:#fff;color:#111;border:1px solid #888}button:disabled{opacity:.4}
canvas{width:100%;border:1px solid #888;background:#fff;image-rendering:pixelated;margin-top:8px}
.row{display:flex;justify-content:space-between;align-items:center;padding:8px 0;border-top:1px solid #eee;gap:8px}
.row span{flex:1;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}.st{color:#555;font-size:13px}.ok{color:#080}.err{color:#b00}
</style></head><body>
<a href="/">‹ 返回</a><h1>相册</h1>
<div class="card"><h2>上传照片</h2>
<input type="file" id="file" accept="image/*">
<label>方式</label><select id="fit"><option value="cover">铺满（裁掉多余部分）</option><option value="contain">完整显示（四周留白）</option></select>
<label>对比度 <span id="cv" class="st"></span></label><input type="range" id="contrast" min="80" max="220" value="130">
<label><input type="checkbox" id="dither" checked> 抖动（过渡更细腻）</label>
<button class="s" id="rot">旋转 90°</button>
<canvas id="view" width="400" height="300"></canvas>
<label>标题</label><input type="text" id="title" maxlength="40" placeholder="照片标题">
<button id="send" disabled>上传到相册</button><span id="msg" class="st"></span></div>
<div class="card"><h2>播放</h2>
<label>设备上全屏播放时，多久自动切换到下一张（“不刷新”则停在当前这张，用 UP/DN 手动切换）</label>
<select id="interval"><option value="off">不刷新</option><option value="5m">5 分钟</option><option value="10m">10 分钟</option><option value="15m">15 分钟</option></select>
<span id="imsg" class="st"></span></div>
<div class="card"><h2>相册 <span id="count" class="st"></span></h2><div id="list"></div>
<canvas id="big" width="400" height="300" style="display:none"></canvas></div>
<script>
const $=id=>document.getElementById(id),W=400,H=300;
let img=null,rot=0,levels=null;
const msg=(t,c)=>{$('msg').textContent=t;$('msg').className='st '+(c||'')};
function process(){
 if(!img)return;
 const c=document.createElement('canvas');c.width=W;c.height=H;const x=c.getContext('2d');
 x.fillStyle='#fff';x.fillRect(0,0,W,H);
 const sw=rot%2?img.height:img.width,sh=rot%2?img.width:img.height;
 const k=$('fit').value==='cover'?Math.max(W/sw,H/sh):Math.min(W/sw,H/sh);
 x.save();x.translate(W/2,H/2);x.rotate(rot*Math.PI/2);x.scale(k,k);x.drawImage(img,-img.width/2,-img.height/2);x.restore();
 const d=x.getImageData(0,0,W,H).data,y=new Float32Array(W*H),hist=new Int32Array(256);
 for(let i=0;i<W*H;i++){const v=Math.round(.299*d[i*4]+.587*d[i*4+1]+.114*d[i*4+2]);y[i]=v;hist[v]++;}
 // auto-levels: darkest 0.5% -> black, lightest 2% -> white
 let lo=0,hi=255,a=0;
 for(let v=0;v<256;v++){a+=hist[v];if(a>W*H/200){lo=v;break;}}
 a=0;for(let v=255;v>=0;v--){a+=hist[v];if(a>W*H/50){hi=v;break;}}
 if(hi-lo<48){lo=0;hi=255;}
 const ct=$('contrast').value/100;$('cv').textContent='×'+ct.toFixed(2);
 for(let i=0;i<W*H;i++){let s=(y[i]-lo)*255/(hi-lo);s=Math.min(255,Math.max(0,s));y[i]=Math.min(255,Math.max(0,128+(s-128)*ct));}
 levels=new Uint8Array(W*H);const dither=$('dither').checked;
 let cur=new Float32Array(W+2),nxt=new Float32Array(W+2);
 for(let r=0;r<H;r++){
  nxt.fill(0);const ltr=r%2===0,dir=ltr?1:-1;
  for(let i=0;i<W;i++){
   const xx=ltr?i:W-1-i,want=y[r*W+xx]+(dither?cur[xx+1]:0);
   const lv=Math.min(15,Math.max(0,Math.round(want/17)));levels[r*W+xx]=lv;
   if(dither){const e=want-lv*17;cur[xx+1+dir]+=e*7/16;nxt[xx+1-dir]+=e*3/16;nxt[xx+1]+=e*5/16;nxt[xx+1+dir]+=e/16;}
  }
  [cur,nxt]=[nxt,cur];
 }
 draw($('view'),levels);$('send').disabled=false;
}
function draw(canvas,lv){const x=canvas.getContext('2d'),o=x.createImageData(W,H);
 for(let i=0;i<W*H;i++){const g=lv[i]*17;o.data[i*4]=o.data[i*4+1]=o.data[i*4+2]=g;o.data[i*4+3]=255;}x.putImageData(o,0,0);}
$('file').onchange=e=>{const f=e.target.files[0];if(!f)return;
 const u=new Image();u.onload=()=>{img=u;rot=0;$('title').value=f.name.replace(/\.[^.]*$/,'').slice(0,20);process();msg('');};
 u.onerror=()=>msg('无法读取这张图片','err');u.src=URL.createObjectURL(f);};
['fit','contrast','dither'].forEach(i=>$(i).oninput=process);
$('rot').onclick=()=>{rot=(rot+1)%4;process();};
$('send').onclick=async()=>{
 const b=new Uint8Array(W*H/2);for(let i=0;i<W*H;i+=2)b[i>>1]=(levels[i]<<4)|levels[i+1];
 $('send').disabled=true;msg('上传中…');
 const r=await fetch('/api/photos?title='+encodeURIComponent($('title').value),{method:'POST',body:b});
 if(r.ok){msg('已上传','ok');load();}else{msg(await r.text(),'err');}
 $('send').disabled=false;};
async function load(){
 const j=await (await fetch('/api/photos')).json();
 $('count').textContent=j.photos.length+' / '+j.capacity;
 const list=$('list');list.textContent='';
 j.photos.forEach(p=>{
  const row=document.createElement('div');row.className='row';
  const t=document.createElement('span');t.textContent=p.title;
  const v=document.createElement('button');v.className='s';v.textContent='查看';v.onclick=()=>view(p.id);
  const d=document.createElement('button');d.className='s';d.textContent='删除';
  d.onclick=async()=>{if(confirm('删除“'+p.title+'”？')){await fetch('/api/photos/'+p.id,{method:'DELETE'});load();}};
  row.append(t,v,d);list.append(row);});
 if(!j.photos.length)list.textContent='还没有照片';
}
async function view(id){
 const b=new Uint8Array(await (await fetch('/api/photos/'+id)).arrayBuffer()),lv=new Uint8Array(W*H);
 for(let i=0;i<W*H;i++)lv[i]=i&1?b[i>>1]&15:b[i>>1]>>4;
 const c=$('big');c.style.display='block';draw(c,lv);c.scrollIntoView();}
async function loadSettings(){try{$('interval').value=(await (await fetch('/api/photo-settings')).json()).interval;}catch(e){}}
$('interval').onchange=async()=>{
 const r=await fetch('/api/photo-settings',{method:'POST',body:JSON.stringify({interval:$('interval').value})});
 $('imsg').textContent=r.ok?' 已保存':' '+await r.text();$('imsg').className='st '+(r.ok?'ok':'err');};
load();loadSettings();
</script></body></html>)HTML";
}  // namespace web
}  // namespace photos

#endif  // APPS_PHOTO_PHOTO_WEB_UI_H_
