"""Windows service configuration and stopped-instance data migration.

XML generation is portable so it can be verified without installing a service.
The PowerShell operator script owns Windows SCM/firewall operations.
"""
import argparse
import math
from pathlib import Path
import shutil
import sqlite3
import xml.etree.ElementTree as ET
from filelock import FileLock

SERVICE_ID = 'JAMboreeVision'


def service_xml(app_dir, data_dir, ffmpeg_dir, log_dir, port=5004):
    if not 1 <= int(port) <= 65535:
        raise ValueError('Port must be 1–65535')
    root = ET.Element('service')
    def node(key, text):
        ET.SubElement(root, key).text = str(text)
    # Use explicit Windows separators even when tests run on Linux.
    def win(path):
        return str(path).replace('/', '\\').rstrip('\\')
    app, data, ffmpeg, logs = map(win, (app_dir, data_dir, ffmpeg_dir, log_dir))
    for value in (app, data, ffmpeg, logs):
        if any(c in value for c in '\r\n"'):
            raise ValueError('Paths cannot contain quotes or line breaks')
    node('id', SERVICE_ID)
    node('name', 'JAMboreeVision')
    node('description', 'Shared settop remote control and network video console')
    node('executable', app + r'\.venv\Scripts\python.exe')
    node('arguments', f'-u -m vision --host 0.0.0.0 --port {int(port)} --data-dir "{data}"')
    node('workingdirectory', app)
    node('startmode', 'Automatic')
    node('delayedAutoStart', 'true')
    node('stoptimeout', '30 sec')
    ET.SubElement(root, 'onfailure', action='restart', delay='10 sec')
    node('resetfailure', '1 hour')
    account = ET.SubElement(root, 'serviceaccount')
    ET.SubElement(account, 'username').text = r'NT AUTHORITY\LocalService'
    for key, value in {
        'PATH': ffmpeg + r';%SystemRoot%\System32;%SystemRoot%',
        'JAMBOREE_VISION_DATA': data,
        'JAMBOREE_BASE': data + r'\base.txt',
        'JAMBOREE_CREDENTIAL_FILE': data + r'\sgs_credentials.dpapi.json',
        'JAMBOREE_SGS_CERT': data + r'\cert.pem',
        'JAMBOREE_SGS_KEY': data + r'\key.pem',
        'PYTHONDONTWRITEBYTECODE': '1',
        'PYTHONUNBUFFERED': '1',
        'TEMP': data + r'\temp', 'TMP': data + r'\temp',
    }.items():
        ET.SubElement(root, 'env', name=key, value=value)
    node('logpath', logs)
    log = ET.SubElement(root, 'log', mode='roll-by-size')
    ET.SubElement(log, 'sizeThreshold').text = '10240'
    ET.SubElement(log, 'keepFiles').text = '8'
    ET.indent(root)
    return ET.tostring(root, encoding='unicode') + '\n'


def migrate(source, target, lock_timeout=0):
    """Copy known durable files only, taking source/destination instance locks."""
    lock_timeout = float(lock_timeout)
    if not math.isfinite(lock_timeout) or not 0 <= lock_timeout <= 120:
        raise ValueError("Lock timeout must be between 0 and 120 seconds")
    source, target = Path(source).resolve(), Path(target).resolve()
    if not source.is_dir() or source == target:
        raise ValueError('Choose an existing source data directory distinct from the destination')
    target.mkdir(parents=True, exist_ok=True)
    names = ('base.txt', 'assets.sqlite3', 'cert.pem', 'key.pem', 'sgs_credentials.dpapi.json')
    # SCM can report Stopped before the child process releases its data lock.
    # Wait only when explicitly requested; never unlink or bypass a held lock.
    with FileLock(str(source / 'server.lock'), timeout=lock_timeout), FileLock(str(target / 'server.lock'), timeout=0):
        if any((target / n).exists() for n in names):
            raise ValueError('Destination already contains configuration; migration will not overwrite it')
        for filename in names:
            src = source / filename
            if not src.is_file():
                continue
            if filename == 'assets.sqlite3':
                with sqlite3.connect(src.as_uri() + '?mode=ro', uri=True) as old, sqlite3.connect(target / filename) as new:
                    old.backup(new)
            else:
                shutil.copy2(src, target / filename)


def main():
    p = argparse.ArgumentParser()
    sub = p.add_subparsers(dest='action', required=True)
    xml = sub.add_parser('xml')
    for option in ('app-dir', 'data-dir', 'ffmpeg-dir', 'log-dir', 'output'):
        xml.add_argument('--' + option, required=True)
    xml.add_argument('--port', type=int, default=5004)
    migration = sub.add_parser('migrate')
    migration.add_argument('--source', required=True)
    migration.add_argument('--target', required=True)
    migration.add_argument('--lock-timeout', type=float, default=0)
    args = p.parse_args()
    if args.action == 'migrate':
        migrate(args.source, args.target, lock_timeout=args.lock_timeout)
    else:
        Path(args.output).write_text(service_xml(args.app_dir, args.data_dir, args.ffmpeg_dir, args.log_dir, args.port), encoding='utf-8')

if __name__ == '__main__':
    main()
