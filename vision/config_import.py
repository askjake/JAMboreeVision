"""Validate and preview browser-selected JSON configurations before additive import."""
import uuid
from .assets import name, stream_url

SECRET_FIELDS = {'lname', 'passwd', 'username', 'password', 'token', 'secret'}


def plan_import(document, store, assets):
    if not isinstance(document, dict):
        raise ValueError('Configuration must be a JSON object')
    if 'stbs' in document or 'streams' in document:
        stbs = document.get('stbs', {})
        streams = document.get('streams', [])
    else:
        stbs, streams = document, []
    if not isinstance(stbs, dict) or not isinstance(streams, list):
        raise ValueError('Expected a stbs object and/or a streams array')
    if not stbs and not streams:
        raise ValueError('The file contains no settops or streams to import')
    if len(stbs) + len(streams) > 2000:
        raise ValueError('Import at most 2,000 assets at a time')
    existing = store.all()
    by_alias = {a.casefold(): a for a in existing}
    additions, aliases, seen = {}, [], set()
    omitted = 0
    for raw_alias, raw in stbs.items():
        alias = name(raw_alias)
        if alias != raw_alias or '/' in alias or '\\' in alias:
            raise ValueError('Settop aliases cannot have outer whitespace or slashes')
        if alias.casefold() in seen:
            raise ValueError('Duplicate settop aliases differ only by case')
        seen.add(alias.casefold())
        if not isinstance(raw, dict):
            raise ValueError('Each settop must be a JSON object')
        if alias.casefold() in by_alias:
            aliases.append(by_alias[alias.casefold()])
            continue
        fields = {k: v for k, v in raw.items() if k.lower() not in SECRET_FIELDS}
        omitted += len(raw) - len(fields)
        for key in ('stb','ip','remote','model','com_port','role','host','mac','protocol'):
            if key in fields and not isinstance(fields[key], (str, int)):
                raise ValueError(f'Settop {alias}: {key} must be text or a number')
            if key in fields:
                fields[key] = str(fields[key])
        fields['protocol'] = fields.get('protocol', 'SGS').upper()
        if fields['protocol'] not in {'SGS','RF'}:
            raise ValueError(f'Settop {alias}: protocol must be SGS or RF')
        additions[alias] = fields
        aliases.append(alias)
    saved_streams = assets.list('streams')
    urls = {s['url'] for s in saved_streams}
    names = {s['name'].casefold(): s['url'] for s in saved_streams}
    new_streams, skipped_streams = [], 0
    for raw in streams:
        if not isinstance(raw, dict):
            raise ValueError('Each stream must be a JSON object')
        label, url = name(raw.get('name')), stream_url(raw.get('url'))
        mode = raw.get('mode', 'transcode')
        if mode not in {'copy','transcode'}:
            raise ValueError('Stream mode must be copy or transcode')
        if url in urls:
            skipped_streams += 1
            continue
        if label.casefold() in names:
            raise ValueError(f'Stream name {label} is already used for another URL')
        new_streams.append({'name':label, 'url':url, 'mode':mode})
        names[label.casefold()] = url
        urls.add(url)
    remotes = assets.list('remotes')
    referenced = {r['stb_alias'].casefold() for r in remotes}
    remote_names = {r['name'].casefold() for r in remotes}
    new_remotes = []
    for alias in aliases:
        if alias.casefold() in referenced:
            continue
        label, suffix = alias, 2
        while label.casefold() in remote_names:
            tail = f' ({suffix})'
            label = alias[:100-len(tail)] + tail
            suffix += 1
        new_remotes.append({'name':label,'stb_alias':alias})
        remote_names.add(label.casefold())
        referenced.add(alias.casefold())
    return {'stbs':additions,'streams':new_streams,'remotes':new_remotes,
            'summary':{'settops_added':len(additions),'settops_skipped':len(stbs)-len(additions),
                       'streams_added':len(new_streams),'streams_skipped':skipped_streams,
                       'remotes_added':len(new_remotes),'credential_fields_omitted':omitted}}


def apply_import(plan, store, assets):
    # Validation is complete before the first write. Reapplying a file is safe.
    # The base file and SQLite are separate stores; an I/O failure can be retried.
    if plan['stbs']:
        store.save({'stbs':plan['stbs']})
    with assets.db() as c:
        for s in plan['streams']:
            c.execute('INSERT INTO streams(id,name,url,mode) VALUES(?,?,?,?)',
                      (uuid.uuid4().hex,s['name'],s['url'],s['mode']))
        for r in plan['remotes']:
            c.execute('INSERT INTO remotes(id,name,stb_alias) VALUES(?,?,?)',
                      (uuid.uuid4().hex,r['name'],r['stb_alias']))
    return plan['summary']
