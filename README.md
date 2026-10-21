# JAMboreeVision

A single local web application for settop control, linked video, and network stream discovery.

**Complete setup and operator procedure:** [Method of Procedure (MOP)](docs/MOP.md) — fresh Windows/Linux/macOS deployment, updates, backups/rollback, importing, every GUI feature, and troubleshooting.

- **Remote console:** searchable remotes, working SGS/DART buttons, a persistent per-remote stream dropdown, independent stream checkboxes, and multiple in-page players.
- **Settops & remotes:** receiver/IP/MAC configuration, Hopper/Joey topology, SGS PIN pairing, serial ports/channels, transport status, and IP recovery.
- **Streams & discovery:** saved source URLs, edit/delete/probe actions, configurable host/CIDR discovery with video-track verification, and playback mode selection.

**Streams are optional.** Every configured settop is immediately available in the remote console; use **Control** in its table row. No separate remote profile or video is needed to send commands. A linked stream may remain offline without disabling buttons. Create and use SGS/DART remotes without adding or selecting any video source. Choose **No stream — remote control only** in the optional stream dropdown. FFmpeg is needed for video features, not remote control.

A remote named `Bench Hopper` linked to a video source exposes the reference `Bench Hopper-stream`. The reference resolves through `GET /api/vision/resolve/Bench%20Hopper-stream`. Stream and remote UUIDs preserve links across renames, source URL changes, and restarts. Multiple remotes may share one source; the app uses one FFmpeg worker per selected source.

## Windows laptop: deploy from PowerShell

Prerequisites: Git, **Python 3.10+** (3.11/3.12 recommended), and, for optional video features, FFmpeg with both `ffmpeg` and `ffprobe` on PATH. Install FFmpeg using your approved package manager or a build linked from https://ffmpeg.org/download.html. Reopen the terminal after changing PATH. No Node.js or Streamlit is needed to run the app.

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

## Import configuration through the GUI

Open **Settops & remotes → Import config…**. Your operating system's file picker opens; browse to a local `.txt` or `.json` file and select it. Review the import counts, then click **Import configuration**. No restart is required. When using Vision over the LAN, the selected file comes from the computer running your browser and is sent to the Vision server.

Supported files (UTF-8 JSON, up to 1 MiB):

- JAMboreeLite `base.txt` with a `stbs` object, or a bare alias-to-settop object.
- JSON containing `stbs`, `streams`, or both. Each stream needs `name` and `url`; optional `mode` is `copy` or `transcode`.

```json
{
  "stbs": {
    "Bench Hopper": {
      "stb": "R0000000000", "ip": "192.168.1.50",
      "protocol": "SGS", "remote": "1", "role": "hopper"
    }
  },
  "streams": [
    {"name": "Bench video", "url": "rtmp://192.168.1.60:1935/live0", "mode": "copy"}
  ]
}
```

Existing settop aliases and stream URLs are skipped. Existing configuration and remote links are preserved. Imported settops receive a remote if they do not already have one. Select the corresponding stream link afterward. A stream name already assigned to another URL is reported before importing. Reimporting the same file does not duplicate assets. The preview is recalculated on import to account for intervening changes.

The browser importer omits top-level credential fields from settop entries and does not import certificates/private keys; use the existing OS keyring or pair through the GUI. It imports asset tables, not arbitrary global configuration or executable launcher scripts. Sling-viewer's Python/PowerShell files are not JSON configuration files; add its URLs through discovery, the JSON format above, or the CLI below.

## Import existing assets through the CLI (stop Vision first)

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

## Linux deployment (Ubuntu / Debian)

```bash
sudo apt-get update
sudo apt-get install -y git python3 python3-venv python3-pip ffmpeg

git clone --branch montjac --single-branch git@github.com:askjake/JAMboreeVision.git
cd JAMboreeVision
python3 -m venv .venv
.venv/bin/python -m pip install --upgrade pip
.venv/bin/python -m pip install -r requirements-dev.txt -r requirements-ocr.txt
ffmpeg -version
ffprobe -version
.venv/bin/python verify.py
bash start.sh --host 0.0.0.0 --port 5004
```

