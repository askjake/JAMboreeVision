'use strict';
const $=s=>document.querySelector(s), el=(tag,text,cls)=>{const n=document.createElement(tag);if(text!==undefined)n.textContent=text;if(cls)n.className=cls;return n;};
let state={stbs:{},remotes:[],streams:[]}, active=null, busy=false, pairAlias=null, editorSave=null;
let multiControl=false;const selectedTargets=new Set();
function commandAliases(){return multiControl?[...selectedTargets].filter(a=>state.stbs[a]):(activeTarget()&&!activeTarget().missing_settop?[activeTarget().stb_alias]:[]);}
function controlTargets(){return [...state.remotes,...Object.keys(state.stbs).filter(alias=>!state.remotes.some(r=>r.stb_alias===alias)).map(alias=>({id:'settop:'+alias,name:alias,stb_alias:alias,stream_id:null,direct:true}))];}
function activeTarget(){return controlTargets().find(r=>r.id===active);}
function selectTarget(id){const target=controlTargets().find(r=>r.id===id);if(!target||target.missing_settop)return;if(multiControl&&!selectedTargets.has(target.stb_alias)){if(selectedTargets.size>=32){notice('Select at most 32 settops',true);return;}selectedTargets.add(target.stb_alias);}active=id;renderRemotes();renderActive();run(refreshHistory)();}
const players=new Map(), viewer=(globalThis.crypto?.randomUUID?.() || 'viewer-'+Date.now()+'-'+Math.random().toString(36).slice(2));
let selected=new Set(JSON.parse(localStorage.getItem('jv-selected')||'[]')), toastTimer;
async function api(path,method='GET',data){const r=await fetch(path,{method,headers:method==='GET'?{}:{'Content-Type':'application/json'},body:method==='GET'?undefined:JSON.stringify(data||{})});const j=await r.json();if(!r.ok||j.ok===false)throw Error(j.error||j.message||'Request failed');return j;}
function notice(msg,error=false){const t=$('#toast');t.textContent=msg;t.className=error?'error':'';t.style.display='block';clearTimeout(toastTimer);toastTimer=setTimeout(()=>t.style.display='none',6000);}
function run(fn){return async(...args)=>{try{await fn(...args);}catch(e){notice(e.message,true);}};}
function button(text,fn,cls){const b=el('button',text,cls);b.type='button';b.onclick=run(fn);return b;}
function option(value,label){const o=el('option',label);o.value=value;return o;}
function streamOptions(sel,value){sel.replaceChildren(option('','No stream — remote control only'));for(const s of state.streams)sel.append(option(s.id,s.name));sel.value=value||'';}
function labelFor(s){const aliases=state.remotes.filter(r=>r.stream_id===s.id).map(r=>r.name+'-stream');return aliases.length?aliases.join(' · '):s.name;}
function tab(id){document.querySelectorAll('.tab').forEach(n=>n.classList.toggle('active',n.id===id));document.querySelectorAll('nav button').forEach(n=>n.classList.toggle('active',n.dataset.tab===id));history.replaceState(null,'','#'+id);}
document.querySelectorAll('nav button').forEach(b=>b.onclick=()=>tab(b.dataset.tab));
function td(...nodes){const c=el('td');for(const n of nodes)c.append(typeof n==='string'?document.createTextNode(n):n);return c;}
function detail(a,b){const d=el('div');d.append(el('strong',a),el('small',b));return d;}
async function refresh(){state=await api('/api/vision/state');for(const alias of selectedTargets)if(!state.stbs[alias])selectedTargets.delete(alias);if(!controlTargets().some(r=>r.id===active))active=controlTargets()[0]?.id||null;selected=new Set([...selected].filter(id=>state.streams.some(s=>s.id===id)));render();run(refreshHistory)();await syncPlayers();}
function render(){
 $('#system-status').textContent=`${state.remotes.length} remotes · ${state.streams.length} streams · ${state.ffmpeg?'FFmpeg ready':'Install FFmpeg for video'}`;
 $('#remote-count').textContent=controlTargets().length;renderRemotes();renderChecks();renderActive();renderAssets();renderStreams();renderScan(state.discovery);
}
function renderRemotes(){
 const list=$('#remote-list');list.replaceChildren();const q=$('#remote-search').value.toLowerCase();
 for(const r of controlTargets().filter(r=>r.name.toLowerCase().includes(q))){
  const b=button('',()=>selectTarget(r.id),'remote-item'+(active===r.id?' active':''));b.append(el('strong',r.name),el('small',r.stb_alias+(r.stream_id?' · video linked':'')));
  if(multiControl){const row=el('div',undefined,'target-row'),check=el('input');check.type='checkbox';check.checked=selectedTargets.has(r.stb_alias);check.disabled=!!r.missing_settop;check.setAttribute('aria-label','Control '+r.name);check.dataset.alias=r.stb_alias;
   check.onchange=()=>{if(check.checked){if(selectedTargets.size>=32){check.checked=false;notice('Select at most 32 settops',true);return;}selectedTargets.add(r.stb_alias);}else selectedTargets.delete(r.stb_alias);renderRemotes();renderActive();run(refreshHistory)();};row.append(check,b);list.append(row);
  }else list.append(b);
 }
 if(!controlTargets().length)list.append(el('p','Add or import a settop in Settops & remotes to enable control.','muted'));
 $('#multi-actions').hidden=!multiControl;
 $('#target-summary').textContent=multiControl?commandAliases().length+' settops selected · every key applies to this group':'Single-target control';
}
$('#multi-control').onchange=()=>{multiControl=$('#multi-control').checked;selectedTargets.clear();if(multiControl&&activeTarget()&&!activeTarget().missing_settop)selectedTargets.add(activeTarget().stb_alias);$('#batch-results').replaceChildren();renderRemotes();renderActive();run(refreshHistory)();};
$('#clear-targets').onclick=()=>{selectedTargets.clear();renderRemotes();renderActive();run(refreshHistory)();};
$('#select-visible-targets').onclick=()=>{const q=$('#remote-search').value.toLowerCase();for(const r of controlTargets().filter(r=>!r.missing_settop&&r.name.toLowerCase().includes(q))){if(selectedTargets.size>=32)break;selectedTargets.add(r.stb_alias);}renderRemotes();renderActive();run(refreshHistory)();};
function renderChecks(){const box=$('#stream-checks');box.replaceChildren();for(const s of state.streams){const l=el('label',undefined,'stream-check'),c=el('input');c.type='checkbox';c.checked=selected.has(s.id);c.onchange=run(()=>toggleStream(s.id,c.checked));const title=el('span',labelFor(s));title.append(el('small',s.name));l.append(c,title);box.append(l);}if(!state.streams.length)box.append(el('p','Add or discover streams in the Streams tab.','muted'));}
function renderActive(){
 const r=activeTarget(),aliases=commandAliases();
 $('#active-remote-name').textContent=multiControl?aliases.length+' settops selected':r?.name||'Select a remote';
 $('#active-remote-detail').textContent=multiControl?(aliases.join(' · ')||'Select target checkboxes to enable group control.'):(r?(r.missing_settop?'Settop missing — edit this remote':`${r.stb_alias} · ${state.stbs[r.stb_alias]?.protocol||''}`):'Add a settop to begin; video is optional.');
 streamOptions($('#console-link'),r?.stream_id);$('#console-link').disabled=!r||multiControl;
 const linked=multiControl?controlTargets().some(t=>aliases.includes(t.stb_alias)&&t.stream_id):!!r?.stream_id;
 $('#watch-linked').disabled=!linked;$('#watch-linked').textContent=multiControl?'Watch selected streams':'Watch linked stream';
 document.querySelectorAll('[data-key]').forEach(b=>b.disabled=!aliases.length||busy);renderVideoTargets();
}
async function linkRemote(r,id){const result=await api('/api/vision/remotes'+(r.direct?'':'/'+r.id),r.direct?'POST':'PUT',{name:r.name,stb_alias:r.stb_alias,stream_id:id});if(r.direct)active=result.remote.id;await refresh();notice('Stream link saved');}
$('#console-link').onchange=run(()=>linkRemote(activeTarget(),$('#console-link').value));
$('#watch-linked').onclick=run(async()=>{const ids=multiControl?[...new Set(controlTargets().filter(r=>commandAliases().includes(r.stb_alias)&&r.stream_id).map(r=>r.stream_id))]:[activeTarget().stream_id];for(const id of ids)await toggleStream(id,true);});
$('#remote-search').oninput=renderRemotes;$('#refresh').onclick=run(refresh);
for(let n=1;n<=9;n++){const b=el('button',String(n));b.dataset.key=n;$('#numbers').append(b);}
document.querySelectorAll('[data-key]').forEach(b=>b.onclick=run(async()=>{
 const aliases=commandAliases(),batch=multiControl,target=activeTarget();if(!aliases.length||busy)return;
 const data={button:b.dataset.key,duration:Number($('#duration').value),transport:$('#transport').value,combo:$('#unpair-combo').value};
 busy=true;renderActive();$('#batch-results').replaceChildren();$('#command-status').textContent='Sending '+b.textContent+' to '+aliases.join(', ')+'…';
 try{
  if(batch){
   const j=await api('/api/vision/commands/batch','POST',{...data,aliases});
   $('#command-status').textContent=`${j.sent} sent · ${j.failed} failed · ${b.textContent}`;
   for(const r of j.results){const row=el('p',r.alias+': '+(r.ok?'Sent via '+r.via+' · '+r.feedback:'Failed · '+r.error),r.ok?'muted':'error');row.dataset.alias=r.alias;row.dataset.status=r.status;$('#batch-results').append(row);}
  }else{
   const route=target.direct?`/api/vision/settops/${encodeURIComponent(target.stb_alias)}/command`:`/api/vision/remotes/${target.id}/command`;
   const j=await api(route,'POST',data);$('#command-status').textContent=b.dataset.key==='unpair'?target.name+': RF unpair sequence sent; verify the remote is unpaired.':`${target.name}: sent via ${j.via} · ${j.feedback||'Command completed'}`;
  }
 }catch(e){$('#command-status').textContent=e.message;throw e;}finally{busy=false;renderActive();run(refreshHistory)();}
}));
async function toggleStream(id,on){if(on){selected.add(id);selectVideoRemote(id);}else selected.delete(id);localStorage.setItem('jv-selected',JSON.stringify([...selected]));renderChecks();await syncPlayers();}
async function syncPlayers(){for(const [id,p] of players){const s=state.streams.find(s=>s.id===id);if(!selected.has(id)||!s||p.source!==s.url||p.mode!==s.mode)await removePlayer(id);}for(const id of selected){if(!players.has(id)){const s=state.streams.find(s=>s.id===id);if(s)await addPlayer(s);}}for(const [id,p] of players){const s=state.streams.find(s=>s.id===id);if(s)p.title.textContent=labelFor(s);}renderVideoTargets();$('#video-count').textContent=players.size+' selected';$('#video-empty').style.display=players.size?'none':'block';}
function videoTargets(id){return controlTargets().filter(r=>r.stream_id===id&&!r.missing_settop);}
function selectVideoRemote(id){
 const linked=videoTargets(id);
 if(linked.length===1){selectTarget(linked[0].id);return;}
 if(linked.length>1){const current=linked.find(r=>r.id===active);if(current){if(multiControl)selectTarget(current.id);}else notice('This stream has multiple remotes. Choose its Control button below the video.');}
}
function renderVideoTargets(){
 for(const [id,p]of players){
  const linked=videoTargets(id),key=JSON.stringify(linked.map(r=>[r.id,r.name,r.stb_alias]));
  if(p.linksKey!==key){
   p.linksKey=key;p.targets.replaceChildren();
   if(!linked.length)p.targets.append(el('span','Video only · no remote linked','muted'));
   for(const r of linked){const b=button('',()=>selectTarget(r.id),'video-control');b.dataset.remoteId=r.id;b.title=r.name+' controls '+r.stb_alias;p.targets.append(b);}
  }
  for(const b of p.targets.querySelectorAll('button')){
   const r=linked.find(r=>r.id===b.dataset.remoteId),chosen=multiControl?selectedTargets.has(r.stb_alias):r.id===active;
   b.textContent=(chosen?(multiControl?'Selected: ':'Controlling: '):'Control: ')+r.name;
   b.classList.toggle('active',chosen);b.setAttribute('aria-pressed',String(chosen));
  }
  p.card.classList.toggle('remote-active',linked.some(r=>multiControl?selectedTargets.has(r.stb_alias):r.id===active));
  p.card.setAttribute('aria-label',p.title.textContent+(linked.length?' · select linked remote':' · video only'));
 }
}
async function removePlayer(id){const p=players.get(id);if(!p)return;players.delete(id);p.dead=true;clearTimeout(p.retry);p.live?.destroy();p.hls?.destroy();p.video.pause();p.video.removeAttribute('src');p.video.load();p.card.remove();try{await api(`/api/vision/streams/${id}/release`,'POST',{viewer});}catch{} }
async function addPlayer(s){
 const card=el('article',undefined,'video-card'),head=el('div',undefined,'video-head'),title=el('strong',labelFor(s)),video=el('video'),status=el('div','Starting stream…','video-state'),targets=el('div',undefined,'video-targets');
 video.controls=true;video.muted=true;video.playsInline=true;
 const live=button('Go live',()=>players.get(s.id)?.live?.goLive(),'go-live');
 live.title='Return to the latest available video and resume following live';
 head.append(title,live,button('Retry',()=>loadPlayer(s.id)),button('✕',()=>toggleStream(s.id,false)));
 card.tabIndex=0;card.dataset.streamId=s.id;
 card.addEventListener('click',e=>{if(!e.target.closest('button,input,select,a'))selectVideoRemote(s.id);});
 card.addEventListener('keydown',e=>{if(e.target===card&&['Enter',' '].includes(e.key)){e.preventDefault();selectVideoRemote(s.id);}});
 card.append(head,video,targets,status);$('#video-grid').append(card);
 const p={card,video,status,title,targets,linksKey:null,source:s.url,mode:s.mode,hls:null,live:null,retry:null,dead:false,loading:false};players.set(s.id,p);renderVideoTargets();await loadPlayer(s.id);
}
async function loadPlayer(id){
 const p=players.get(id);if(!p||p.loading)return;p.loading=true;clearTimeout(p.retry);
 const saved=p.live?.snapshot();p.live?.destroy();p.hls?.destroy();p.hls=null;
 p.video.onerror=null;p.video.pause();p.video.removeAttribute('src');p.video.load();
 p.live=createLivePlayback(p.video,{saved,getHls:()=>p.hls,onStatus:({kind,text})=>{if(!p.dead){p.status.dataset.playback=kind;p.status.textContent=text;}}});
 p.status.dataset.playback='connecting';p.status.textContent='Connecting…';
 try{const j=await api(`/api/vision/streams/${id}/play`,'POST',{viewer});if(p.dead)return;p.url=j.url;
 const retry=()=>{if(!p.dead){p.live.destroy();p.status.dataset.playback='buffering';p.status.textContent='Waiting for video — retrying…';clearTimeout(p.retry);p.retry=setTimeout(()=>loadPlayer(id),5000);}};
 if(globalThis.Hls?.isSupported()){
  const h=new Hls({liveSyncDurationCount:3,maxBufferLength:12,maxLiveSyncPlaybackRate:1});p.hls=h;
  h.on(Hls.Events.MANIFEST_PARSED,()=>p.live.start());
  h.on(Hls.Events.LEVEL_UPDATED,()=>p.live.update());
  h.on(Hls.Events.ERROR,(_,d)=>{if(d.fatal)retry();});
  h.loadSource(j.url);h.attachMedia(p.video);
 }else if(p.video.canPlayType('application/vnd.apple.mpegurl')){
  p.video.src=j.url;p.video.onerror=retry;p.live.start();
 }else {p.live.destroy();p.status.textContent='HLS playback unavailable. Use a modern browser.';}
 }catch(e){p.live.destroy();if(!p.dead){p.status.dataset.playback='error';p.status.textContent=e.message;}}finally{p.loading=false;}
}
setInterval(async()=>{for(const [id,p] of players){try{const j=await api(`/api/vision/streams/${id}/play`,'POST',{viewer});if(!p.dead&&p.url&&j.url!==p.url)await loadPlayer(id);}catch(e){p.status.textContent=e.message;}}},20000);
window.addEventListener('pagehide',()=>{for(const id of players.keys())navigator.sendBeacon(`/api/vision/streams/${id}/release`,new Blob([JSON.stringify({viewer})],{type:'application/json'}));});
function renderAssets(){const st=$('#stb-table');st.replaceChildren();for(const [alias,s] of Object.entries(state.stbs)){const tr=el('tr');tr.append(td(detail(alias,s.stb||'No receiver ID')),td(detail(s.ip||'—',s.mac||'MAC learned after verified SGS success')),td(detail(s.protocol,`DART ${s.remote||'—'} · ${s.com_port||'no COM'}`)),td(s.role==='joey'?'Joey → '+s.host:'Hopper / standalone'),td(button('Control',()=>{const target=controlTargets().find(r=>r.stb_alias===alias);selectTarget(target.id);tab('console');}),button('Edit',()=>editStb(alias)),button('Pair',()=>pair(alias)),button('Status',async()=>notice(JSON.stringify(await api('/api/transports/'+encodeURIComponent(alias))))),button('Recover IP',async()=>{await api('/api/ip_recovery/run','POST',{alias});notice('IP recovery started');}),button('Delete',async()=>{if(confirm('Delete settop '+alias+'?')){await api('/api/vision/settops/'+encodeURIComponent(alias),'DELETE');await refresh();}},'danger')));st.append(tr);}
 const rt=$('#remote-table');rt.replaceChildren();for(const r of state.remotes){const select=el('select');select.setAttribute('aria-label','Stream for '+r.name);streamOptions(select,r.stream_id);select.onchange=run(()=>linkRemote(r,select.value));const tr=el('tr');tr.append(td(r.name),td(r.stb_alias),td(select),td(el('code',r.stream_alias||'Not linked')),td(button('Edit',()=>editRemote(r)),button('Delete',async()=>{if(confirm('Delete remote '+r.name+'?')){await api('/api/vision/remotes/'+r.id,'DELETE');await refresh();}},'danger')));rt.append(tr);}}
