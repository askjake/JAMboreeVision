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

def test_gui_import_preview_apply_and_repeat(client):
    import jamboree.app as legacy
    seed(client)
    config={'stbs':{'H':{'ip':'do-not-overwrite'},'New Hopper':{'ip':'192.0.2.2','protocol':'SGS','passwd':'omit-this'}},
            'streams':[{'name':'Imported video','url':'rtmp://lab/live9','mode':'copy'}]}
    preview=client.post('/api/vision/import',json={'config':config,'preview':True})
    assert preview.status_code==200
    assert preview.json['summary']=={'settops_added':1,'settops_skipped':1,'remotes_added':1,'streams_added':1,'streams_skipped':0,'credential_fields_omitted':1}
    assert 'New Hopper' not in legacy.store.all()
    applied=client.post('/api/vision/import',json={'config':config,'preview':False})
    assert applied.status_code==200
    assert legacy.store.get('H')['ip']=='127.0.0.1'
    assert 'passwd' not in legacy.store.get('New Hopper')
    state=client.get('/api/vision/state').json
    assert len(state['remotes'])==2
    assert next(r for r in state['remotes'] if r['name']=='New Hopper')['stream_id'] is None
    assert next(r for r in state['remotes'] if r['name']=='My Remote')['stream_id'] is not None
    again=client.post('/api/vision/import',json={'config':config,'preview':False})
    assert again.json['summary']['settops_added']==0
    assert again.json['summary']['remotes_added']==0
    assert again.json['summary']['streams_added']==0


def test_gui_import_validates_entire_file_before_writes(client):
    import jamboree.app as legacy
    seed(client)
    bad={'stbs':{'New':{'ip':'192.0.2.2'}},'streams':[{'name':'Video','url':'http://lab/different'}]}
    res=client.post('/api/vision/import',json={'config':bad,'preview':False})
    assert res.status_code==400 and 'New' not in legacy.store.all()
    for config in [[],{}, {'stbs':[]}, {'stbs':{'A':{},'a':{}}}, {'stbs':{'x':None}}, {'streams':[{'name':'x','url':'file:///x'}]}]:
        assert client.post('/api/vision/import',json={'config':config}).status_code==400


def test_gui_import_file_size_and_bare_alias_table(client):
    assert client.post('/api/vision/import',json={'config':{'Bare':{'protocol':'RF','remote':1}},'preview':False}).status_code==200
    oversized='x'*(2*1024*1024+1)
    assert client.post('/api/vision/import',data=oversized,content_type='application/json').status_code==413


def test_updated_ui_is_not_cached(client):
    for path in ['/', '/settops', '/vision-static/app.js', '/vision-static/style.css']:
        response=client.get(path)
        assert response.status_code==200
        assert 'no-store' in response.headers['Cache-Control']
    assert 'id="import-config"' in client.get('/').text


def test_remote_control_without_streams_or_ffmpeg(client,app,monkeypatch):
    import jamboree.app as legacy
    monkeypatch.setattr('vision.app.shutil.which',lambda _:None)
    fn=Mock(return_value={'ok':True,'via':'sgs'})
    monkeypatch.setattr(legacy.ctl,'handle_auto_remote',fn)
    assert client.post('/api/vision/settops',json={'alias':'Control only','protocol':'SGS','role':'hopper','stb':'R123','ip':'192.0.2.1'}).status_code==200
    for label,fields in [('Omitted',{}),('Blank',{'stream_id':''}),('Null',{'stream_id':None})]:
        res=client.post('/api/vision/remotes',json={'name':label,'stb_alias':'Control only',**fields})
        assert res.status_code==200
        r=res.json['remote'];assert r['stream_id'] is None
        assert client.post('/api/vision/remotes/'+r['id']+'/command',json={'button':'guide'}).status_code==200
    inventory=client.get('/api/vision/state').json
    assert inventory['streams']==[] and inventory['ffmpeg'] is False
    assert all(r['stream_alias'] is None for r in inventory['remotes'])
    assert fn.call_count==3
    assert app.extensions['vision']['media'].status()=={}


def test_clearing_stream_preserves_remote_and_control(client,monkeypatch):
    import jamboree.app as legacy
    s,r=seed(client)
    res=client.put('/api/vision/remotes/'+r['id'],json={**r,'stream_id':''})
    assert res.status_code==200 and res.json['remote']['id']==r['id']
    assert res.json['remote']['stream_id'] is None
    fn=Mock(return_value={'ok':True,'via':'sgs'})
    monkeypatch.setattr(legacy.ctl,'handle_auto_remote',fn)
    assert client.post('/api/vision/remotes/'+r['id']+'/command',json={'button':'guide'}).status_code==200
    assert client.get('/api/vision/state').json['streams'][0]['id']==s['id']


