#ifndef ZOS_WEB_UI_H_
#define ZOS_WEB_UI_H_

// One generic page. The home view lists only the apps that have settings;
// opening one shows its form (built from /api/apps). Hash routes:
//   #/            list     #/app/<id>   one app's settings
static const char kWebUiHtml[] = R"HTML(<!doctype html>
<html lang="zh-CN"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>ZECTRIX OS</title>
<style>
body{font:15px system-ui,sans-serif;max-width:640px;margin:0 auto;padding:12px;background:#f4f4f4;color:#111}
h1{font-size:20px}.card{background:#fff;border:1px solid #ccc;border-radius:6px;padding:12px;margin:12px 0}
.card h2{font-size:16px;margin:0 0 2px}.sum{color:#666}
a.row{display:flex;justify-content:space-between;align-items:center;text-decoration:none;color:inherit}
a.row:after{content:"\203A";font-size:24px;color:#999}
.back{display:inline-block;margin:4px 0 8px;color:#06c;text-decoration:none}
label{display:block;margin:12px 0 2px;font-weight:600}.hint{color:#777;font-size:12px;margin-top:2px}
input[type=text],input[type=number],select{width:100%;box-sizing:border-box;padding:6px;font-size:15px}
button{margin-top:14px;padding:8px 16px;font-size:15px;background:#111;color:#fff;border:0;border-radius:4px}
.st{color:#555;font-size:13px}.ok{color:#080}.err{color:#b00}
</style></head><body>
<h1>ZECTRIX OS</h1><div id="status" class="st"></div><div id="view"></div>
<script>
const $=(t,p={},c=[])=>{const e=Object.assign(document.createElement(t),p);c.forEach(x=>e.append(x));return e};
const view=document.getElementById('view');
let apps=[];
function list(){
 view.textContent='';
 const configurable=apps.filter(a=>a.fields.length);
 if(!configurable.length){view.append($('div',{className:'hint',textContent:'暂无可配置的应用。'}));return;}
 configurable.forEach(a=>{
  const row=$('a',{className:'row',href:'#/app/'+a.id},[$('div',{},[$('h2',{textContent:a.title}),$('div',{className:'sum',textContent:a.summary})])]);
  view.append($('div',{className:'card'},[row]));
 });
}
function detail(id){
 const a=apps.find(x=>x.id===id&&x.fields.length);
 if(!a){location.hash='#/';return;}
 view.textContent='';
 const card=$('div',{className:'card'},[$('h2',{textContent:a.title}),$('div',{className:'sum',textContent:a.summary})]);
 const inputs={};
 a.fields.forEach(f=>{
  let el;
  if(f.type==='bool'){el=$('input',{type:'checkbox',checked:f.value==='1'});}
  else if(f.type==='choice'){el=$('select');f.choices.forEach(c=>el.append($('option',{value:c,textContent:c,selected:c===f.value})));}
  else{el=$('input',{type:f.type==='int'?'number':'text',value:f.value});}
  inputs[f.key]=[f,el];
  card.append($('label',{textContent:f.label}),el);
  if(f.hint)card.append($('div',{className:'hint',textContent:f.hint}));
 });
 const msg=$('span',{className:'st'});
 card.append($('button',{textContent:'保存',onclick:async()=>{
  const body={};
  for(const k in inputs){const[f,el]=inputs[k];body[k]=f.type==='bool'?(el.checked?'1':'0'):el.value;}
  const r=await fetch('/api/apps/'+a.id+'/config',{method:'POST',body:JSON.stringify(body)});
  msg.className=r.ok?'ok':'err';msg.textContent=r.ok?' 已保存':' '+await r.text();
  if(r.ok)apps=await (await fetch('/api/apps')).json();}}),msg);
 view.append($('a',{className:'back',href:'#/',textContent:'‹ 返回应用列表'}),card);
}
function route(){
 const m=location.hash.match(/^#\/app\/([a-z0-9_]+)$/);
 if(m)detail(m[1]);else list();
}
async function load(){
 const s=await (await fetch('/api/status')).json();
 document.getElementById('status').textContent='Wi-Fi：'+s.ssid+'  IP：'+s.ip+'  电量：'+(s.battery<0?'未知':s.battery+'%');
 apps=await (await fetch('/api/apps')).json();
 route();
}
addEventListener('hashchange',route);
load();
</script></body></html>)HTML";

#endif  // ZOS_WEB_UI_H_
