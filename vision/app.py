"""One same-origin app; the existing SGS/DART controller remains authoritative."""
import atexit
import os
import shutil
import sqlite3
import threading
import time
from pathlib import Path
from urllib.parse import urlsplit
from flask import Flask, jsonify, request, send_from_directory, send_file
from werkzeug.exceptions import HTTPException
from .assets import Assets, name
from .media import Media, Discovery, probe

STATIC = Path(__file__).parent / 'static'
SECRET_FIELDS = {'lname', 'passwd', 'username', 'password'}


def configure(data_dir=None):
    root = Path(data_dir or os.getenv('JAMBOREE_VISION_DATA', Path.home() / '.jamboreevision')).resolve()
    root.mkdir(parents=True, exist_ok=True)
    os.environ.setdefault('JAMBOREE_BASE', str(root / 'base.txt'))
    os.environ.setdefault('JAMBOREE_AUTOPAIR', '0')
    for env, filename in [('JAMBOREE_SGS_CERT', 'cert.pem'), ('JAMBOREE_SGS_KEY', 'key.pem')]:
        if (root / filename).is_file():
            os.environ.setdefault(env, str(root / filename))
    return root


def create_app(data_dir=None):
    root = configure(data_dir)
    from jamboree.app import app as legacy_app, ctl, store, init_serial_from_base
    from jamboree.commands import button_id_to_number
    # Reuse transport endpoints without mutating the upstream application's UI.
    import copy
    app = Flask(__name__, static_folder=None)
    for rule in legacy_app.url_map.iter_rules():
        if rule.endpoint != 'static':
            app.add_url_rule(rule.rule, rule.endpoint, legacy_app.view_functions[rule.endpoint], methods=rule.methods)
    app.error_handler_spec = copy.deepcopy(legacy_app.error_handler_spec)
    app.after_request_funcs = copy.deepcopy(legacy_app.after_request_funcs)
    assets = Assets(root / 'assets.sqlite3')
    assets.recover_history()
    media = Media(root / 'media')
    discovery = Discovery()
    asset_lock = threading.RLock()
    command_lock = threading.Lock()
    app.extensions['vision'] = {'assets': assets, 'media': media, 'discovery': discovery, 'root': root}
    app.config['MAX_CONTENT_LENGTH'] = 2 * 1024 * 1024
    atexit.register(media.close)
    app.view_functions['remote_page'] = lambda: send_from_directory(STATIC, 'index.html')
    app.view_functions['settops_page'] = lambda: send_from_directory(STATIC, 'index.html')

    @app.after_request
    def fresh_ui(response):
        if request.path in {'/', '/settops'} or request.path.startswith('/vision-static/'):
            response.headers['Cache-Control'] = 'no-store, max-age=0'
        return response

    def body():
        data = request.get_json()
        if not isinstance(data, dict):
            raise ValueError('Expected a JSON object')
        return data

    def public_stbs():
        return {a: {k: v for k, v in e.items() if k.lower() not in SECRET_FIELDS}
                for a, e in store.all().items()}

    def require(table, ident):
        obj = assets.get(table, ident)
        if not obj:
            raise KeyError('Asset not found')
        return obj

    @app.before_request
    def same_origin_writes():
        # Legacy GET control endpoints are disabled; POST remains available.
        if request.method == 'GET' and request.path.startswith(('/auto/', '/dart/', '/unpair/', '/api/sgs/pair/verify')):
            return jsonify(ok=False, error='Use POST for device commands'), 405
        if request.method not in {'GET', 'HEAD', 'OPTIONS'}:
            origin = request.headers.get('Origin')
            if origin and origin != request.host_url.rstrip('/'):
                return jsonify(ok=False, error='Cross-origin writes are disabled'), 403
            if request.headers.get('Sec-Fetch-Site') == 'cross-site':
                return jsonify(ok=False, error='Cross-site writes are disabled'), 403
            if not request.is_json:
                return jsonify(ok=False, error='Send application/json'), 415

    @app.errorhandler(ValueError)
    def invalid(exc):
        return jsonify(ok=False, error=str(exc)), 400

    @app.errorhandler(KeyError)
    def missing(exc):
        return jsonify(ok=False, error=str(exc).strip("'")), 404

    @app.errorhandler(sqlite3.IntegrityError)
    def conflict(exc):
        return jsonify(ok=False, error='That name or stream URL already exists; edit the existing asset'), 409

    @app.get('/vision-static/<path:filename>')
    def vision_static(filename):
        return send_from_directory(STATIC, filename)

    def legacy_save_guard():
        data = body()
        stbs = data.get('stbs', data)
        if not isinstance(stbs, dict):
            raise ValueError('stbs must be an object')
        with asset_lock:
            if any(r['stb_alias'] not in stbs for r in assets.list('remotes')):
                return jsonify(ok=False, error='Bulk save would remove a linked settop'), 409
            store.replace_stbs(stbs, allow_delete=True)
            init_serial_from_base(store.document())
        return jsonify(ok=True, success=True, stbs=public_stbs())

    app.view_functions['save_stb_list'] = legacy_save_guard

    @app.post('/api/vision/import')
    def import_config():
        from .config_import import plan_import, apply_import
        data = body()
        if not isinstance(data.get('preview', True), bool):
            raise ValueError('preview must be a boolean')
        with asset_lock:
            plan = plan_import(data.get('config'), store, assets)
            if data.get('preview', True):
                return jsonify(ok=True, preview=True, summary=plan['summary'])
            summary = apply_import(plan, store, assets)
            init_serial_from_base(store.document())
            return jsonify(ok=True, preview=False, summary=summary)

    @app.get('/api/vision/serial-ports')
    def serial_ports():
        from serial.tools import list_ports
        try:
            ports = sorted(list_ports.comports(), key=lambda p: p.device)
            return jsonify(ok=True, ports=[{'device': p.device, 'description': p.description or ''}
                                           for p in ports])
        except OSError:
            return jsonify(ok=False, error='Unable to enumerate host serial ports'), 503

    @app.get('/api/vision/commands')
    def command_history():
        return jsonify(ok=True, commands=assets.history(request.args.get('alias')))

    @app.get('/api/vision/state')
    def state():
        remotes = assets.list('remotes')
        for r in remotes:
            r['stream_alias'] = r['name'] + '-stream' if r['stream_id'] else None
            r['missing_settop'] = r['stb_alias'] not in store.all()
        return jsonify(stbs=public_stbs(), remotes=remotes, streams=assets.list('streams'),
                       playback=media.status(), discovery=discovery.status(),
                       discovery_config=assets.setting('discovery', {}),
                       ffmpeg=bool(shutil.which('ffmpeg')), ffprobe=bool(shutil.which('ffprobe')))

    @app.route('/api/vision/streams', methods=['POST'])
    @app.route('/api/vision/streams/<ident>', methods=['PUT', 'DELETE'])
    def streams(ident=None):
        with asset_lock:
            if request.method == 'DELETE':
                assets.delete('streams', ident)
                media.stop(ident)
                return jsonify(ok=True)
            result = assets.save_stream(body(), ident)
            if ident:
                media.stop(ident)  # changed URL/codec must not keep the previous source alive
            return jsonify(ok=True, stream=result)

    @app.route('/api/vision/remotes', methods=['POST'])
    @app.route('/api/vision/remotes/<ident>', methods=['PUT', 'DELETE'])
    def remotes(ident=None):
        with asset_lock:
            if request.method == 'DELETE':
                assets.delete('remotes', ident)
                return jsonify(ok=True)
            data = body()
            if 'com_port' in data and (not isinstance(data['com_port'], str) or len(data['com_port']) > 256 or any(ord(c) < 32 for c in data['com_port'])):
                raise ValueError('Invalid serial port')
            if 'remote' in data and (not isinstance(data['remote'], str) or not data['remote'].isdigit() or not 1 <= int(data['remote']) <= 16):
                raise ValueError('DART remote channel must be 1–16')
            remote = assets.save_remote(data, store.all(), ident)
            fields = {k: data[k] for k in ('com_port', 'remote') if k in data}
            if fields:
                store.update_stb(remote['stb_alias'], fields)
                init_serial_from_base(store.document())
            return jsonify(ok=True, remote=remote)

    @app.get('/api/vision/resolve/<path:alias>')
    def resolve(alias):
        return jsonify(ok=True, stream=assets.resolve(alias))

    @app.post('/api/vision/settops')
    def settops():
        data = body()
        alias = name(data.get('alias'))
        if '/' in alias or '\\' in alias:
            raise ValueError('Settop alias cannot contain slashes')
        fields = {k: data.get(k, '') for k in ('stb','ip','protocol','remote','model','com_port','role','host','mac')}
        if any(not isinstance(v, str) for v in fields.values()):
            raise ValueError('Settop fields must be text')
        if fields['protocol'] not in {'SGS', 'RF'}:
            raise ValueError('Choose SGS or RF')
        if fields['role'] not in {'hopper', 'joey'}:
            raise ValueError('Choose Hopper or Joey')
        if fields['role'] == 'joey' and (fields['host'] not in store.all() or fields['host'] == alias):
            raise ValueError('Select the host Hopper for this Joey')
        if fields['remote'] and (not fields['remote'].isdigit() or not 1 <= int(fields['remote']) <= 16):
            raise ValueError('DART remote channel must be 1–16')
        with asset_lock:
            store.update_stb(alias, fields)
            init_serial_from_base(store.document())
        return jsonify(ok=True)

    @app.delete('/api/vision/settops/<path:alias>')
    def delete_settop(alias):
        with asset_lock:
            if any(r['stb_alias'] == alias for r in assets.list('remotes')):
                return jsonify(ok=False, error='Reassign or delete linked remotes first'), 409
            if any(a != alias and e.get('host') == alias and e.get('role') == 'joey' for a, e in store.all().items()):
                return jsonify(ok=False, error='Reassign child Joeys first'), 409
            if alias not in store.all():
                raise KeyError('Settop not found')
            store.remove([alias])
            init_serial_from_base(store.document())
        return jsonify(ok=True)

    @app.post('/api/vision/remotes/<ident>/command')
    def command(ident):
        return dispatch_command(require('remotes', ident)['stb_alias'])

    @app.post('/api/vision/settops/<path:alias>/command')
    def settop_command(alias):
        canonical = store.resolve_alias(alias)
        if not canonical:
            raise KeyError('Settop not found')
        return dispatch_command(canonical)

    def dispatch_command(alias):
        # Device control depends only on the configured settop transport.
        # Never probe, acquire, or check video here.
        data = body()
        button = str(data.get('button', '')).lower()
        if button not in button_id_to_number and button != 'unpair':
            raise ValueError('Unknown remote button')
        combo = data.get('combo', 'dvr')
        if button == 'unpair' and combo not in {'dvr', 'home'}:
            raise ValueError('Unpair combination must be dvr or home')
        delay = int(data.get('duration', 120))
        if not 0 <= delay <= 5000:
            raise ValueError('Button duration must be between 0 and 5000 milliseconds')
        force = data.get('transport', 'auto')
        if force not in {'auto', 'sgs', 'rf'}:
            raise ValueError('Unknown transport')
        history_id = assets.start_command(alias, button + (' + ' + combo + '+guide' if button == 'unpair' else ''),
                                          'rf' if button in {'reset', 'allup', 'unpair'} else force,
                                          3000 if button == 'unpair' else 0 if button in {'reset', 'allup'} else delay)
        started = time.monotonic()
        def finish(status, via, message):
            assets.finish_command(history_id, status, via, round((time.monotonic()-started)*1000), message)
        if not command_lock.acquire(blocking=False):
            finish('rejected', None, 'Another remote command is still running; nothing sent')
            return jsonify(ok=False, error='A remote command is still running'), 409
        try:
            if button == 'unpair':
                result = ctl.unpair(alias, combo=combo)
            elif button in {'reset', 'allup'}:
                result = ctl.dart(alias, button, button)
            else:
                result = ctl.handle_auto_remote('1', alias, button, delay,
                    force=None if force == 'auto' else force)
            via = str(result.get('via', 'unknown'))
            ok = result.get('ok', True)
            serial = result.get('delivery') == 'serial_flushed' or via in {'rf', 'dart', 'rf_fallback'}
            message = ('Serial write completed; receiver action not confirmed' if serial else
                       'SGS command completed; verify receiver action') if ok else 'Controller reported command failure'
            finish('sent' if ok else 'failed', via, message)
            return jsonify({**result, 'history_id': history_id, 'feedback': message})
        except Exception as exc:
            finish('failed', None, 'Command failed (' + type(exc).__name__ + '); check transport and server log')
            if isinstance(exc, RuntimeError):
                return jsonify(ok=False, error=str(exc)), 503
            raise
        finally:
            command_lock.release()

    @app.post('/api/vision/streams/<ident>/probe')
    def probe_stream(ident):
        return jsonify(probe(require('streams', ident)['url']))

    @app.post('/api/vision/streams/<ident>/play')
    def play(ident):
        with asset_lock:
            return jsonify(ok=True, **media.acquire(require('streams', ident), body().get('viewer')))

    @app.post('/api/vision/streams/<ident>/release')
    def release(ident):
        media.release(ident, body().get('viewer'))
        return jsonify(ok=True)

    @app.get('/media/<generation>/<filename>')
    def media_file(generation, filename):
        path = media.file(generation, filename)
        if not path.is_file():
            return jsonify(ok=False, error='Waiting for video segments'), 404
        response = send_file(path, mimetype='application/vnd.apple.mpegurl' if filename.endswith('.m3u8') else 'video/mp2t', conditional=True)
        response.headers['Cache-Control'] = 'no-store'
        return response

    @app.post('/api/vision/discovery')
    def discover():
        data = body()
        if not shutil.which('ffprobe'):
            raise ValueError('Install FFmpeg (including ffprobe) before discovery')
        result = discovery.start(data)
        assets.save_setting('discovery', data)
        return jsonify(ok=True, job=result), 202

    @app.get('/api/vision/discovery')
    def discovery_status():
        return jsonify(discovery.status())

    @app.post('/api/vision/discovery/cancel')
    def cancel():
        discovery.cancelled.set()
        return jsonify(ok=True)

    return app
