import time
from unittest.mock import Mock
import pytest
from vision.media import Media, Discovery, candidates, probe

def test_discovery_bounds_and_deduplication():
    assert len(candidates({'target':'lab','template':'rtmp://{host}/live{n}','start':0,'end':8}))==9
    assert len(candidates({'target':'192.168.1.0/30','template':'http://{host}/live.m3u8'}))==2
    for d in [{'target':'10.0.0.0/16','template':'http://{host}/x'}, {'target':'10.0.0.0/24','template':'rtmp://{host}/live{n}'}, {'target':'lab','template':'file://{host}/x'}, {'target':'lab','template':'http://{unknown}/x'}]:
        with pytest.raises(ValueError):candidates(d)

def test_only_real_video_is_discovered():
    d=Discovery(lambda url:{'ok':url.endswith('1'),'tracks':[{'codec_type':'video'}]})
    d.start({'target':'lab','template':'rtmp://{host}/live{n}','end':2})
    deadline=time.monotonic()+3
    while d.status()['state']=='running' and time.monotonic()<deadline:time.sleep(.01)
    result=d.status()
    assert result['completed']==3 and len(result['found'])==1 and result['failures']==2

def test_shared_playback_and_scoped_cleanup(tmp_path,monkeypatch):
    p=Mock();p.poll.return_value=None
    calls=[]
    monkeypatch.setattr('vision.media.shutil.which',lambda _: '/bin/ffmpeg')
    monkeypatch.setattr('vision.media.subprocess.Popen',lambda cmd,**kw:(calls.append(cmd) or p))
    m=Media(tmp_path)
    s={'id':'a'*32,'url':'rtmp://lab/live','mode':'copy'}
    try:
        one=m.acquire(s,'viewer-111');two=m.acquire(s,'viewer-222')
        assert one['url']==two['url'] and len(calls)==1
        assert '-c:v' in calls[0] and 'copy' in calls[0]
        m.release(s['id'],'viewer-111');p.terminate.assert_not_called()
        m.release(s['id'],'viewer-222');p.terminate.assert_called_once()
        assert m.status()=={}
        with pytest.raises(KeyError):m.file('..','x')
    finally:m.close()

def test_crashed_worker_gets_new_generation(tmp_path,monkeypatch):
    procs=[]
    def spawn(*_,**kw):
        p=Mock();p.poll.return_value=None;procs.append(p);return p
    monkeypatch.setattr('vision.media.shutil.which',lambda _: '/bin/ffmpeg')
    monkeypatch.setattr('vision.media.subprocess.Popen',spawn)
    m=Media(tmp_path)
    try:
        s={'id':'b'*32,'url':'rtsp://lab/a','mode':'transcode'}
        first=m.acquire(s,'viewer-111');procs[0].poll.return_value=1
        second=m.acquire(s,'viewer-111')
        assert first['url']!=second['url']
    finally:m.close()

def test_probe_missing_ffprobe(monkeypatch):
    monkeypatch.setattr('vision.media.shutil.which',lambda _:None)
    assert not probe('rtmp://lab/a')['ok']
