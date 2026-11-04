#ifndef ZOS_WEB_UI_H_
#define ZOS_WEB_UI_H_

// One generic page: it builds a form for every app from /api/apps.
static const char kWebUiHtml[] = R"HTML(<!doctype html>
<html lang="zh-CN"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>ZECTRIX OS</title>
<style>
body{font:15px system-ui,sans-serif;max-width:640px;margin:0 auto;padding:12px;background:#f4f4f4;color:#111}
h1{font-size:20px}.card{background:#fff;border:1px solid #ccc;border-radius:6px;padding:12px;margin:12px 0}
.card h2{font-size:16px;margin:0 0 2px}.sum{color:#666;margin-bottom:8px}
label{display:block;margin:8px 0 2px;font-weight:600}.hint{color:#777;font-size:12px}
input[type=text],input[type=number],select{width:100%;box-sizing:border-box;padding:6px;font-size:15px}
button{margin-top:10px;padding:8px 16px;font-size:15px;background:#111;color:#fff;border:0;border-radius:4px}
.st{color:#555;font-size:13px}.ok{color:#080}.err{color:#b00}
</style></head><body>
<h1>ZECTRIX OS 应用设置</h1><div id="status" class="st"></div><div id="apps"></div>
<script>
const $=(t,p={},c=[])=>{const e=Object.assign(document.createElement(t),p);c.forEach(x=>e.append(x));return e};
async function load(){
 const s=await (await fetch('/api/status')).json();
 document.getElementById('status').textContent='Wi-Fi：'+s.ssid+'  IP：'+s.ip+'  电量：'+(s.battery<0?'未知':s.battery+'%');
 const apps=await (await fetch('/api/apps')).json();
 const root=document.getElementById('apps');root.textContent='';
 apps.forEach(a=>{
  const card=$('div',{className:'card'},[$('h2',{textContent:a.title}),$('div',{className:'sum',textContent:a.summary})]);
  const inputs={};
  if(!a.fields.length)card.append($('div',{className:'hint',textContent:'没有可设置的选项。'}));
  a.fields.forEach(f=>{
   let el;
   if(f.type==='bool'){el=$('input',{type:'checkbox',checked:f.value==='1'});}
   else if(f.type==='choice'){el=$('select');f.choices.forEach(c=>el.append($('option',{value:c,textContent:c,selected:c===f.value})));}
   else{el=$('input',{type:f.type==='int'?'number':'text',value:f.value});}
   inputs[f.key]=[f,el];
   card.append($('label',{textContent:f.label}),el);
   if(f.hint)card.append($('div',{className:'hint',textContent:f.hint}));
  });
  if(a.fields.length){
   const msg=$('span',{className:'st'});
   const btn=$('button',{textContent:'保存',onclick:async()=>{
    const body={};for(const k in inputs){const[f,el]=inputs[k];body[k]=f.type==='bool'?(el.checked?'1':'0'):el.value;}
    const r=await fetch('/api/apps/'+a.id+'/config',{method:'POST',body:JSON.stringify(body)});
    msg.className=r.ok?'ok':'err';msg.textContent=r.ok?' 已保存':' '+await r.text();}});
   card.append(btn,msg);
  }
  root.append(card);
 });
}
load();
</script></body></html>)HTML";

#endif  // ZOS_WEB_UI_H_