function renderStreams(){const t=$('#streams-table');t.replaceChildren();for(const s of state.streams){const tr=el('tr');tr.append(td(detail(s.name,s.url)),td(state.remotes.filter(r=>r.stream_id===s.id).map(r=>r.name+'-stream').join(', ')||'—'),td(s.mode==='copy'?'Copy video':'H.264 transcode'),td(button('Watch',async()=>{tab('console');await toggleStream(s.id,true);}),button('Probe',async()=>{notice('Probing '+s.name+'…');const j=await api('/api/vision/streams/'+s.id+'/probe','POST');notice('Video detected: '+j.tracks.filter(t=>t.codec_type==='video').map(t=>t.codec_name+' '+t.width+'×'+t.height).join(', '));}),button('Edit',()=>editStream(s)),button('Delete',async()=>{if(confirm('Delete '+s.name+' and clear its remote links?')){await api('/api/vision/streams/'+s.id,'DELETE');await refresh();}},'danger')));t.append(tr);}}
function portField(value=''){
 const box=el('label','Host USB / COM port','wide'),select=el('select'),input=el('input'),status=el('small','Ports belong to the app server. You can also enter a path manually.');
 select.setAttribute('aria-label','Available host serial ports');input.name='com_port';input.value=value;input.placeholder='Manual port, e.g. /dev/ttyUSB0 or COM3';
 select.onchange=()=>{input.value=select.value;};
 const scan=button('Rescan ports',async()=>{
  scan.disabled=true;
  try{const j=await api('/api/vision/serial-ports');const saved=input.value;
   select.replaceChildren(option('','No serial port'));
   for(const p of j.ports)select.append(option(p.device,p.device+' — '+p.description));
   if(saved&&!j.ports.some(p=>p.device===saved))select.append(option(saved,saved+' — configured / not detected'));
   select.value=saved;status.textContent=j.ports.length+' host ports detected. Detection does not verify access or identify DART hardware. Manual entry is supported.';
  }catch(e){status.textContent=e.message+'; manual entry is available.';}finally{scan.disabled=false;}
 });
 select.append(option(value,value||'No serial port'));box.append(select,input,scan,status);scan.click();return box;
}
async function refreshHistory(){
 const group=multiControl&&!$('#history-all').checked?commandAliases():null;
 const alias=$('#history-all').checked||group?null:activeTarget()?.stb_alias;
 if((group&&!group.length)||(!alias&&!$('#history-all').checked&&!group)){$('#command-history').replaceChildren();$('#history-count').textContent='0 recent commands';return;}
 const query=group?'?'+group.map(a=>'alias='+encodeURIComponent(a)).join('&'):(alias?'?alias='+encodeURIComponent(alias):'');
 const j=await api('/api/vision/commands'+query);
 if(group?JSON.stringify(group)!==JSON.stringify(commandAliases()):alias!==($('#history-all').checked?null:activeTarget()?.stb_alias))return;
 const filtered=group?j.commands.filter(c=>group.includes(c.alias)):j.commands;
 const rows=filtered.map(c=>{const tr=el('tr');tr.dataset.status=c.status;tr.append(td(new Date(c.started_at).toLocaleString()),td(c.alias),td(c.button),td((c.via||c.transport)+' · '+c.duration+' ms'),td(c.status+(c.elapsed_ms===null?'':' · '+c.elapsed_ms+' ms')),td(c.message));return tr;});
 $('#command-history').replaceChildren(...rows);$('#history-count').textContent=filtered.length+' recent commands';
}
$('#history-all').onchange=run(refreshHistory);$('#refresh-history').onclick=run(refreshHistory);
setInterval(()=>run(refreshHistory)(),3000);
function field(key,label,value='',options=null,wide=false){const l=el('label',label,wide?'wide':'');let n;if(options){n=el('select');for(const [val,text] of options)n.append(option(val,text));}else n=el('input');n.name=key;n.value=value;l.append(n);return l;}
function editor(title,fields,save){$('#editor-title').textContent=title;$('#editor-fields').replaceChildren(...fields);$('#editor-error').textContent='';editorSave=save;$('#editor').showModal();}
$('#close-editor').onclick=()=>$('#editor').close();$('#editor-form').onsubmit=async e=>{e.preventDefault();const b=e.submitter;b.disabled=true;try{await editorSave(Object.fromEntries(new FormData(e.target)));$('#editor').close();await refresh();notice('Saved');}catch(err){$('#editor-error').textContent=err.message;}finally{b.disabled=false;}};
function editStb(alias){const s=state.stbs[alias]||{},f=[field('alias','Settop alias',alias||''),field('stb','Receiver ID',s.stb),field('ip','IP address',s.ip),field('mac','Verified MAC (optional)',s.mac),field('protocol','Transport',s.protocol||'SGS',[['SGS','SGS'],['RF','RF / DART']]),field('remote','DART channel (1–16)',s.remote||'1'),portField(s.com_port),field('model','Model',s.model),field('role','Role',s.role||'hopper',[['hopper','Hopper / standalone'],['joey','Joey / client']]),field('host','Host Hopper',s.host||'',[['','None'],...Object.keys(state.stbs).filter(a=>a!==alias).map(a=>[a,a])])];if(alias)f[0].querySelector('input').readOnly=true;editor(alias?'Edit settop':'Add settop',f,data=>api('/api/vision/settops','POST',data));}
function editRemote(r={}){
 const alias=r.stb_alias||Object.keys(state.stbs)[0]||'',stb=state.stbs[alias]||{};
 const target=field('stb_alias','Settop',alias,Object.keys(state.stbs).map(a=>[a,a]));
 let port=portField(stb.com_port);const channel=field('remote','DART channel (1–16)',stb.remote||'1');
 const note=el('p','Port and channel are shared by all remote profiles for this settop.','muted wide');
 target.querySelector('select').onchange=e=>{const s=state.stbs[e.target.value]||{};const next=portField(s.com_port);port.replaceWith(next);port=next;channel.querySelector('input').value=s.remote||'1';};
 editor(r.id?'Edit remote':'Add remote',[field('name','Remote name',r.name),target,port,channel,note,field('stream_id','Linked stream (optional)',r.stream_id||'',[['','No stream — remote control only'],...state.streams.map(s=>[s.id,s.name])],true)],data=>api('/api/vision/remotes'+(r.id?'/'+r.id:''),r.id?'PUT':'POST',data));
}
function editStream(s={}){editor(s.id?'Edit stream':'Add stream',[field('name','Stream name',s.name,undefined,true),field('url','Stream source URL',s.url,undefined,true),field('mode','Playback mode',s.mode||'transcode',[['transcode','Transcode to H.264 (compatible, more CPU)'],['copy','Copy video (H.264 sources, less CPU)']],true)],data=>api('/api/vision/streams'+(s.id?'/'+s.id:''),s.id?'PUT':'POST',data));}
$('#new-stb').onclick=()=>editStb();$('#new-remote').onclick=()=>editRemote();$('#new-stream').onclick=()=>editStream();
async function pair(alias){pairAlias=alias;notice('Requesting pairing PIN…');await api('/sgs/pair/start','POST',{alias});$('#pair-message').textContent='Enter the PIN shown on '+alias+'. You can watch its linked stream in the remote console.';$('#pair-pin').value='';$('#pair-dialog').showModal();}
$('#close-pair').onclick=()=>$('#pair-dialog').close();$('#pair-form').onsubmit=run(async e=>{e.preventDefault();await api('/sgs/pair/complete','POST',{alias:pairAlias,pin:$('#pair-pin').value});$('#pair-dialog').close();notice('Pairing completed');});
$('#discovery-form').onsubmit=run(async e=>{e.preventDefault();const d=Object.fromEntries(new FormData(e.target));await api('/api/vision/discovery','POST',d);notice('Discovery started');pollScan();});$('#cancel-scan').onclick=run(()=>api('/api/vision/discovery/cancel','POST'));
function renderScan(j){if(!j)return;$('#scan-status').textContent=j.state==='idle'?'No scan running.':`${j.state} · ${j.completed}/${j.total} checked · ${j.found.length} video streams found${j.last_error?' · Last probe: '+j.last_error:''}`;const box=$('#scan-results');box.replaceChildren();for(const s of j.found){const row=el('div',undefined,'result'),saved=state.streams.some(x=>x.url===s.url);row.append(el('span',s.url),button(saved?'Saved':'Add stream',()=>editStream({url:s.url,name:new URL(s.url).pathname.split('/').filter(Boolean).pop()||'Video'})));if(saved)row.querySelector('button').disabled=true;box.append(row);}$('#cancel-scan').disabled=j.state!=='running';}
async function pollScan(){try{const j=await api('/api/vision/discovery');renderScan(j);if(j.state==='running')setTimeout(pollScan,1500);}catch(e){notice(e.message,true);}}
run(async()=>{await refresh();const cfg=state.discovery_config;for(const [k,v] of Object.entries(cfg))if($('#discovery-form').elements[k])$('#discovery-form').elements[k].value=v;const initial=location.hash.slice(1)|| (location.pathname==='/settops'?'assets':'console');tab(['console','assets','streams'].includes(initial)?initial:'console');if(state.discovery.state==='running')pollScan();})();

