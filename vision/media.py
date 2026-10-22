"""Owned FFmpeg sessions, shared viewer leases, and bounded stream probing."""
import json
import re
import shutil
import subprocess
import threading
import time
import uuid
from concurrent.futures import ThreadPoolExecutor, as_completed
from pathlib import Path
from urllib.parse import urlsplit
from .assets import stream_url


def input_args(url):
    protocol = urlsplit(url).scheme.lower()
    if protocol in {'rtsp', 'rtsps'}:
        return ['-rtsp_transport', 'tcp', '-stimeout', '5000000']
    return ['-rw_timeout', '5000000']


def probe(url, executable='ffprobe'):
    url = stream_url(url)
    if not shutil.which(executable):
        return {'ok': False, 'error': 'ffprobe is missing; install FFmpeg and reopen the terminal'}
    try:
        result = subprocess.run([executable, '-v', 'error', *input_args(url), '-i', url,
            '-show_entries', 'stream=codec_type,codec_name,width,height', '-of', 'json'],
            capture_output=True, timeout=9, check=False)
        data = json.loads(result.stdout or b'{}')
        tracks = data.get('streams', [])
        ok = result.returncode == 0 and any(s.get('codec_type') == 'video' for s in tracks)
        return {'ok': ok, 'tracks': tracks, 'error': None if ok else 'No decodable video found at this URL'}
    except subprocess.TimeoutExpired:
        return {'ok': False, 'error': 'Probe timed out'}
    except (OSError, ValueError):
        return {'ok': False, 'error': 'Stream probe failed'}


