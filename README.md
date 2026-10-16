# JAMboreeVision

A single local web application for settop control, linked video, and network stream discovery.

- **Remote console:** searchable remotes, working SGS/DART buttons, a persistent per-remote stream dropdown, independent stream checkboxes, and multiple in-page players.
- **Settops & remotes:** receiver/IP/MAC configuration, Hopper/Joey topology, SGS PIN pairing, serial ports/channels, transport status, and IP recovery.
- **Streams & discovery:** saved source URLs, edit/delete/probe actions, configurable host/CIDR discovery with video-track verification, and playback mode selection.

A remote named `Bench Hopper` linked to a video source exposes the reference `Bench Hopper-stream`. The reference resolves through `GET /api/vision/resolve/Bench%20Hopper-stream`. Stream and remote UUIDs preserve links across renames, source URL changes, and restarts. Multiple remotes may share one source; the app uses one FFmpeg worker per selected source.

## Windows laptop: deploy from PowerShell

Prerequisites: Git, **Python 3.10+** (3.11/3.12 recommended), and FFmpeg with both `ffmpeg` and `ffprobe` on PATH. Install FFmpeg using your approved package manager or a build linked from https://ffmpeg.org/download.html. Reopen the terminal after changing PATH. No Node.js or Streamlit is needed to run the app.

```powershell
git clone --branch montjac --single-branch git@github.com:askjake/JAMboreeVision.git
Set-Location JAMboreeVision
py -3 -m venv .venv
.\.venv\Scripts\python.exe -m pip install -r requirements-dev.txt -r requirements-ocr.txt
ffmpeg -version
ffprobe -version
.\.venv\Scripts\python.exe verify.py
.\.venv\Scripts\python.exe -m vision
```

Open **http://localhost:5004**. The default bind is `0.0.0.0` for LAN use; from another device use `http://<laptop-ip>:5004`. Use `--host 127.0.0.1` for laptop-only access. Allow Python through Windows Firewall on the intended private network if LAN clients need access. This is a trusted-lab service without user authentication; do not expose it to the public Internet.

Stop with **Ctrl+C** in its terminal. Future starts: `start.cmd`, or `powershell -NoProfile -ExecutionPolicy Bypass -File .\start.ps1`. It never kills unrelated FFmpeg processes or services that occupy another port. If 5004 is occupied, use `python -m vision --port 5005`.

For runtime-only installation, `requirements.txt` suffices. The development and OCR requirements above enable the full upstream regression suite. Optional OCR automatic pairing additionally requires an installed Tesseract executable and a configured frame provider; manual PIN pairing works without it. Linked playback is not automatically used as an OCR frame source.

## Import existing assets (stop Vision first)

No import or network scan happens automatically. Import from your own local files; the original installation is not modified. Substitute your actual JAMboreeLite path:

```powershell
.\.venv\Scripts\python.exe -m vision.migrate --legacy-base "C:\path\to\JAMboreeLite\base.txt" --legacy-certs "C:\path\to\JAMboreeLite\jamboree" --sling-base "rtmp://10.79.86.118:1935"
.\.venv\Scripts\python.exe -m vision
```

The Sling base above comes from the original Sling-viewer launcher; replace it for another network. It registers `live0` through `live8` without starting playback or claiming they are online. `--first` and `--last` change the range. Stream names already used for different URLs produce a clear conflict; resolve that source in the UI. Imports are additive and repeatable. Existing destination settop rows and stream URLs win. Each imported settop gets a remote unless already referenced. Choose the stream links in **Settops & remotes** afterward.

`--legacy-certs` optionally copies missing `cert.pem`/`key.pem` files into the local data directory; they are not included in this repository. Existing OS-keyring credentials remain keyed by the original settop alias. On Windows, the retained credential layer also supports JAMboreeLite's machine-protected DPAPI fallback. Re-pair if moving to a different machine/user or no existing credentials are available. Plaintext legacy credentials remain disabled by default.

## First-use and hardware acceptance

