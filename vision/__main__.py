import argparse
import os
import signal
from pathlib import Path
from filelock import FileLock, Timeout
from waitress import serve
from .app import configure, create_app


def main():
    p = argparse.ArgumentParser(description='JAMboreeVision local remote and stream console')
    p.add_argument('--host', default=os.getenv('JAMBOREE_VISION_HOST', '0.0.0.0'))
    p.add_argument('--port', type=int, default=int(os.getenv('JAMBOREE_VISION_PORT', '5004')))
    p.add_argument('--data-dir', default=None)
    args = p.parse_args()
    root = configure(args.data_dir)
    lock = FileLock(str(root / 'server.lock'))
    try:
        lock.acquire(timeout=0)
    except Timeout:
        p.exit(1, 'JAMboreeVision is already using this data directory.\n')
    app = create_app(root)
    def stop(*_):
        app.extensions['vision']['media'].close()
        raise SystemExit(0)
    signal.signal(signal.SIGTERM, stop)
    try:
        print(f'JAMboreeVision: http://localhost:{args.port} (bind {args.host})', flush=True)
        serve(app, host=args.host, port=args.port, threads=12)
    finally:
        app.extensions['vision']['media'].close()
        lock.release()

if __name__ == '__main__':
    main()
