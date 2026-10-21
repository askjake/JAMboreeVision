import sqlite3
import xml.etree.ElementTree as ET
import pytest
from filelock import FileLock, Timeout
from vision.windows_service import service_xml, migrate


def test_service_paths_identity_and_logs():
    root = ET.fromstring(service_xml(r'C:\ProgramData\JV & Lab\releases\one',
        r'C:\ProgramData\JV & Lab\data', r'C:\ffmpeg\bin', r'C:\ProgramData\JV & Lab\logs'))
    assert root.findtext('serviceaccount/username') == 'NT AUTHORITY\\LocalService'
    assert root.findtext('executable').endswith(r'one\.venv\Scripts\python.exe')
    assert '--data-dir "C:\\ProgramData\\JV & Lab\\data"' in root.findtext('arguments')
    env = {e.attrib['name']: e.attrib['value'] for e in root.findall('env')}
    assert env['JAMBOREE_BASE'].endswith(r'data\base.txt')
    assert env['JAMBOREE_CREDENTIAL_FILE'].endswith(r'data\sgs_credentials.dpapi.json')
    assert env['TEMP'].endswith(r'data\temp')
    assert root.findtext('startmode') == 'Automatic'
    assert root.find('log').attrib['mode'] == 'roll-by-size'
    assert root.findtext('log/keepFiles') == '8'
    with pytest.raises(ValueError):
        service_xml('bad"path', 'data', 'ffmpeg', 'logs')


def test_stopped_data_migration_including_wal(tmp_path):
    old, new = tmp_path/'old', tmp_path/'new'
    old.mkdir()
    (old/'base.txt').write_text('{"stbs": {"Bench": {"remote": "2", "com_port": "COM3"}}}')
    (old/'cert.pem').write_text('fixture')
    (old/'media').mkdir()
    db = sqlite3.connect(old/'assets.sqlite3')
    db.execute('PRAGMA journal_mode=WAL')
    db.execute('CREATE TABLE fixture(value)')
    db.execute('INSERT INTO fixture VALUES(42)')
    db.commit()
    migrate(old, new)
    assert (new/'base.txt').read_text() == (old/'base.txt').read_text()
    assert not (new/'media').exists()
    with sqlite3.connect(new/'assets.sqlite3') as copied:
        assert copied.execute('SELECT value FROM fixture').fetchone()[0] == 42
    db.close()
    with pytest.raises(ValueError, match='overwrite'):
        migrate(old, new)


def test_running_source_cannot_be_migrated(tmp_path):
    old = tmp_path/'old'
    old.mkdir()
    with FileLock(str(old/'server.lock')):
        with pytest.raises(Timeout):
            migrate(old, tmp_path/'new')


def test_migration_waits_for_stopping_instance(tmp_path):
    import threading
    old, new = tmp_path/'old', tmp_path/'new'
    old.mkdir()
    (old/'base.txt').write_text('fixture')
    held = threading.Event()
    release = threading.Event()
    def holder():
        with FileLock(str(old/'server.lock')):
            held.set()
            release.wait(5)
    thread = threading.Thread(target=holder)
    thread.start()
    assert held.wait(2)
    timer = threading.Timer(.15, release.set)
    timer.start()
    try:
        migrate(old, new, lock_timeout=2)
        assert (new/'base.txt').read_text() == 'fixture'
    finally:
        release.set()
        thread.join(3)
        timer.cancel()


def test_migration_timeout_never_copies_or_removes_held_lock(tmp_path):
    old, new = tmp_path/'old', tmp_path/'new'
    old.mkdir()
    (old/'base.txt').write_text('fixture')
    with FileLock(str(old/'server.lock')):
        with pytest.raises(Timeout):
            migrate(old, new, lock_timeout=.1)
        assert not (new/'base.txt').exists()
        assert (old/'server.lock').exists()


@pytest.mark.parametrize('timeout', [-1,121,float('inf'),float('nan')])
def test_migration_timeout_validation(tmp_path, timeout):
    with pytest.raises(ValueError, match='timeout'):
        migrate(tmp_path, tmp_path/'new', lock_timeout=timeout)