1. In **Settops & remotes**, add/import a settop: alias, receiver ID, IP, SGS or RF, model, serial port/channel if applicable. For a Joey, configure its host Hopper first and select that host. Settop aliases are immutable in the editor to preserve pairing identity.
2. For SGS, select **Pair**, read the PIN on the settop (or linked video), and complete pairing. Check **Status**. For RF, connect your DART hardware and configure its COM port and channel 1–16.
3. In **Streams & discovery**, enter a host and URL pattern, e.g. `rtmp://{host}:1935/live{n}` with indices 0–8. For HLS use `http://{host}:8088/live{n}/index.m3u8`. Add found streams, or manually add a known source URL. Discovery requires known path patterns: it cannot enumerate every unknown URL on arbitrary servers.
4. Create a remote for each desired settop and select its linked stream. In **Remote console**, select the remote and **Watch linked stream**. Select additional stream checkboxes for a multi-view layout.
5. Test **Guide**, arrows, **OK**, **Back**, and **Play/Pause** against the actual device. Check the command result (`sgs`, `rf`, or fallback). Observe the corresponding video, not another device. Test **SGS only** and **RF only** separately where both are available.
6. Rename a remote; verify the new `name-stream` reference. Restart Vision and confirm all assets and links remain. Open two browser tabs on one stream; closing one must leave the other playing.
7. If applicable, test stale-IP recovery with a known receiver and check that the verified RxID/MAC matches before accepting its new address. The source transport's recovery logic is retained.

## Linux / macOS

```bash
git clone --branch montjac --single-branch git@github.com:askjake/JAMboreeVision.git
cd JAMboreeVision
python3 -m venv .venv
.venv/bin/python -m pip install -r requirements-dev.txt -r requirements-ocr.txt
# Install FFmpeg through your system's package manager first.
.venv/bin/python verify.py
bash start.sh
```

SGS pairing needs a usable OS keyring. A headless Linux session without Secret Service/keyring cannot persist new credentials; the app reports failure instead of silently saving plaintext. Serial access may require your OS's serial-device group/permissions.

## Update without leaving your branch

Stop the app, then:

```powershell
git branch --show-current
# The branch should be montjac.
git pull --ff-only origin montjac
.\.venv\Scripts\python.exe -m pip install -r requirements-dev.txt -r requirements-ocr.txt
.\.venv\Scripts\python.exe verify.py
.\.venv\Scripts\python.exe -m vision
```

## Persistence and playback

Data defaults to `~/.jamboreevision` (`C:\Users\<you>\.jamboreevision` on Windows), outside the checkout. Use `--data-dir` or `JAMBOREE_VISION_DATA` to change it. `base.txt` retains the transport configuration with atomic writes and backup behavior from JAMboreeLite. `assets.sqlite3` stores remotes, streams, links, and discovery settings with transactions/WAL/foreign keys. Deleting a stream clears its links; deleting a referenced settop is rejected. Back up the entire data directory **with the app stopped**; back up credentials using the OS's supported mechanism. Avoid setting a shared `JAMBOREE_BASE` unless intentionally sharing transport configuration with JAMboreeLite.

RTMP, RTSP, HTTP/HLS, and UDP sources are normalized through FFmpeg to same-origin HLS; browser playback requires no cross-origin stream permissions. `Transcode` produces browser-compatible H.264/AAC; `Copy` saves CPU for compatible H.264 sources with regular keyframes. HLS adds several seconds of latency. Start with a few windows on a laptop; transcodes cost CPU. Video starts muted, with native audio/fullscreen controls. There is a 16-worker ceiling.

One worker serves all viewers of a stream. Each browser renews its lease every 20 seconds; abandoned leases expire after 65 seconds. Closing the last video stops that worker. Edits replace the prior stream source; failed workers can restart on retry/lease renewal. Graceful shutdown stops owned children. A forced OS kill can leave children behind; use normal shutdown. Expired media URLs cannot be used to access arbitrary files.

The bundled HLS.js asset supports offline UI loading. Discovery checks at most 256 candidate URLs with eight probes at once and per-probe timeouts. A successful probe proves a video track was readable then, not permanent availability or a particular settop identity; link sources deliberately.

See [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md), [docs/PROVENANCE.md](docs/PROVENANCE.md), and [docs/VALIDATION.md](docs/VALIDATION.md).
