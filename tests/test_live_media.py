"""Real FFmpeg → HTTP HLS → ffprobe → managed FFmpeg smoke test (loopback only)."""
import functools
import http.server
import shutil
import subprocess
import threading
import time
import urllib.request
import pytest
from vision.media import Media, probe

@pytest.mark.skipif(not shutil.which('ffmpeg') or not shutil.which('ffprobe'), reason='FFmpeg required')
def test_real_hls_pipeline(tmp_path):
    source=tmp_path/'source';source.mkdir()
    handler=functools.partial(http.server.SimpleHTTPRequestHandler,directory=str(source))
    server=http.server.ThreadingHTTPServer(('127.0.0.1',0),handler)
    threading.Thread(target=server.serve_forever,daemon=True).start()
    producer=subprocess.Popen(['ffmpeg','-nostdin','-v','error','-re','-f','lavfi','-i','testsrc2=size=320x180:rate=15',
       '-c:v','libx264','-preset','ultrafast','-g','15','-f','hls','-hls_time','1','-hls_list_size','6',
       '-hls_flags','delete_segments+temp_file',str(source/'index.m3u8')],stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
    media=Media(tmp_path/'output')
    try:
        deadline=time.monotonic()+15
        while not (source/'index.m3u8').exists() and time.monotonic()<deadline:time.sleep(.1)
        assert (source/'index.m3u8').exists()
        url=f'http://127.0.0.1:{server.server_port}/index.m3u8'
        assert probe(url)['ok']
        stream={'id':'f'*32,'url':url,'mode':'transcode'}
        result=media.acquire(stream,'test-viewer')
        generation=result['url'].split('/')[2]
        manifest=media.file(generation,'index.m3u8')
        deadline=time.monotonic()+20
        while not manifest.exists() and time.monotonic()<deadline:time.sleep(.2)
        assert manifest.exists()
        text=manifest.read_text()
        assert '#EXTM3U' in text and '.ts' in text
        segment=next(l for l in text.splitlines() if l.endswith('.ts'))
        assert media.file(generation,segment).stat().st_size>0
        assert media.status()[stream['id']]['state']=='playing'
    finally:
        media.close();producer.terminate();producer.wait(timeout=5);server.shutdown();server.server_close()