Open http://localhost:5004. Use `--host 127.0.0.1` for local-only access. Stop with Ctrl+C; run `bash start.sh` for subsequent starts. Use Python 3.10 or newer.

SGS pairing needs an available OS keyring. A headless Linux session without Secret Service/keyring cannot persist new credentials; the app reports failure instead of silently saving plaintext. For DART serial access on Ubuntu/Debian, add your user to the serial group if needed and log out/in afterward:

```bash
sudo usermod -aG dialout "$USER"
```

Serial devices are commonly `/dev/ttyACM0` or `/dev/ttyUSB0`; select your actual device in settop configuration.

## macOS deployment

With Homebrew installed:

```bash
brew install git python@3.12 ffmpeg

git clone --branch montjac --single-branch git@github.com:askjake/JAMboreeVision.git
cd JAMboreeVision
"$(brew --prefix python@3.12)/bin/python3.12" -m venv .venv
.venv/bin/python -m pip install --upgrade pip
.venv/bin/python -m pip install -r requirements-dev.txt -r requirements-ocr.txt
ffmpeg -version
ffprobe -version
.venv/bin/python verify.py
bash start.sh --host 0.0.0.0 --port 5004
```

Open http://localhost:5004. Stop with Ctrl+C. Subsequent starts use `bash start.sh`. Serial devices generally appear under `/dev/cu.*`. macOS may request Keychain access when pairing.

For any platform, if you use HTTPS Git authentication instead of SSH, substitute `https://github.com/askjake/JAMboreeVision.git` in the clone command. The repository must be accessible to your GitHub account.

## Update without leaving your branch

Stop the app with Ctrl+C before updating. These commands retain `montjac` and use fast-forward-only pulls. If you have local edits or a divergent branch, Git will stop; resolve those changes before continuing. Data in `~/.jamboreevision` is outside the checkout and survives updates.

Windows PowerShell, from your existing repository directory:

```powershell
git switch montjac
git branch --show-current
git pull --ff-only origin montjac
.\.venv\Scripts\python.exe -m pip install -r requirements-dev.txt -r requirements-ocr.txt
.\.venv\Scripts\python.exe verify.py
.\.venv\Scripts\python.exe -m vision
```

Linux / macOS, from your existing repository directory:

```bash
git switch montjac
git branch --show-current
git pull --ff-only origin montjac
.venv/bin/python -m pip install -r requirements-dev.txt -r requirements-ocr.txt
.venv/bin/python verify.py
bash start.sh --host 0.0.0.0 --port 5004
```

After updating, hard-refresh the browser (Ctrl+Shift+R on Windows/Linux, Cmd+Shift+R on macOS) to reload the UI assets.

## Persistence and playback

Data defaults to `~/.jamboreevision` (`C:\Users\<you>\.jamboreevision` on Windows), outside the checkout. Use `--data-dir` or `JAMBOREE_VISION_DATA` to change it. `base.txt` retains the transport configuration with atomic writes and backup behavior from JAMboreeLite. `assets.sqlite3` stores remotes, streams, links, and discovery settings with transactions/WAL/foreign keys. Deleting a stream clears its links; deleting a referenced settop is rejected. Back up the entire data directory **with the app stopped**; back up credentials using the OS's supported mechanism. Avoid setting a shared `JAMBOREE_BASE` unless intentionally sharing transport configuration with JAMboreeLite.

RTMP, RTSP, HTTP/HLS, and UDP sources are normalized through FFmpeg to same-origin HLS; browser playback requires no cross-origin stream permissions. `Transcode` produces browser-compatible H.264/AAC; `Copy` saves CPU for compatible H.264 sources with regular keyframes. HLS adds several seconds of latency. Start with a few windows on a laptop; transcodes cost CPU. Video starts muted, with native audio/fullscreen controls. There is a 16-worker ceiling.

One worker serves all viewers of a stream. Each browser renews its lease every 20 seconds; abandoned leases expire after 65 seconds. Closing the last video stops that worker. Edits replace the prior stream source; failed workers can restart on retry/lease renewal. Graceful shutdown stops owned children. A forced OS kill can leave children behind; use normal shutdown. Expired media URLs cannot be used to access arbitrary files.

