const {JSDOM}=require('jsdom');const fs=require('fs');const {spawn}=require('child_process');const assert=require('assert/strict');
(async()=>{const fixture=spawn(process.env.JV_PYTHON||'python',['-m','tests.browser_fixture'],{stdio:['ignore','pipe','pipe']});let dom,cfg;
try{cfg=await new Promise((resolve,reject)=>{let b='';fixture.stdout.on('data',d=>{b+=d;for(const l of b.split('\n'))if(l.startsWith('{')){try{resolve(JSON.parse(l))}catch{}}});fixture.on('exit',()=>reject(Error('fixture exited')))});
const html=fs.readFileSync('vision/static/index.html','utf8');dom=new JSDOM(html,{url:cfg.url,runScripts:'outside-only',pretendToBeVisual:true});const w=dom.window,d=w.document;
let fixturePorts=[{device:'/dev/jv-test-only',description:'Fixture USB'}];
w.fetch=(p,o)=>String(p).endsWith('/serial-ports')?Promise.resolve({ok:true,json:async()=>({ok:true,ports:fixturePorts})}):fetch(new URL(p,cfg.url),o);w.confirm=()=>true;w.HTMLDialogElement.prototype.showModal=function(){this.open=true};w.HTMLDialogElement.prototype.close=function(){this.open=false};w.HTMLMediaElement.prototype.play=()=>Promise.resolve();w.HTMLMediaElement.prototype.pause=function(){};w.HTMLMediaElement.prototype.load=function(){};w.Hls={isSupported:()=>false};
w.eval(fs.readFileSync('vision/static/live-player.js','utf8'));
w.eval(fs.readFileSync('vision/static/app.js','utf8'));
async function wait(fn){for(let i=0;i<100;i++){if(fn())return;await new Promise(r=>setTimeout(r,30))}throw Error('DOM wait timeout')}
function fill(k,v){d.querySelector('#editor-fields [name="'+k+'"]').value=v}
async function save(){const f=d.querySelector('#editor-form'),b=f.querySelector('[type=submit]');f.dispatchEvent(new w.SubmitEvent('submit',{bubbles:true,cancelable:true,submitter:b}));await wait(()=>!d.querySelector('#editor').open);await new Promise(r=>setTimeout(r,80))}
await wait(()=>d.querySelector('#system-status').textContent.includes('FFmpeg'));
d.querySelector('#new-stb').click();fill('alias','DOM Hopper');fill('stb','R123');fill('ip','127.0.0.1');
await wait(()=>d.querySelector('[aria-label="Available host serial ports"]').options.length===2);
const portSelect=d.querySelector('[aria-label="Available host serial ports"]');portSelect.value='/dev/jv-test-only';portSelect.dispatchEvent(new w.Event('change'));assert.equal(d.querySelector('[name=com_port]').value,'/dev/jv-test-only');
fixturePorts=[];[...d.querySelectorAll('#editor-fields button')].find(b=>b.textContent==='Rescan ports').click();
await wait(()=>portSelect.textContent.includes('not detected'));assert.equal(d.querySelector('[name=com_port]').value,'/dev/jv-test-only');
fill('com_port','');await save();
// A manually added settop is immediately controllable without a remote profile.
assert.equal(d.querySelector('[data-key=guide]').disabled,false);
assert.equal(d.querySelector('#watch-linked').disabled,true);
const control=[...d.querySelectorAll('#stb-table button')].find(b=>b.textContent==='Control');assert.ok(control);control.click();
d.querySelector('[data-key=guide]').click();await wait(()=>d.querySelector('#command-status').textContent.includes('sgs_fixture'));
await wait(()=>d.querySelector('#command-history').textContent.includes('SGS command completed'));
assert.ok(d.querySelector('#console #command-history'));
// Every new control is wired without a stream; verify both chord choices.
const actualFetch=w.fetch, addedCommands=[];
w.fetch=async(p,o)=>{
 if(String(p).endsWith('/command')){addedCommands.push(JSON.parse(o.body));return {ok:true,json:async()=>({ok:true,via:'dart'})};}
 return actualFetch(p,o);
};
for(const key of ['sat','tv','aux','input','mic','reset','allup','unpair']){
 const b=d.querySelector('[data-key="'+key+'"]');assert.ok(b);assert.equal(b.disabled,false);
 b.click();await wait(()=>!b.disabled);assert.equal(addedCommands.at(-1).button,key);
}
assert.equal(addedCommands.at(-1).combo,'dvr');
d.querySelector('#unpair-combo').value='home';d.querySelector('[data-key=unpair]').click();
await wait(()=>!d.querySelector('[data-key=unpair]').disabled);assert.equal(addedCommands.at(-1).combo,'home');
w.fetch=actualFetch;
console.log('PASS: SAT, TV, AUX, INPUT, power reset, release all and both unpair choices dispatch without video');
// Create and operate a remote before any stream exists.
d.querySelector('#new-remote').click();fill('name','DOM Remote');fill('stb_alias','DOM Hopper');
assert.ok(d.querySelector('[aria-label="Available host serial ports"]'));assert.equal(d.querySelector('[name=remote]').value,'1');
assert.equal(d.querySelector('[name=stream_id]').value,'');assert.match(d.querySelector('[name=stream_id]').textContent,/remote control only/);await save();
assert.equal(d.querySelector('#watch-linked').disabled,true);assert.equal(d.querySelector('[data-key=guide]').disabled,false);
d.querySelector('[data-key=guide]').click();await wait(()=>d.querySelector('#command-status').textContent.includes('sgs_fixture'));
d.querySelector('#new-stream').click();fill('name','DOM video');fill('url',cfg.stream);await save();
const link=d.querySelector('#console-link');link.selectedIndex=1;link.dispatchEvent(new w.Event('change'));await wait(()=>!d.querySelector('#watch-linked').disabled);
await wait(()=>d.querySelector('#remote-table').textContent.includes('DOM Remote-stream'));
d.querySelector('[data-key=guide]').click();await wait(()=>d.querySelector('#command-status').textContent.includes('sgs_fixture'));
// A hung then failed video request must leave device keys responsive.
const normalFetch=w.fetch;let finishPlayback;
w.fetch=(p,o)=>String(p).endsWith('/play')?new Promise(resolve=>{finishPlayback=resolve}):normalFetch(p,o);
d.querySelector('#watch-linked').click();await wait(()=>d.querySelectorAll('video').length===1&&finishPlayback);
assert.equal(d.querySelector('[data-key=guide]').disabled,false);
d.querySelector('[data-key=guide]').click();await wait(()=>d.querySelector('#command-status').textContent.includes('sgs_fixture'));
finishPlayback({ok:false,json:async()=>({ok:false,error:'Stream offline'})});
await wait(()=>d.querySelector('.video-state').textContent==='Stream offline');
assert.equal(d.querySelector('[data-key=guide]').disabled,false);
d.querySelector('[data-key=guide]').click();await wait(()=>d.querySelector('#command-status').textContent.includes('sgs_fixture'));
w.fetch=normalFetch;
console.log('PASS: direct settop control, command during hung video, command after video failure with link retained');

const j=await(await fetch(cfg.url+'/_test/commands')).json();assert.equal(j.commands[0].args[1],'DOM Hopper');assert.equal(j.commands[0].args[2],'guide');
assert.equal(d.querySelector('.video-head strong').textContent,'DOM Remote-stream');assert.equal(d.querySelector('.go-live').textContent,'Go live');assert.equal(d.querySelector('#toast').className,'');await wait(()=>d.querySelector('#video-count').textContent==='1 selected');console.log('PASS: DOM UI bootstrap, settop/stream/remote forms, persistent links, selected video window, command dispatch');

// Clicking an offline video selects its remote without sending a command.
async function post(path,data){const response=await fetch(cfg.url+path,{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(data)});const json=await response.json();assert.equal(response.ok,true,JSON.stringify(json));return json;}
await post('/api/vision/settops',{alias:'Second Hopper',protocol:'SGS',ip:'127.0.0.1',role:'hopper'});
const secondStream=(await post('/api/vision/streams',{name:'Second video',url:cfg.stream+'?second=1'})).stream;
const secondRemote=(await post('/api/vision/remotes',{name:'Second Remote',stb_alias:'Second Hopper',stream_id:secondStream.id})).remote;
w.fetch=(p,o)=>String(p).endsWith('/play')?Promise.resolve({ok:false,json:async()=>({ok:false,error:'Fixture offline'})}):normalFetch(p,o);
d.querySelector('#refresh').click();await wait(()=>d.querySelector('#remote-table').textContent.includes('Second Remote'));
d.querySelectorAll('#stream-checks input')[1].click();await wait(()=>d.querySelectorAll('video').length===2);
const cards=[...d.querySelectorAll('.video-card')],firstCard=cards[0],secondCard=cards[1];
assert.equal(d.querySelector('#active-remote-name').textContent,'Second Remote');
const beforeSelect=await(await fetch(cfg.url+'/_test/commands')).json();
firstCard.querySelector('video').click();assert.equal(d.querySelector('#active-remote-name').textContent,'DOM Remote');
assert.equal(firstCard.classList.contains('remote-active'),true);assert.equal(secondCard.classList.contains('remote-active'),false);
assert.deepEqual((await(await fetch(cfg.url+'/_test/commands')).json()).commands,beforeSelect.commands);
secondCard.dispatchEvent(new w.KeyboardEvent('keydown',{key:'Enter',bubbles:true}));assert.equal(d.querySelector('#active-remote-name').textContent,'Second Remote');
firstCard.querySelector('.go-live').click();assert.equal(d.querySelector('#active-remote-name').textContent,'Second Remote');
d.querySelector('[data-key=guide]').click();await wait(()=>d.querySelector('#command-status').textContent.includes('sgs_fixture'));
const afterSelection=await(await fetch(cfg.url+'/_test/commands')).json();assert.equal(afterSelection.commands.at(-1).args[1],'Second Hopper');
// Two remotes share the first stream: do not guess which STB to control.
const firstId=firstCard.dataset.streamId;
await post('/api/vision/remotes',{name:'Shared Remote',stb_alias:'Second Hopper',stream_id:firstId});
d.querySelector('#refresh').click();await wait(()=>firstCard.querySelectorAll('.video-control').length===2);
firstCard.querySelector('video').click();assert.equal(d.querySelector('#active-remote-name').textContent,'Second Remote');
[...firstCard.querySelectorAll('.video-control')].find(b=>b.textContent.includes('Shared Remote')).click();
assert.equal(d.querySelector('#active-remote-name').textContent,'Shared Remote');
assert.equal(firstCard.querySelector('[aria-pressed=true]').textContent,'Controlling: Shared Remote');
// Removing a link updates existing cards; a video-only window does not choose another remote.
await fetch(cfg.url+'/api/vision/remotes/'+secondRemote.id,{method:'PUT',headers:{'Content-Type':'application/json'},body:JSON.stringify({...secondRemote,stream_id:null})});
d.querySelector('#refresh').click();await wait(()=>secondCard.querySelector('.video-targets').textContent.includes('no remote linked'));
secondCard.querySelector('video').click();assert.equal(d.querySelector('#active-remote-name').textContent,'Shared Remote');
w.fetch=normalFetch;
console.log('PASS: video/keyboard remote selection, actual command target, active highlighting, shared choices, unlinked video, live-button independence');

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
