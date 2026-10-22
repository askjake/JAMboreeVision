"""Run deterministic tests, upstream regression tests, and dependency checks."""
import shutil
import subprocess
import sys
from pathlib import Path
root=Path(__file__).resolve().parent
result=subprocess.run([sys.executable,'-m','pytest','-q'],cwd=root)
for exe in ('ffmpeg','ffprobe'):
    print(f'{exe}: {shutil.which(exe) or "MISSING (required for playback/discovery)"}')
raise SystemExit(result.returncode)