The bundled HLS.js asset supports offline UI loading. Discovery checks at most 256 candidate URLs with eight probes at once and per-probe timeouts. A successful probe proves a video track was readable then, not permanent availability or a particular settop identity; link sources deliberately.

See [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md), [docs/PROVENANCE.md](docs/PROVENANCE.md), and [docs/VALIDATION.md](docs/VALIDATION.md).


### Additional remote controls

The remote console includes **SAT**, **TV**, **AUX**, and **INPUT**, using the
selected transport and press duration and the existing JAMboree button mappings.
**Reset remotes (DART)** sends the existing DART reset command for the selected
settop's configured remote channel. It does not delete remote profiles or links.
**Unpair (RF) remote** always uses DART/RF, regardless of the transport dropdown:
choose **DVR + GUIDE** or **HOME + GUIDE**, then click Unpair. SAT is held for
3 seconds and released; after a 200 ms gap, both chosen keys are held together
for 3 seconds and released. Other console commands are blocked while the
sequence runs. A configured, working DART serial connection is required for
reset and unpair. Neither action requires video. Delivery status confirms serial
writes; verify unpairing on the physical remote/receiver.


### Host serial ports and command feedback

Add/Edit settop and Add/Edit remote include a host USB/COM port dropdown, a
**Rescan ports** button, and manual path entry. Detection runs on the server
hosting JAMboreeVision (Linux, macOS, or Windows), not the browser computer.
Configured disconnected ports remain selectable. Detection does not prove that
a port is accessible or is DART hardware. Choose the correct port and DART
channel; these are shared by all profiles referencing that settop. Saving a
stream link alone leaves its serial settings unchanged.

The remote console shows sending/result feedback and a **Remote command history**
table, refreshed every three seconds. It defaults to the active settop; choose
**All settops** for the shared history. Records include UTC timestamp (displayed
in browser local time), command, requested transport/hold, actual transport when
known, elapsed time, and running/sent/failed/rejected/interrupted status. The
server retains the latest 1,000 entries in assets.sqlite3 and displays up to 100
matching entries. Unfinished records become interrupted after restart, indicating
an unknown device outcome. History covers the unified console command endpoints,
not legacy API callers, pairing, or recovery operations. Invalid requests rejected
before dispatch are not recorded. Serial delivery means the write completed;
SGS completion also does not visually verify the receiver action. Failure history
stores a generic diagnostic without raw transport responses or credentials.


### Terminal serial traffic

Normal startup now logs DART serial traffic at INFO level, with timestamp and
port: `TX -> b'1 83 03 120\n'`, `TX flushed (12 bytes)`, and
`RX <- b'board response\r\n'` (illustrative values). TX records attempted bytes;
TX flushed records a completed serial write, not receiver confirmation. RX is
printed whenever the board sends data, even without a GUI command. Byte escapes
preserve line endings and prevent board control characters changing the terminal.
No debug flag is required. To also save the terminal output on Linux/macOS:

```bash
.venv/bin/python -m vision --host 0.0.0.0 --port 5004 2>&1 | tee -a jamboreevision-console.log
```

When a port is reassigned, the old worker cancels its read and closes the device
from its own thread, avoiding a close/read race during port switching.


### Mic / voice key

The remote console's **🎤 Mic** button uses the existing backend `mic` mapping:
DART button 7 (press `97`, release `17`) or SGS `Mic`. The selected transport
and press duration apply. To send the physical remote key for an RF pairing
workflow, select **RF only**, select the required tap/hold duration (including
**Hold · 3 s**), and click **Mic**. Confirm the pairing result on the receiver;
this sends a key and does not itself verify pairing. No stream is required.
Mic commands appear in command history; RF traffic also appears in terminal
TX/RX logs. This button sends the remote key, not audio from the browser's mic.


### Windows: FFmpeg is not found after a PATH update

Do not use `setx PATH "$env:PATH;..."`. It does not update the current terminal
and can truncate the persisted value to 1,024 characters. If its warning has
already appeared, keep the original PowerShell window open and preserve its
process PATH before closing it. The process PATH may still contain entries
lost from the saved user PATH; it is a merged process value, not an exact backup
of the original user PATH, so do not blindly write it back as the user PATH.

Run in that original PowerShell window (replace the FFmpeg folder if different):