// The picker reads a file on the browser's computer, including for LAN clients.
let pendingImport=null, importBusy=false, importGeneration=0;
$('#import-config').onclick=()=>$('#config-file').click();
function importSummary(s){return `${s.settops_added} settops to add, ${s.settops_skipped} existing settops skipped; ${s.remotes_added} remotes to add; ${s.streams_added} streams to add, ${s.streams_skipped} existing streams skipped.${s.credential_fields_omitted?' Credential fields omitted: '+s.credential_fields_omitted+'.':''}`;}
$('#config-file').onchange=async()=>{
 const picker=$('#config-file'),file=picker.files[0];picker.value='';if(!file)return;
 const generation=++importGeneration;pendingImport=null;$('#apply-import').disabled=true;$('#import-error').textContent='';$('#import-summary').textContent='Reading and validating…';$('#import-filename').textContent=file.name;$('#import-dialog').showModal();
 try{
  if(file.size>1024*1024)throw Error('Choose a configuration file smaller than 1 MiB.');
  const text=await file.text();if(generation!==importGeneration)return;
  try{pendingImport=JSON.parse(text.replace(/^\uFEFF/,''));}catch{throw Error('This file is not valid JSON. Select a JAMboreeLite base.txt or a JSON configuration.');}
  const j=await api('/api/vision/import','POST',{config:pendingImport,preview:true});
  if(generation!==importGeneration)return;
  if(!$('#import-dialog').open){pendingImport=null;return;}
  $('#import-summary').textContent=importSummary(j.summary);$('#apply-import').disabled=false;
 }catch(e){if(generation!==importGeneration)return;pendingImport=null;$('#import-summary').textContent='';$('#import-error').textContent=e.message;}
};
$('#cancel-import').onclick=()=>{if(!importBusy){importGeneration++;pendingImport=null;$('#import-dialog').close();}};
$('#import-dialog').addEventListener('cancel',e=>{if(importBusy)e.preventDefault();else {importGeneration++;pendingImport=null;}});
$('#apply-import').onclick=async()=>{
 if(!pendingImport||importBusy)return;importBusy=true;$('#apply-import').disabled=true;$('#cancel-import').disabled=true;
 try{const j=await api('/api/vision/import','POST',{config:pendingImport,preview:false});pendingImport=null;$('#import-dialog').close();await refresh();notice(`Imported ${j.summary.settops_added} settops, ${j.summary.remotes_added} remotes, and ${j.summary.streams_added} streams.`);}
 catch(e){$('#import-error').textContent=e.message;$('#apply-import').disabled=false;}
 finally{importBusy=false;$('#cancel-import').disabled=false;}
};