def test_settop_control_needs_no_remote_profile_or_video(client,app,monkeypatch):
    import jamboree.app as legacy
    assert client.post('/api/vision/settops',json={'alias':'RF bench','protocol':'RF','role':'hopper','remote':'1'}).status_code==200
    assert client.get('/api/vision/state').json['remotes']==[]
    fn=Mock(return_value={'ok':True,'via':'rf'})
    monkeypatch.setattr(legacy.ctl,'handle_auto_remote',fn)
    acquire=Mock(side_effect=AssertionError('Control must not access video'))
    monkeypatch.setattr(app.extensions['vision']['media'],'acquire',acquire)
    res=client.post('/api/vision/settops/RF%20bench/command',json={'button':'guide','transport':'rf'})
    assert res.status_code==200
    fn.assert_called_once_with('1','RF bench','guide',120,force='rf')
    acquire.assert_not_called()
    assert client.post('/api/vision/settops/missing/command',json={'button':'guide'}).status_code==404


def test_offline_linked_stream_does_not_block_remote(client,app,monkeypatch):
    import jamboree.app as legacy
    s,r=seed(client)
    acquire=Mock(side_effect=ValueError('Stream offline'))
    monkeypatch.setattr(app.extensions['vision']['media'],'acquire',acquire)
    assert client.post('/api/vision/streams/'+s['id']+'/play',json={'viewer':'test-viewer'}).status_code==400
    fn=Mock(return_value={'ok':True,'via':'sgs'})
    monkeypatch.setattr(legacy.ctl,'handle_auto_remote',fn)
    for key in ['guide','up','select']:
        assert client.post('/api/vision/remotes/'+r['id']+'/command',json={'button':key}).status_code==200
    assert fn.call_count==3 and acquire.call_count==1
    assert client.get('/api/vision/state').json['remotes'][0]['stream_id']==s['id']

@pytest.mark.parametrize('key', ['sat', 'tv', 'aux', 'input', 'mic'])
@pytest.mark.parametrize('transport', ['sgs', 'rf'])
def test_added_mode_keys(client, monkeypatch, key, transport):
    import jamboree.app as legacy
    _, r = seed(client)
    fn = Mock(return_value={'ok': True, 'via': transport})
    monkeypatch.setattr(legacy.ctl, 'handle_auto_remote', fn)
    response = client.post(f"/api/vision/remotes/{r['id']}/command",
                           json={'button': key, 'duration': 3000, 'transport': transport})
    assert response.status_code == 200
    fn.assert_called_once_with('1', 'H', key, 3000, force=transport)

@pytest.mark.parametrize('combo', ['dvr', 'home'])
def test_unpair_uses_rf_and_locks_other_commands(client, monkeypatch, combo):
    import jamboree.app as legacy
    seed(client)
    route = '/api/vision/settops/H/command'
    def unpair(alias, **kwargs):
        assert alias == 'H' and kwargs == {'combo': combo}
        assert client.post(route, json={'button': 'guide'}).status_code == 409
        return {'ok': True, 'via': 'dart'}
    monkeypatch.setattr(legacy.ctl, 'unpair', unpair)
    assert client.post(route, json={'button': 'unpair', 'combo': combo,
                                    'transport': 'sgs'}).status_code == 200
    assert client.post(route, json={'button': 'unpair', 'combo': 'bad'}).status_code == 400
    dart = Mock(return_value={'ok': True, 'via': 'dart'})
    monkeypatch.setattr(legacy.ctl, 'dart', dart)
    assert client.post(route, json={'button': 'reset'}).status_code == 200
    dart.assert_called_once_with('H', 'reset', 'reset')

@pytest.mark.parametrize('combo', ['dvr', 'home'])
def test_unpair_timing_and_releases(monkeypatch, combo):
    from jamboree.controller import Controller
    import jamboree.controller as module
    ctl = Controller()
    monkeypatch.setattr(ctl, '_canonical_alias', lambda alias: alias)
    events = []
    monkeypatch.setattr(ctl, 'dart', lambda alias, key, action: events.append((key, action)))
    monkeypatch.setattr(module.time, 'sleep', lambda seconds: events.append(('sleep', seconds)))
    assert ctl.unpair('H', combo)['via'] == 'dart'
    assert events == [('sat', 'down'), ('sleep', 3.0), ('sat', 'up'),
                      ('sleep', .2), (combo, 'down'), ('guide', 'down'),
                      ('sleep', 3.0), ('sat', 'up'), (combo, 'up'),
                      ('guide', 'up'), ('allup', 'allup')]
    events.clear()
    def fail_press(alias, key, action):
        events.append((key, action))
        if key == 'guide' and action == 'down':
            raise RuntimeError('port lost')
    monkeypatch.setattr(ctl, 'dart', fail_press)
    with pytest.raises(RuntimeError, match='port lost'):
        ctl.unpair('H', combo)
    assert events[-4:] == [('sat', 'up'), (combo, 'up'), ('guide', 'up'), ('allup', 'allup')]