```powershell
& {
    $ErrorActionPreference = 'Stop'
    $backup = Join-Path $env:USERPROFILE ('jv-path-backup-' + (Get-Date -Format 'yyyyMMdd-HHmmss') + '.json')
    [ordered]@{
        ProcessPath = $env:PATH
        UserPath = [Environment]::GetEnvironmentVariable('Path', 'User')
        MachinePath = [Environment]::GetEnvironmentVariable('Path', 'Machine')
    } | ConvertTo-Json | Set-Content -LiteralPath $backup -Encoding UTF8
    Write-Host "PATH snapshot: $backup"
    $ffmpegBin = 'C:\ffmpeg\ffmpeg-master-latest-win64-gpl\bin'
    foreach ($tool in 'ffmpeg.exe', 'ffprobe.exe') {
        if (-not (Test-Path -LiteralPath (Join-Path $ffmpegBin $tool) -PathType Leaf)) {
            throw "Missing $tool in $ffmpegBin. Check the extracted folder."
        }
    }
    $env:PATH = "$ffmpegBin;$env:PATH"
    ffmpeg -version
    if ($LASTEXITCODE -ne 0) { throw 'FFmpeg verification failed' }
    ffprobe -version
    if ($LASTEXITCODE -ne 0) { throw 'ffprobe verification failed' }
}
```

Launch Vision from this same terminal so it inherits the working PATH. This fix
changes only the current process and its children; it does not repair a user
PATH already truncated by setx. For a persistent installation, use Windows
**Edit environment variables for your account → Path → Edit → New** to add the
verified FFmpeg `bin` directory. Review any truncated entries and restore missing
user entries using a known backup or the preserved process snapshot as evidence.
Leave the machine PATH intact. If the folder check fails, locate both executables
in the extracted FFmpeg distribution before changing PATH.

## One shared Windows service for the department

Use [the Windows service deployment guide](docs/WINDOWS-SERVICE.md) for one
always-on host and a shared `http://HOSTNAME:5004` URL. It includes a ZIP-based
installation without Git, explicit Python/FFmpeg paths, LocalService identity,
configuration migration, department-subnet firewall rule, rotating serial logs,
automatic startup/recovery, staged updates, and code rollback. Clients need only
a browser. Native Windows service/hardware acceptance is required before rollout.


## Live playback and DART recovery controls

Each new video window starts near the latest available HLS video and follows
that moving edge. A **Go live** button returns a paused/delayed viewer to the
latest available video and resumes playback. Seeking backward or pausing using
the native video controls disables automatic catch-up for that window. Other
windows keep their own playback choices. Automatically reconnecting a player
preserves delayed/paused intent and restores the closest available position if
the old position has expired from the live window. Closing and reopening a video
window starts in live-following mode again.

The status below the video distinguishes **Live**, **Delayed**, **Paused**, and
**Buffering**. The seconds shown measure distance from the player's available
stream edge, not the STB's capture time. HLS uses a safety buffer (typically
several seconds), and an upstream source can itself be delayed. A video that
appears unchanged is not evidence that the remote command failed: click Go live
and confirm the correct stream/settop association before diagnosing RF delivery.
Muted autoplay is used; browsers can still require a manual Play gesture.

**DART power reset (99)** uses the existing reset path and sends
`<configured channel> 99 reset`. **Release all buttons (86)** sends
`<configured channel> 86 allup`. These use the selected settop's configured
COM port/channel regardless of the SGS/RF transport selector and work without
video. They are different from the normal remote Power key. In the supplied
16-remote DART sketch, reset pulses a shared reset pin; firmware/hardware decide
which remote circuits reset. These commands do not establish that the STB
accepted an RF command or power-cycle the STB itself. Board firmware variants
must be checked on hardware.

The service installer now polls startup transitions through the HTTP-readiness
deadline, fixing the premature "Service stopped" report seen during the initial
Windows deployment. No data migration is needed for these changes.

Player regression checks: `npm run test:player` (Node 22+), `npm run test:dom`
(after `npm install`), and the optional real-browser acceptance flow
`npm run test:browser`. The Python suite checks exact DART wire output and history
for direct settop and remote-profile routes, including forced SGS selection.
