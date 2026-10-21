'use strict';
const {test}=require('node:test');
const assert=require('node:assert/strict');
const create=require('../vision/static/live-player.js');
class Video extends EventTarget{
 constructor(){super();this.ranges=[[0,120]];this._time=0;this.paused=true;this.readyState=4;this.seeking=false;this.seeks=[];}
 get seekable(){const r=this.ranges;return{length:r.length,start:i=>r[i][0],end:i=>r[i][1]};}
 get currentTime(){return this._time;}
 set currentTime(t){this._time=t;this.seeks.push(t);this.emit('seeking');this.emit('seeked');}
 emit(type){this.dispatchEvent(new Event(type));}
 play(){this.paused=false;this.emit('playing');return Promise.resolve();}
 pause(){this.paused=true;this.emit('pause');}
}
function fixture(options={}){const video=new Video(),states=[];const player=create(video,{interval:0,onStatus:s=>states.push(s),...options});return{video,player,states,last:()=>states.at(-1)};}
test('HLS starts at live sync, native HLS uses seekable edge minus safety buffer',()=>{
 const h=fixture({getHls:()=>({liveSyncPosition:114})});h.player.start();assert.equal(h.video.currentTime,114);assert.equal(h.last().kind,'live');
 const n=fixture();n.player.start();assert.equal(n.video.currentTime,118);
});
test('initial seek waits for a usable range rather than finite duration',()=>{
 const f=fixture();f.video.ranges=[];f.player.start();assert.equal(f.video.currentTime,0);
 f.video.ranges=[[10,200]];f.video.emit('progress');assert.equal(f.video.currentTime,198);
});
test('deliberate seek and pause remain delayed as the live window advances',()=>{
 const f=fixture();f.player.start();f.video.currentTime=50;assert.equal(f.last().kind,'delayed');
 f.video.ranges=[[10,140]];f.player.update();assert.equal(f.video.currentTime,50);
 f.video.pause();f.video.ranges=[[20,160]];f.video.emit('progress');assert.equal(f.video.currentTime,50);assert.equal(f.last().kind,'paused');
 f.video.play();assert.equal(f.video.currentTime,50);assert.equal(f.last().kind,'delayed');
});
test('Go live resumes a paused viewer and re-enables live following',()=>{
 const f=fixture();f.player.start();f.video.pause();f.video.currentTime=50;
 f.player.goLive();assert.equal(f.video.currentTime,118);assert.equal(f.video.paused,false);assert.equal(f.player.snapshot().follow,true);
 f.video.ranges=[[20,150]];f.player.update();assert.equal(f.video.currentTime,148);
});
test('buffering is not labelled live; recovery follows only the live viewer',()=>{
 const f=fixture();f.player.start();f.video.emit('waiting');assert.equal(f.last().kind,'buffering');
 f.video.ranges=[[50,200]];f.video.emit('playing');assert.equal(f.video.currentTime,198);assert.equal(f.last().kind,'live');
 f.video.currentTime=100;f.video.emit('waiting');f.video.ranges=[[60,220]];f.video.emit('playing');assert.equal(f.video.currentTime,100);assert.equal(f.last().kind,'delayed');
});
test('manual scrub to live resumes following; player windows are independent',()=>{
 const a=fixture(),b=fixture();a.player.start();b.player.start();a.video.currentTime=20;
 a.video.ranges=[[0,140]];b.video.ranges=[[0,140]];a.player.update();b.player.update();
 assert.equal(a.video.currentTime,20);assert.equal(b.video.currentTime,138);
 a.video.currentTime=138;assert.equal(a.player.snapshot().follow,true);
});
test('automatic reload preserves delayed/paused intent, with range clamping',()=>{
 const f=fixture();f.player.start();f.video.currentTime=60;f.video.pause();const saved=f.player.snapshot();f.player.destroy();
 const g=fixture({saved});g.video.ranges=[[100,150]];g.player.start();assert.equal(g.video.currentTime,100);assert.equal(g.video.paused,true);assert.equal(g.player.snapshot().follow,false);
});
test('destroy removes listeners and updates, autoplay denial does not claim live',async()=>{
 const f=fixture();f.video.play=()=>Promise.reject(Error('blocked'));f.player.start();await Promise.resolve();assert.equal(f.last().text,'Press play to start');
 const n=f.states.length;f.player.destroy();f.video.emit('playing');f.player.update();assert.equal(f.states.length,n);
});

test('HLS automatic initial positioning does not become a user-delayed choice',()=>{
 const f=fixture();f.video.ranges=[];f.video.play=()=>Promise.resolve();f.player.start();
 f.video.ranges=[[0,120]];f.video.currentTime=80;f.video.paused=false;f.video.emit('playing');
 assert.equal(f.player.snapshot().follow,true);assert.equal(f.video.currentTime,118);
});
