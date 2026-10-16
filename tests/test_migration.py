import json
from vision.migrate import migrate
from vision.assets import Assets

def test_migration_is_additive_and_idempotent(tmp_path):
    src=tmp_path/'legacy.txt';root=tmp_path/'data';root.mkdir()
    src.write_text(json.dumps({'stbs':{'H':{'ip':'10.0.0.1','stb':'R1','protocol':'SGS','remote':'1'}}}))
    first=migrate(root,src,'rtmp://lab:1935')
    assert first['settops_added']==1 and first['streams_added']==9
    from jamboree.stb_store import STBStore
    STBStore(root/'base.txt').update_stb('H',{'ip':'10.0.0.2'})
    again=migrate(root,src,'rtmp://lab:1935')
    assert again['settops_added']==0 and again['streams_added']==0 and again['remotes']==1
    assert STBStore(root/'base.txt').get('H')['ip']=='10.0.0.2'
