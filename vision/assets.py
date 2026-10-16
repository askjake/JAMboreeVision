"""Durable IDs and foreign-key links, independent of mutable names and IPs."""
import json
import sqlite3
import uuid
from contextlib import contextmanager
from pathlib import Path
from urllib.parse import urlsplit, urlunsplit


def stream_url(value):
    value = str(value or '')
    if any(ord(c) < 32 for c in value):
        raise ValueError('Stream URLs cannot contain control characters')
    value = value.strip()
    p = urlsplit(value)
    if p.scheme.lower() not in {'http', 'https', 'rtmp', 'rtmps', 'rtsp', 'rtsps', 'udp'} or not p.hostname:
        raise ValueError('Use an HTTP(S), RTMP(S), RTSP(S), or UDP stream URL')
    if p.username or p.password:
        raise ValueError('Credentials in stream URLs are not supported; use an authenticated upstream relay')
    if any(ord(c) < 32 for c in value) or p.fragment:
        raise ValueError('Stream URLs cannot contain control characters or fragments')
    _ = p.port  # validates port syntax/range
    return urlunsplit((p.scheme.lower(), p.netloc.lower(), p.path, p.query, ''))


def name(value):
    s = str(value or '').strip()
    if not s or len(s) > 100 or any(ord(c) < 32 for c in s):
        raise ValueError('Name must contain 1–100 printable characters')
    return s


class Assets:
    def __init__(self, path):
        self.path = Path(path)
        self.path.parent.mkdir(parents=True, exist_ok=True)
        with self.db() as c:
            c.executescript('''
                PRAGMA journal_mode=WAL;
                CREATE TABLE IF NOT EXISTS streams (
                  id TEXT PRIMARY KEY, name TEXT NOT NULL COLLATE NOCASE UNIQUE,
                  url TEXT NOT NULL UNIQUE, mode TEXT NOT NULL DEFAULT 'transcode',
                  created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP);
                CREATE TABLE IF NOT EXISTS remotes (
                  id TEXT PRIMARY KEY, name TEXT NOT NULL COLLATE NOCASE UNIQUE,
                  stb_alias TEXT NOT NULL,
                  stream_id TEXT REFERENCES streams(id) ON DELETE SET NULL,
                  created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP);
                CREATE TABLE IF NOT EXISTS settings (key TEXT PRIMARY KEY, value TEXT NOT NULL);
                CREATE TABLE IF NOT EXISTS command_history (
                  id INTEGER PRIMARY KEY AUTOINCREMENT,
                  started_at TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%fZ','now')),
                  alias TEXT NOT NULL, button TEXT NOT NULL, transport TEXT NOT NULL,
                  duration INTEGER NOT NULL, status TEXT NOT NULL, via TEXT,
                  elapsed_ms INTEGER, message TEXT NOT NULL);
                PRAGMA user_version=1;
            ''')

    @contextmanager
    def db(self):
        c = sqlite3.connect(self.path, timeout=10)
        c.row_factory = sqlite3.Row
        c.execute('PRAGMA foreign_keys=ON')
        try:
            with c:
                yield c
        finally:
            c.close()

    def recover_history(self):
        with self.db() as c:
            c.execute("UPDATE command_history SET status='interrupted', message='Server stopped before result was recorded; device outcome unknown' WHERE status='running'")

    def start_command(self, alias, button, transport, duration):
        with self.db() as c:
            ident = c.execute("INSERT INTO command_history(alias,button,transport,duration,status,message) VALUES(?,?,?,?,'running','Sending command')",
                              (alias, button, transport, duration)).lastrowid
            c.execute('DELETE FROM command_history WHERE id NOT IN (SELECT id FROM command_history ORDER BY id DESC LIMIT 1000)')
            return ident

    def finish_command(self, ident, status, via, elapsed_ms, message):
        with self.db() as c:
            c.execute('UPDATE command_history SET status=?,via=?,elapsed_ms=?,message=? WHERE id=?',
                      (status, via, elapsed_ms, message, ident))

    def history(self, alias=None):
        with self.db() as c:
            if alias is not None:
                rows = c.execute('SELECT * FROM command_history WHERE alias=? ORDER BY id DESC LIMIT 100', (alias,))
            else:
                rows = c.execute('SELECT * FROM command_history ORDER BY id DESC LIMIT 100')
            return [dict(r) for r in rows]

    def list(self, table):
        if table not in {'streams', 'remotes'}:
            raise ValueError('Unknown asset type')
        with self.db() as c:
            return [dict(r) for r in c.execute(f'SELECT * FROM {table} ORDER BY name COLLATE NOCASE')]

    def get(self, table, ident):
        return next((r for r in self.list(table) if r['id'] == ident), None)

    def save_stream(self, data, ident=None):
        label, url = name(data.get('name')), stream_url(data.get('url'))
        mode = data.get('mode', 'transcode')
        if mode not in {'copy', 'transcode'}:
            raise ValueError('Playback mode must be copy or transcode')
        with self.db() as c:
            if ident:
                if not c.execute('SELECT id FROM streams WHERE id=?', (ident,)).fetchone():
                    raise KeyError('Stream not found')
                c.execute('UPDATE streams SET name=?,url=?,mode=? WHERE id=?', (label, url, mode, ident))
            else:
                ident = uuid.uuid4().hex
                c.execute('INSERT INTO streams(id,name,url,mode) VALUES(?,?,?,?)', (ident, label, url, mode))
        return self.get('streams', ident)

    def save_remote(self, data, aliases, ident=None):
        label = name(data.get('name'))
        alias = data.get('stb_alias')
        if alias not in aliases:
            raise ValueError('Select an existing settop')
        linked = data.get('stream_id') or None
        with self.db() as c:
            if linked and not c.execute('SELECT id FROM streams WHERE id=?', (linked,)).fetchone():
                raise ValueError('Linked stream no longer exists')
            if ident:
                if not c.execute('SELECT id FROM remotes WHERE id=?', (ident,)).fetchone():
                    raise KeyError('Remote not found')
                c.execute('UPDATE remotes SET name=?,stb_alias=?,stream_id=? WHERE id=?', (label, alias, linked, ident))
            else:
                ident = uuid.uuid4().hex
                c.execute('INSERT INTO remotes(id,name,stb_alias,stream_id) VALUES(?,?,?,?)', (ident, label, alias, linked))
        return self.get('remotes', ident)

    def delete(self, table, ident):
        if table not in {'streams', 'remotes'}:
            raise ValueError('Unknown asset type')
        with self.db() as c:
            if not c.execute(f'DELETE FROM {table} WHERE id=?', (ident,)).rowcount:
                raise KeyError('Asset not found')

    def setting(self, key, default=None):
        with self.db() as c:
            row = c.execute('SELECT value FROM settings WHERE key=?', (key,)).fetchone()
            return json.loads(row[0]) if row else default

    def save_setting(self, key, value):
        with self.db() as c:
            c.execute('INSERT OR REPLACE INTO settings VALUES(?,?)', (key, json.dumps(value)))

    def resolve(self, alias):
        matches = [r for r in self.list('remotes') if (r['name'] + '-stream').casefold() == alias.casefold()]
        if not matches or not matches[0]['stream_id']:
            raise KeyError('Remote stream alias is not linked')
        return self.get('streams', matches[0]['stream_id'])
