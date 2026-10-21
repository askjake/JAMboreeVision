/* Per-window live-edge tracking. Edge lag is a player estimate, not camera latency. */
(function(root,factory){
  if(typeof module==='object'&&module.exports)module.exports=factory;
  else root.createLivePlayback=factory;
})(globalThis,function createLivePlayback(video,{getHls=()=>null,onStatus=()=>{},saved=null,interval=1000}={}){
  let follow=saved?.follow??true,userPaused=saved?.paused??false;
  let active=false,initial=true,hasPlayed=false,waiting=false,dead=false,ownSeek=null,restoreLag=saved?.lag??null;
  const listeners=[];
  function bounds(){
    const r=video.seekable;
    if(!r?.length)return null;
    const start=r.start(r.length-1),end=r.end(r.length-1);
    if(!Number.isFinite(start)||!Number.isFinite(end)||end<=start)return null;
    const sync=getHls()?.liveSyncPosition;
    const target=Number.isFinite(sync)?Math.max(start,Math.min(end-.1,sync)):Math.max(start,end-2);
    return {start,end,target};
  }
  function lag(){const b=bounds();return b?Math.max(0,b.end-video.currentTime):null;}
  function seek(target){
    if(Math.abs(video.currentTime-target)<.4)return;
    ownSeek=target;
    try{video.currentTime=target;}catch{ownSeek=null;}
  }
  function status(){
    if(dead)return;
    const b=bounds(),behind=lag(),suffix=behind===null?'':` · ${Math.round(behind)}s behind stream edge`;
    const near=b&&video.currentTime>=b.target-3;
    let kind,text;
    if(userPaused){kind='paused';text='Paused'+suffix;}
    else if(waiting||video.readyState<2){kind='buffering';text='Buffering'+(follow?' · following live':suffix);}
    else if(video.paused){kind='paused';text='Press play to start';}
    else if(near){kind='live';text='Live'+suffix;}
    else {kind='delayed';text='Delayed'+suffix;}
    onStatus({kind,text,follow,lag:behind});
  }
  function update(){
    if(dead||!active)return;
    const b=bounds();
    if(b&&!video.seeking){
      if(initial){
        if(follow)seek(b.target);
        else if(restoreLag!==null)seek(Math.max(b.start,Math.min(b.end-.1,b.end-restoreLag)));
        initial=false;restoreLag=null;
      }else if(follow&&!userPaused&&hasPlayed&&video.currentTime<b.target-4){
        // Recover lag after a stall or a background tab without forcing delayed viewers.
        seek(b.target);
      }
    }
    status();
  }
  function listen(type,fn){video.addEventListener(type,fn);listeners.push([type,fn]);}
  listen('seeking',()=>{
    // HLS itself can choose a start position before its first playing event.
    if(!hasPlayed&&initial)return;
    if(ownSeek!==null&&Math.abs(video.currentTime-ownSeek)<1)return;
    ownSeek=null;initial=false;follow=false;status();
  });
  listen('seeked',()=>{
    if(!hasPlayed&&initial&&ownSeek===null)return;
    if(ownSeek!==null){ownSeek=null;status();return;}
    const b=bounds();
    follow=!!b&&video.currentTime>=b.target-1;
    status();
  });
  listen('pause',()=>{if(hasPlayed){userPaused=true;follow=false;}status();});
  listen('playing',()=>{hasPlayed=true;userPaused=false;waiting=false;update();});
  listen('waiting',()=>{waiting=true;status();});
  listen('stalled',()=>{waiting=true;status();});
  for(const event of ['loadedmetadata','durationchange','progress','canplay','timeupdate'])listen(event,update);
  const timer=interval?setInterval(update,interval):null;
  function start(){
    active=true;update();
    if(userPaused)return;
    try{const promise=video.play();promise?.catch(()=>{if(!dead)onStatus({kind:'paused',text:'Press play to start',follow,lag:lag()});});}
    catch{onStatus({kind:'paused',text:'Press play to start',follow,lag:lag()});}
  }
  return {
    start,update,
    goLive(){if(dead)return;follow=true;userPaused=false;initial=true;restoreLag=null;update();start();},
    snapshot(){return {follow,paused:userPaused,lag:lag()};},
    destroy(){dead=true;if(timer!==null)clearInterval(timer);for(const [type,fn]of listeners)video.removeEventListener(type,fn);},
  };
});
