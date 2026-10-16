"""Loopback-only fixture. Does not import user configuration or command hardware."""
import functools
import http.server
import json
import os
from pathlib import Path
import signal
import subprocess
import tempfile
import threading
import time
from werkzeug.serving import make_server

root=Path(tempfile.mkdtemp(prefix='jv-browser-'))
os.environ['JAMBOREE_BASE']=str(root/'base.txt')
os.environ['JAMBOREE_AUTOPAIR']='0'
from vision.app import create_app
app=create_app(root)
import jamboree.app as legacy
commands=[]
def fake_command(*args,**kwargs):
    commands.append({'args':args,'kwargs':kwargs})
    return {'ok':True,'via':'sgs_fixture'}
legacy.ctl.handle_auto_remote=fake_command
@app.post('/_test/shutdown')
def shutdown_fixture():
    threading.Thread(target=web.shutdown,daemon=True).start()
    return {'ok':True}
@app.get('/_test/commands')
def captured():return {'commands':commands}
source=root/'source';source.mkdir()
class QuietHandler(http.server.SimpleHTTPRequestHandler):
    def log_message(self,*args):pass
server=http.server.ThreadingHTTPServer(('127.0.0.1',0),functools.partial(QuietHandler,directory=str(source)))
threading.Thread(target=server.serve_forever,daemon=True).start()
producer=subprocess.Popen(['ffmpeg','-nostdin','-v','error','-re','-f','lavfi','-i','testsrc2=size=640x360:rate=15',
    '-c:v','libx264','-preset','ultrafast','-g','15','-f','hls','-hls_time','1','-hls_list_size','6',
    '-hls_flags','delete_segments+temp_file',str(source/'index.m3u8')],stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
web=make_server('127.0.0.1',0,app,threaded=True)
def stop(*_):raise SystemExit(0)
signal.signal(signal.SIGTERM,stop)
print(json.dumps({'url':f'http://127.0.0.1:{web.server_port}','stream':f'http://127.0.0.1:{server.server_port}/index.m3u8'}),flush=True)
try:web.serve_forever()
finally:
    app.extensions['vision']['media'].close()
    producer.terminate();producer.wait(timeout=5);server.shutdown();server.server_close()
