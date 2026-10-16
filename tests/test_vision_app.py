import os
import tempfile
from pathlib import Path
from unittest.mock import Mock
import pytest
# Separate process invocation from upstream tests; both exercise a module-level legacy app.
os.environ['JAMBOREE_BASE']=str(Path(tempfile.mkdtemp())/'base.txt')
from vision.app import create_app

@pytest.fixture(scope='module')
def app():
    a=create_app(tempfile.mkdtemp())
    yield a
    a.extensions['vision']['media'].close()

@pytest.fixture
def client(app,monkeypatch,tmp_path):
    import jamboree.app as legacy
    from vision.assets import Assets
    from jamboree.stb_store import STBStore
    st=STBStore(tmp_path/'base.txt')
    # Route closures intentionally retain legacy.store; clear it between tests.
    legacy.store.replace_stbs({})
    a=app.extensions['vision']['assets']
    for r in a.list('remotes'):a.delete('remotes',r['id'])
    for s in a.list('streams'):a.delete('streams',s['id'])
    return app.test_client()

def seed(c):
    assert c.post('/api/vision/settops',json={'alias':'H','stb':'R123','ip':'127.0.0.1','protocol':'SGS','remote':'1','role':'hopper'}).status_code==200
    s=c.post('/api/vision/streams',json={'name':'Video','url':'rtmp://lab/live0'}).json['stream']
    r=c.post('/api/vision/remotes',json={'name':'My Remote','stb_alias':'H','stream_id':s['id']}).json['remote']
    return s,r

def test_round_trip_and_controller_dispatch(client,monkeypatch):
    import jamboree.app as legacy
    s,r=seed(client)
    fn=Mock(return_value={'ok':True,'via':'sgs'})
    monkeypatch.setattr(legacy.ctl,'handle_auto_remote',fn)
    res=client.post(f"/api/vision/remotes/{r['id']}/command",json={'button':'guide','duration':120,'transport':'sgs'})
    assert res.status_code==200
    fn.assert_called_once_with('1','H','guide',120,force='sgs')
    assert client.get('/api/vision/resolve/My%20Remote-stream').json['stream']['id']==s['id']
    assert client.delete('/api/vision/settops/H',json={}).status_code==409
    assert client.delete('/api/vision/streams/'+s['id'],json={}).status_code==200
    assert client.get('/api/vision/state').json['remotes'][0]['stream_id'] is None

def test_validation_and_origin(client):
    _,r=seed(client)
    url=f"/api/vision/remotes/{r['id']}/command"
    for data in [{'button':'bogus'},{'button':'guide','duration':9999},{'button':'guide','transport':'bogus'}]:
        assert client.post(url,json=data).status_code==400
    assert client.post(url,json={'button':'guide'},headers={'Origin':'https://evil.invalid'}).status_code==403
    assert client.post(url,data='x').status_code==415
    assert client.get('/auto/1/H/guide/120').status_code==405
    assert client.post('/api/vision/remotes',json=[]).status_code==400

def test_no_secret_fields_in_inventory(client):
    import jamboree.app as legacy
    seed(client)
    legacy.store.update_stb('H',{'passwd':'do-not-display','username':'hidden','custom':'preserve-me'})
    assert 'do-not-display' not in client.get('/api/vision/state').text
    assert 'hidden' not in client.get('/get-stb-list').text
    client.post('/api/vision/settops',json={'alias':'H','protocol':'SGS','role':'hopper'})
    assert legacy.store.get('H')['custom']=='preserve-me'

def test_stream_edit_stops_old_session(client,app,monkeypatch):
    s,_=seed(client)
    stop=Mock();monkeypatch.setattr(app.extensions['vision']['media'],'stop',stop)
    assert client.put('/api/vision/streams/'+s['id'],json={**s,'url':'rtmp://lab/live2'}).status_code==200
    stop.assert_called_once_with(s['id'])

def test_root_assets_and_missing_media(client):
    assert client.get('/').status_code==200
    assert client.get('/vision-static/app.js').status_code==200
    assert client.get('/media/'+'a'*32+'/index.m3u8').status_code==404

def test_legacy_bulk_save_cannot_break_remote_links(client):
    seed(client)
    assert client.post('/save-stb-list',json={'stbs':{}}).status_code==409
    import jamboree.app as legacy
    legacy.store.update_stb('H',{'passwd':'secret-value'})
    saved=client.post('/save-stb-list',json={'stbs':legacy.store.all()})
    assert saved.status_code==200 and 'secret-value' not in saved.text