class Media:
    def __init__(self, root, executable='ffmpeg', idle_seconds=65, max_sessions=16):
        self.root = Path(root)
        self.root.mkdir(parents=True, exist_ok=True)
        self.executable, self.idle_seconds, self.max_sessions = executable, idle_seconds, max_sessions
        self.sessions = {}
        self.lock = threading.RLock()
        self.closed = threading.Event()
        self.worker = threading.Thread(target=self._reaper, daemon=True)
        self.worker.start()

    def acquire(self, stream, viewer):
        if not re.fullmatch(r'[a-zA-Z0-9-]{8,80}', str(viewer)):
            raise ValueError('Invalid viewer ID')
        with self.lock:
            ident = stream['id']
            session = self.sessions.get(ident)
            if session and session['process'].poll() is not None:
                self._stop(ident)
                session = None
            if not session:
                if len(self.sessions) >= self.max_sessions:
                    raise ValueError('Playback limit reached; close a video window first')
                if not shutil.which(self.executable):
                    raise ValueError('ffmpeg is missing; install FFmpeg and reopen the terminal')
                generation = uuid.uuid4().hex
                folder = self.root / generation
                folder.mkdir()
                codec = ['-c:v', 'copy'] if stream['mode'] == 'copy' else [
                    '-c:v', 'libx264', '-preset', 'veryfast', '-tune', 'zerolatency',
                    '-pix_fmt', 'yuv420p', '-vf', 'scale=trunc(iw/2)*2:trunc(ih/2)*2',
                    '-force_key_frames', 'expr:gte(t,n_forced*2)']
                command = [self.executable, '-nostdin', '-hide_banner', '-loglevel', 'error',
                    *input_args(stream['url']), '-i', stream['url'], '-map', '0:v:0', '-map', '0:a:0?',
                    *codec, '-c:a', 'aac', '-f', 'hls', '-hls_time', '2', '-hls_list_size', '6',
                    '-hls_flags', 'delete_segments+temp_file', '-hls_segment_filename', str(folder / 'seg%06d.ts'),
                    str(folder / 'index.m3u8')]
                # stderr is not retained: URLs may carry upstream query tokens.
                try:
                    process = subprocess.Popen(command, stdin=subprocess.DEVNULL,
                        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
                except OSError:
                    shutil.rmtree(folder, ignore_errors=True)
                    raise ValueError('Could not start FFmpeg')
                session = {'process': process, 'folder': folder, 'viewers': {}, 'started': time.time()}
                self.sessions[ident] = session
            session['viewers'][viewer] = time.monotonic()
            return {'url': f"/media/{session['folder'].name}/index.m3u8", **self._status(session)}

    def _status(self, s):
        manifest = s['folder'] / 'index.m3u8'
        alive = s['process'].poll() is None
        try:
            fresh = time.time() - manifest.stat().st_mtime < 25
        except FileNotFoundError:
            fresh = False
        return {'state': ('playing' if fresh else 'starting') if alive else 'failed',
                'viewers': len(s['viewers']), 'started': s['started']}

    def status(self):
        with self.lock:
            return {k: self._status(v) for k, v in self.sessions.items()}

    def release(self, ident, viewer):
        with self.lock:
            s = self.sessions.get(ident)
            if s:
                s['viewers'].pop(viewer, None)
                if not s['viewers']:
                    self._stop(ident)

    def _stop(self, ident):
        s = self.sessions.pop(ident, None)
        if not s:
            return
        p = s['process']
        if p.poll() is None:
            p.terminate()
            try:
                p.wait(timeout=3)
            except subprocess.TimeoutExpired:
                p.kill()
                p.wait(timeout=3)
        shutil.rmtree(s['folder'], ignore_errors=True)

    def stop(self, ident):
        with self.lock:
            self._stop(ident)

    def file(self, generation, filename):
        if not re.fullmatch(r'[0-9a-f]{32}', generation) or not re.fullmatch(r'(index\.m3u8|seg[0-9]+\.ts)', filename):
            raise KeyError('Unknown media file')
        with self.lock:
            if not any(s['folder'].name == generation for s in self.sessions.values()):
                raise KeyError('Expired playback session')
        return self.root / generation / filename

    def _reaper(self):
        while not self.closed.wait(5):
            with self.lock:
                for ident, s in list(self.sessions.items()):
                    now = time.monotonic()
                    s['viewers'] = {v: t for v, t in s['viewers'].items() if now - t < self.idle_seconds}
                    if not s['viewers']:
                        self._stop(ident)

    def close(self):
        self.closed.set()
        with self.lock:
            for ident in list(self.sessions):
                self._stop(ident)


def candidates(data):
    """Probe known application paths: open ports alone do not identify streams."""
    import ipaddress
    template = str(data.get('template', '')).strip()
    if '{host}' not in template:
        raise ValueError('URL template must include {host}')
    target = str(data.get('target', '')).strip()
    if '/' in target:
        network = ipaddress.ip_network(target, strict=False)
        if network.version != 4 or network.num_addresses > 256:
            raise ValueError('Discovery is limited to an IPv4 /24 or smaller')
        hosts = [str(h) for h in network.hosts()]
    else:
        if not re.fullmatch(r'[A-Za-z0-9_.-]+', target):
            raise ValueError('Enter a hostname, IPv4 address, or IPv4 CIDR')
        hosts = [target]
    start, end = int(data.get('start', 0)), int(data.get('end', 8))
    if not 0 <= start <= end <= 255:
        raise ValueError('Stream indices must be between 0 and 255')
    indices = range(start, end + 1) if '{n}' in template else [0]
    if len(hosts) * len(indices) > 256:
        raise ValueError('At most 256 URLs per scan; narrow hosts or stream indices')
    result = []
    for host in hosts:
        for n in indices:
            try:
                url = template.format(host=host, n=n)
            except (KeyError, ValueError, IndexError):
                raise ValueError('Template supports only {host} and {n}')
            result.append(stream_url(url))
    return list(dict.fromkeys(result))


class Discovery:
    def __init__(self, prober=probe):
        self.prober = prober
        self.lock = threading.Lock()
        self.job = None
        self.cancelled = threading.Event()

    def start(self, data):
        urls = candidates(data)
        with self.lock:
            if self.job and self.job['state'] == 'running':
                raise ValueError('A discovery scan is already running')
            self.cancelled.clear()
            self.job = {'id': uuid.uuid4().hex, 'state': 'running', 'total': len(urls), 'completed': 0, 'found': [], 'failures': 0}
        threading.Thread(target=self._run, args=(urls,), daemon=True).start()
        return self.status()

    def _run(self, urls):
        def work(url):
            if self.cancelled.is_set():
                return url, None
            try:
                return url, self.prober(url)
            except Exception:
                return url, {'ok': False, 'error': 'Probe failed'}
        with ThreadPoolExecutor(max_workers=8) as pool:
            futures = [pool.submit(work, u) for u in urls]
            for f in as_completed(futures):
                url, result = f.result()
                with self.lock:
                    if result is not None:
                        self.job['completed'] += 1
                        if result.get('ok'):
                            self.job['found'].append({'url': url, 'tracks': result.get('tracks', [])})
                        else:
                            self.job['failures'] += 1
                            self.job['last_error'] = result.get('error')
        with self.lock:
            self.job['state'] = 'cancelled' if self.cancelled.is_set() else 'complete'

    def status(self):
        with self.lock:
            return json.loads(json.dumps(self.job or {'state': 'idle', 'found': []}))
