/* Optional browser acceptance: npm install --no-save playwright; npx playwright install chromium */
const {chromium}=require('playwright');
const {spawn}=require('node:child_process');
const assert=require('node:assert/strict');
const os=require('node:os'),path=require('node:path');
(async()=>{
 const fixture=spawn(process.env.JV_PYTHON||'python',['-m','tests.browser_fixture'],{cwd:process.cwd(),stdio:['ignore','pipe','pipe']});
 let stderr='';fixture.stderr.on('data',d=>stderr+=d);
 let browser;
 try{
 const config=await new Promise((resolve,reject)=>{let text='';const timer=setTimeout(()=>reject(Error('Fixture timeout: '+stderr)),15000);fixture.stdout.on('data',d=>{text+=d;for(const line of text.split('\n'))if(line.startsWith('{')){try{resolve(JSON.parse(line));clearTimeout(timer);}catch{}}});fixture.on('exit',code=>{clearTimeout(timer);reject(Error('Fixture exited '+code+': '+stderr));});});
 browser=await chromium.launch({headless:true,executablePath:process.env.JV_CHROMIUM||undefined,args:['--no-sandbox']});
 const page=await browser.newPage({viewport:{width:1600,height:1100}});const errors=[];page.on('pageerror',e=>errors.push(e.message));
 await page.goto(config.url);await page.waitForFunction(()=>document.querySelector('#system-status').textContent.includes('FFmpeg'));
 await page.locator('[data-tab="assets"]').click();await page.locator('#new-stb').click();
 for(const [k,v] of Object.entries({alias:'Bench Hopper',stb:'R1234567890',ip:'127.0.0.1'}))await page.locator(`#editor-fields [name="${k}"]`).fill(v);
 await page.locator('#editor-form button[type="submit"]').click();await page.locator('#editor').waitFor({state:'hidden'});
 await page.locator('[data-tab="streams"]').click();await page.locator('#new-stream').click();
 await page.locator('#editor-fields [name="name"]').fill('Bench video');await page.locator('#editor-fields [name="url"]').fill(config.stream);
 await page.locator('#editor-form button[type="submit"]').click();await page.locator('#editor').waitFor({state:'hidden'});
 await page.locator('[data-tab="assets"]').click();await page.locator('#new-remote').click();
 await page.locator('#editor-fields [name="name"]').fill('Bench Remote');await page.locator('#editor-fields [name="stb_alias"]').selectOption('Bench Hopper');await page.locator('#editor-fields [name="stream_id"]').selectOption({label:'Bench video'});
 await page.locator('#editor-form button[type="submit"]').click();await page.locator('#editor').waitFor({state:'hidden'});
 await page.locator('[data-tab="console"]').click();await page.locator('#watch-linked').click();
 await page.waitForFunction(()=>{const v=document.querySelector('video');return v&&v.currentTime>0&&v.videoWidth>0;},{},{timeout:45000});
 await page.locator('[data-key="guide"]').click();await page.waitForFunction(()=>document.querySelector('#command-status').textContent.includes('sgs_fixture'));
 const captured=await (await page.request.get(config.url+'/_test/commands')).json();assert.equal(captured.commands[0].args[1],'Bench Hopper');assert.equal(captured.commands[0].args[2],'guide');
 let state=await (await page.request.get(config.url+'/api/vision/state')).json();assert.equal(state.remotes[0].stream_alias,'Bench Remote-stream');
 await page.screenshot({path:process.env.JV_SCREENSHOT||path.join(os.tmpdir(),'jv-console.png'),fullPage:true});
 // A second browser viewer must not lose its stream when the first closes it.
 const second=await browser.newPage();await second.goto(config.url);await second.locator('#watch-linked').click();
 await second.waitForFunction(()=>document.querySelector('video')?.currentTime>0,{},{timeout:30000});
 await page.locator('.video-head button').last().click();
 await second.waitForFunction(()=>document.querySelector('video')?.currentTime>1,{},{timeout:15000});
 state=await(await page.request.get(config.url+'/api/vision/state')).json();assert.equal(Object.values(state.playback)[0].viewers,1);
 await page.locator('[data-tab="assets"]').click();await page.locator('#remote-table button').filter({hasText:'Edit'}).click();await page.locator('#editor-fields [name="name"]').fill('Renamed Remote');await page.locator('#editor-form button[type="submit"]').click();await page.locator('#editor').waitFor({state:'hidden'});
 const resolved=await(await page.request.get(config.url+'/api/vision/resolve/Renamed%20Remote-stream')).json();assert.equal(resolved.stream.name,'Bench video');
 await page.reload();await page.waitForFunction(()=>document.querySelector('#remote-table').textContent.includes('Renamed Remote'));
 await page.setViewportSize({width:390,height:844});await page.locator('[data-tab="console"]').click();await page.screenshot({path:path.join(os.tmpdir(),'jv-mobile.png'),fullPage:true});
 assert.deepEqual(errors,[]);console.log('PASS: browser CRUD, stream links, actual HLS playback, remote dispatch, shared viewers, rename, reload, mobile layout.');
 }finally{if(browser)await browser.close();fixture.kill('SIGTERM');}
})().catch(e=>{console.error(e);process.exitCode=1;});
