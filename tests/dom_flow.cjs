const {JSDOM}=require('jsdom');const fs=require('fs');const {spawn}=require('child_process');const assert=require('assert/strict');
(async()=>{const fixture=spawn(process.env.JV_PYTHON||'python',['-m','tests.browser_fixture'],{stdio:['ignore','pipe','pipe']});let dom,cfg;
try{cfg=await new Promise((resolve,reject)=>{let b='';fixture.stdout.on('data',d=>{b+=d;for(const l of b.split('\n'))if(l.startsWith('{')){try{resolve(JSON.parse(l))}catch{}}});fixture.on('exit',()=>reject(Error('fixture exited')))});
const html=fs.readFileSync('vision/static/index.html','utf8');dom=new JSDOM(html,{url:cfg.url,runScripts:'outside-only',pretendToBeVisual:true});const w=dom.window,d=w.document;
w.fetch=(p,o)=>fetch(new URL(p,cfg.url),o);w.confirm=()=>true;w.HTMLDialogElement.prototype.showModal=function(){this.open=true};w.HTMLDialogElement.prototype.close=function(){this.open=false};w.HTMLMediaElement.prototype.play=()=>Promise.resolve();w.HTMLMediaElement.prototype.pause=function(){};w.HTMLMediaElement.prototype.load=function(){};w.Hls={isSupported:()=>false};
w.eval(fs.readFileSync('vision/static/app.js','utf8'));
async function wait(fn){for(let i=0;i<100;i++){if(fn())return;await new Promise(r=>setTimeout(r,30))}throw Error('DOM wait timeout')}
function fill(k,v){d.querySelector('#editor-fields [name="'+k+'"]').value=v}
async function save(){const f=d.querySelector('#editor-form'),b=f.querySelector('[type=submit]');f.dispatchEvent(new w.SubmitEvent('submit',{bubbles:true,cancelable:true,submitter:b}));await wait(()=>!d.querySelector('#editor').open);await new Promise(r=>setTimeout(r,80))}
await wait(()=>d.querySelector('#system-status').textContent.includes('FFmpeg'));
d.querySelector('#new-stb').click();fill('alias','DOM Hopper');fill('stb','R123');fill('ip','127.0.0.1');await save();
// Create and operate a remote before any stream exists.
d.querySelector('#new-remote').click();fill('name','DOM Remote');fill('stb_alias','DOM Hopper');
assert.equal(d.querySelector('[name=stream_id]').value,'');assert.match(d.querySelector('[name=stream_id]').textContent,/remote control only/);await save();
assert.equal(d.querySelector('#watch-linked').disabled,true);assert.equal(d.querySelector('[data-key=guide]').disabled,false);
d.querySelector('[data-key=guide]').click();await wait(()=>d.querySelector('#command-status').textContent.includes('sgs_fixture'));
d.querySelector('#new-stream').click();fill('name','DOM video');fill('url',cfg.stream);await save();
const link=d.querySelector('#console-link');link.selectedIndex=1;link.dispatchEvent(new w.Event('change'));await wait(()=>!d.querySelector('#watch-linked').disabled);
await wait(()=>d.querySelector('#remote-table').textContent.includes('DOM Remote-stream'));
d.querySelector('[data-key=guide]').click();await wait(()=>d.querySelector('#command-status').textContent.includes('sgs_fixture'));
d.querySelector('#watch-linked').click();await wait(()=>d.querySelectorAll('video').length===1);
const j=await(await fetch(cfg.url+'/_test/commands')).json();assert.equal(j.commands[0].args[1],'DOM Hopper');assert.equal(j.commands[0].args[2],'guide');
assert.equal(d.querySelector('.video-head strong').textContent,'DOM Remote-stream');assert.equal(d.querySelector('#toast').className,'');await wait(()=>d.querySelector('#video-count').textContent==='1 selected');console.log('PASS: DOM UI bootstrap, settop/stream/remote forms, persistent links, selected video window, command dispatch');

const picker=d.querySelector('#config-file');let pickerClicked=false;picker.click=()=>{pickerClicked=true};d.querySelector('#import-config').click();assert.equal(pickerClicked,true);
const config=JSON.stringify({stbs:{'Imported Hopper':{protocol:'SGS',ip:'192.0.2.1'}}});
async function choose(text){Object.defineProperty(picker,'files',{configurable:true,value:[{name:'base.txt',size:text.length,text:async()=>text}]});await picker.onchange();}
await choose(config);assert.equal(d.querySelector('#apply-import').disabled,false);assert.match(d.querySelector('#import-summary').textContent,/1 settops to add/);
let inventory=await(await fetch(cfg.url+'/api/vision/state')).json();assert.equal(inventory.stbs['Imported Hopper'],undefined);
d.querySelector('#cancel-import').click();assert.equal(d.querySelector('#import-dialog').open,false);
await choose(config);d.querySelector('#apply-import').click();await wait(()=>!d.querySelector('#import-dialog').open);await wait(()=>d.querySelector('#remote-table').textContent.includes('Imported Hopper'));await wait(()=>d.querySelector('#cancel-import').disabled===false);
await choose(config);assert.match(d.querySelector('#import-summary').textContent,/0 settops to add, 1 existing settops skipped/);d.querySelector('#cancel-import').click();
await choose('{invalid');assert.equal(d.querySelector('#apply-import').disabled,true);assert.match(d.querySelector('#import-error').textContent,/not valid JSON/);d.querySelector('#cancel-import').click();
console.log('PASS: import button opens file input; local file preview, cancel, apply, repeated import, invalid JSON');
}finally{dom?.window.close();if(cfg){try{await fetch(cfg.url+'/_test/shutdown',{method:'POST',headers:{'Content-Type':'application/json'},body:'{}'});}catch{fixture.kill('SIGTERM')}}else fixture.kill('SIGTERM')}})().catch(e=>{console.error(e);process.exitCode=1});
