"""Explicit, additive import from a local JAMboreeLite base.txt and Sling source."""
import argparse
import json
import shutil
from pathlib import Path
from filelock import FileLock
from .app import configure
from .assets import Assets, stream_url


def migrate(root, legacy_base=None, sling_base=None, first=0, last=8, legacy_certs=None):
    from jamboree.stb_store import STBStore
    root=Path(root)
    assets=Assets(root/'assets.sqlite3')
    store=STBStore(root/'base.txt')
    imported=0
    if legacy_base:
        data=json.loads(Path(legacy_base).read_text(encoding='utf-8-sig'))
        stbs=data.get('stbs',data)
        if not isinstance(stbs,dict) or any(not isinstance(v,dict) for v in stbs.values()):
            raise ValueError('Expected a base.txt object containing a settop table')
        store._validate_alias_table(stbs)
        # Existing destination rows win: imports must not revert recovered IPs.
        existing=store.all()
        additions={k:v for k,v in stbs.items() if not any(k.casefold()==a.casefold() for a in existing)}
        store.save({'stbs':additions})
        imported=len(additions)
    remotes=assets.list('remotes')
    for alias in store.all():
        if not any(r['stb_alias']==alias for r in remotes):
            candidate=alias
            suffix=2
            while any(r['name'].casefold()==candidate.casefold() for r in remotes):
                candidate=f'{alias} ({suffix})';suffix+=1
            remotes.append(assets.save_remote({'name':candidate,'stb_alias':alias},store.all()))
    streams=0
    if sling_base:
        if not 0<=first<=last<=255:
            raise ValueError('Stream range must be between 0 and 255')
        for n in range(first,last+1):
            url=stream_url(sling_base.rstrip('/')+f'/live{n}')
            entries=assets.list('streams')
            if any(s['url']==url for s in entries):continue
            label=f'live{n}'
            if any(s['name'].casefold()==label.casefold() for s in entries):
                raise ValueError(f'Stream name {label} is already used by another source; add it in the UI')
            assets.save_stream({'name':label,'url':url,'mode':'copy'})
            streams+=1
    if legacy_certs:
        for filename in ('cert.pem','key.pem'):
            source=Path(legacy_certs)/filename;target=root/filename
            if source.is_file() and not target.exists():
                shutil.copyfile(source,target);target.chmod(0o600)
    return {'settops_added':imported,'remotes':len(remotes),'streams_added':streams}


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--data-dir');p.add_argument('--legacy-base');p.add_argument('--legacy-certs')
    p.add_argument('--sling-base',help='Example: rtmp://10.79.86.118:1935')
    p.add_argument('--first',type=int,default=0);p.add_argument('--last',type=int,default=8)
    a=p.parse_args();root=configure(a.data_dir)
    with FileLock(str(root/'server.lock'),timeout=0):
        print(json.dumps(migrate(root,a.legacy_base,a.sling_base,a.first,a.last,a.legacy_certs)))

if __name__=='__main__':main()
