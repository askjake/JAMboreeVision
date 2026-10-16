import sqlite3
import pytest
from vision.assets import Assets, stream_url

@pytest.fixture
def assets(tmp_path):
    return Assets(tmp_path/'assets.db')

def test_links_survive_rename_restart_and_source_change(assets):
    s=assets.save_stream({'name':'Video','url':'rtmp://lab:1935/live0'})
    r=assets.save_remote({'name':'Remote A','stb_alias':'Hopper','stream_id':s['id']},['Hopper'])
    assets.save_remote({**r,'name':'Remote B'},['Hopper'],r['id'])
    assets.save_stream({**s,'url':'rtmp://lab:1935/live1'},s['id'])
    reloaded=Assets(assets.path)
    assert reloaded.resolve('remote b-stream')['url'].endswith('live1')
    with pytest.raises(KeyError):reloaded.resolve('Remote A-stream')
    assert reloaded.list('remotes')[0]['id']==r['id']

def test_delete_stream_unlinks_all_remotes(assets):
    s=assets.save_stream({'name':'V','url':'http://lab/live.m3u8'})
    for n in ['A','B']:assets.save_remote({'name':n,'stb_alias':'H','stream_id':s['id']},['H'])
    assets.delete('streams',s['id'])
    assert all(r['stream_id'] is None for r in assets.list('remotes'))

def test_duplicate_names_urls_and_bad_links(assets):
    s=assets.save_stream({'name':'V','url':'http://LAB/live.m3u8'})
    for d in [{'name':'v','url':'http://lab/other.m3u8'},{'name':'Other','url':s['url']}]:
        with pytest.raises(sqlite3.IntegrityError):assets.save_stream(d)
    with pytest.raises(ValueError):assets.save_remote({'name':'R','stb_alias':'missing'},['H'])
    with pytest.raises(ValueError):assets.save_remote({'name':'R','stb_alias':'H','stream_id':'missing'},['H'])

@pytest.mark.parametrize('url',['file:///etc/passwd','http://a:99999/x','http://user:pass@lab/x','rtmp://','http://a/x#f','http://a/\n'])
def test_reject_bad_urls(url):
    with pytest.raises(ValueError):stream_url(url)

def test_unrelated_settings_and_assets_survive(assets):
    assets.save_setting('discovery',{'target':'lab'})
    assets.save_stream({'name':'V','url':'udp://239.1.1.1:1234'})
    assert Assets(assets.path).setting('discovery')['target']=='lab'