def test_host_serial_discovery(client, monkeypatch):
    from serial.tools import list_ports
    from types import SimpleNamespace
    monkeypatch.setattr(list_ports, 'comports', lambda: [SimpleNamespace(device='COM4', description='USB DART'), SimpleNamespace(device='/dev/ttyUSB0', description=None)])
    result = client.get('/api/vision/serial-ports')
    assert result.json['ports'] == [{'device':'/dev/ttyUSB0','description':''},{'device':'COM4','description':'USB DART'}]
    monkeypatch.setattr(list_ports, 'comports', lambda: [])
    assert client.get('/api/vision/serial-ports').json['ports'] == []


def test_remote_port_selection_preserves_other_fields(client):
    import jamboree.app as legacy
    _, r = seed(client)
    legacy.store.update_stb('H', {'custom': 'retained'})
    data = {**r, 'com_port': '/dev/disconnected-dart', 'remote': '4'}
    assert client.put('/api/vision/remotes/'+r['id'], json=data).status_code == 200
    assert legacy.store.get('H')['com_port'] == '/dev/disconnected-dart'
    assert legacy.store.get('H')['remote'] == '4'
    assert legacy.store.get('H')['custom'] == 'retained'
    assert client.put('/api/vision/remotes/'+r['id'], json={**r, 'stream_id': None}).status_code == 200
    assert legacy.store.get('H')['com_port'] == '/dev/disconnected-dart'
    assert client.put('/api/vision/remotes/'+r['id'], json={**r, 'remote': '17'}).status_code == 400


def test_command_history_results(client, monkeypatch):
    import jamboree.app as legacy
    seed(client)
    fn = Mock(return_value={'ok':True,'via':'dart','delivery':'serial_flushed','stdout':'secret'})
    monkeypatch.setattr(legacy.ctl, 'handle_auto_remote', fn)
    route = '/api/vision/settops/H/command'
    result = client.post(route, json={'button':'guide','transport':'rf'})
    assert result.status_code == 200 and 'not confirmed' in result.json['feedback']
    rows = client.get('/api/vision/commands?alias=H').json['commands']
    row = rows[0]
    assert row['status'] == 'sent' and row['via'] == 'dart' and row['elapsed_ms'] >= 0
    assert 'secret' not in str(rows)
    fn.side_effect = RuntimeError('private transport detail')
    assert client.post(route, json={'button':'guide'}).status_code == 503
    rows = client.get('/api/vision/commands?alias=H').json['commands']
    assert rows[0]['status'] == 'failed' and 'private' not in str(rows)
    fn.side_effect = None
    assert client.post(route, json={'button':'guide'}).status_code == 200
    assert client.get('/api/vision/commands?alias=NoSuchSettop').json['commands'] == []


def test_command_history_retention_and_restart(tmp_path):
    from vision.assets import Assets
    path = tmp_path/'assets.sqlite3'
    assets = Assets(path)
    with assets.db() as c:
        c.executemany("INSERT INTO command_history(alias,button,transport,duration,status,message) VALUES('H','guide','rf',120,'sent','done')", [()]*1000)
    ident = assets.start_command('H', 'sat', 'rf', 3000)
    with assets.db() as c:
        assert c.execute('SELECT count(*) FROM command_history').fetchone()[0] == 1000
    reopened = Assets(path)
    assert reopened.history()[0]['id'] == ident
    reopened.recover_history()
    assert reopened.history()[0]['status'] == 'interrupted'
    assert len(reopened.history()) == 100


def test_mic_backend_rf_wire_mapping(client, monkeypatch):
    import jamboree.serial_bridge as bridge
    from jamboree.commands import get_sgs_codes
    seed(client)
    lines = []
    monkeypatch.setattr(bridge, '_write', lambda alias, line: lines.append((alias, line)) or line.rstrip())
    monkeypatch.setattr(bridge.time, 'sleep', lambda seconds: None)
    response = client.post('/api/vision/settops/H/command',
                           json={'button': 'mic', 'transport': 'rf', 'duration': 3000})
    assert response.status_code == 200
    assert response.json['via'] == 'rf'
    assert lines == [('H', '1 97 17 3000\n')]
    assert get_sgs_codes('mic', 120) == 'Mic'
    assert get_sgs_codes('mic', 3000) == 'Mic'
