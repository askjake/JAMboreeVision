# JAMboreeVision — Method of Procedure and Operator Guide

**Revision:** 2026-10-16  
**Repository:** https://github.com/askjake/JAMboreeVision  
**Deployment branch:** `montjac`  
**Implementation baseline reviewed:** `52a0b590502a0d99cf3b7352fe41cc8ea1587ac0`  
**Audience:** installers, lab operators, and maintainers on Windows, Linux, and macOS.

## 1. Purpose and completion criteria

Use this procedure to install JAMboreeVision from scratch, configure settop control and network video, operate every current GUI feature, update an existing installation, and recover from a failed change.

Installation is complete when the service starts, the browser shows the three tabs and **Import config…**, the automated checks pass, and FFmpeg is available if video features are required. Operational acceptance additionally requires a real button press reaching the intended settop and its linked video showing the resulting action. Passing software tests alone does not establish receiver, serial, pairing, or lab-network acceptance.

### Navigation

- [2. What runs where](#2-what-runs-where)
- [3. Before starting](#3-before-starting)
- [4. Fresh Linux installation](#4-fresh-linux-installation-ubuntu--debian)
- [5. Fresh macOS installation](#5-fresh-macos-installation)
- [6. Fresh Windows installation](#6-fresh-windows-installation)
- [7. Start, stop, and check health](#7-start-stop-and-check-health)
- [8. Import existing configuration](#8-import-existing-configuration)
- [9. Configure settops and control](#9-configure-settops-and-control)
- [10. Discover and manage streams](#10-discover-and-manage-streams)
- [11. Link remotes and watch video](#11-link-remotes-and-watch-video)
- [12. Acceptance checklist](#12-acceptance-checklist)
- [13. Update an installation](#13-update-an-installation)
- [14. Rollback and restore](#14-rollback-and-restore)
- [15. Troubleshooting](#15-troubleshooting)
- [16. Advanced operations](#16-advanced-operations)
- [17. Persistence and limits](#17-persistence-and-limits)

## 2. What runs where

| Component | Location | Purpose |
|---|---|---|
| Python / Waitress service | Server computer | Stores assets, sends SGS/DART commands, probes streams, runs FFmpeg |
| FFmpeg and ffprobe | Server computer | Converts selected sources to HLS and verifies discovered video |
| Browser | Server or another LAN computer | Shows controls and video; chooses local import files |
| Settop | Lab network / connected DART fixture | Receives commands |
| Stream source | Reachable from server | Supplies RTMP, RTSP, HTTP/HLS, or UDP video |
| Serial DART adapter | Connected to server | Provides RF control through the selected serial channel |

A remote browser does **not** need Python or FFmpeg. Its file picker sees files on the browser computer, not files on the server. A source URL must be reachable from the server even if the browser can reach it directly.

Default service port: **5004**. Default bind: **0.0.0.0**. Browser address on the server: **http://localhost:5004**. From another computer: **http://SERVER_IP:5004**.

**Current 3080 example:** http://10.79.85.47:5004. Here, `localhost` on your Windows laptop means the laptop, not the 3080.

The three tabs are **Remote console**, **Settops & remotes**, and **Streams & discovery**. This release is intended for a trusted LAN and has no user-login/access-control layer. Keep its listener on the intended network.

## 3. Before starting

Record the server name, server IP, intended port, OS, repository directory, deployment branch, and data directory. For each receiver, collect its alias, receiver ID, current IP, model, topology, and preferred transport. For RF, identify the serial port and DART channel. For video, collect a known source host and stream path or URL template.

Required for remote control: Git, Python **3.10 or newer** (3.11/3.12 recommended), and a modern browser. Video discovery/playback additionally needs FFmpeg including ffprobe on the server and HLS.js or native HLS support in the browser. The full-install commands below include these optional video tools; a remote-only installation may omit the FFmpeg system package. SGS credential persistence requires a usable OS keyring; Windows also has the retained DPAPI fallback.

The commands below install the test and optional OCR Python dependencies so the full test suite can run. Node.js is only needed for optional DOM/browser tests. Tesseract is only needed for advanced OCR-based pairing, not manual PIN entry.

Use one OS-specific installation section. Run commands as your normal user; use administrator privileges only for system package/driver installation. Do not run Vision with sudo.

The Git clone examples use SSH. If your GitHub account uses HTTPS, replace the repository URL with `https://github.com/askjake/JAMboreeVision.git`. A failed clone is a GitHub access/authentication problem; resolve that before continuing. If `JAMboreeVision` already exists, use the update procedure rather than cloning over it.

**Execution rule:** stop at any failed command. Do not launch a candidate whose dependency installation or verification failed. The bash installation/update blocks use a subshell so failure does not close your interactive login. PowerShell blocks explicitly check native command results.

## 4. Fresh Linux installation (Ubuntu / Debian)

Install system prerequisites:

```bash
sudo apt-get update
sudo apt-get install -y git python3 python3-venv python3-pip ffmpeg
```

Install and verify the application in your home directory:

```bash
bash <<'JV_LINUX_INSTALL'
set -Eeuo pipefail
cd "$HOME"
git clone --branch montjac --single-branch git@github.com:askjake/JAMboreeVision.git
cd JAMboreeVision
python3 --version
python3 -m venv .venv
.venv/bin/python -m pip install --upgrade pip
.venv/bin/python -m pip install -r requirements-dev.txt -r requirements-ocr.txt
ffmpeg -version
ffprobe -version
.venv/bin/python verify.py
git branch --show-current
git rev-parse HEAD
JV_LINUX_INSTALL
```

Expected: branch `montjac`, successful tests, and paths for both FFmpeg tools. The real media test should run when FFmpeg is installed. Record any skipped tests; do not treat a missing-media skip as playback verification.

For RF/DART, connect the adapter and identify the device:

```bash
ls /dev/ttyACM* /dev/ttyUSB* 2>/dev/null
```

If your account lacks serial access on Ubuntu/Debian:

```bash
sudo usermod -aG dialout "$USER"
```

Log out and back in after changing group membership. Do not guess the adapter from a port number alone when several adapters are attached. Close another application using the same adapter before starting RF control.

Start the app:

```bash
cd ~/JAMboreeVision
.venv/bin/python -m vision --host 0.0.0.0 --port 5004
```

For other Linux distributions, install equivalent Git/Python/venv/FFmpeg packages with the distribution's package manager, then follow the Python/application steps above.

## 5. Fresh macOS installation

With Homebrew installed, install prerequisites:

```bash
brew install git python@3.12 ffmpeg
```

Install and verify:

```bash
bash <<'JV_MAC_INSTALL'
set -Eeuo pipefail
cd "$HOME"
git clone --branch montjac --single-branch git@github.com:askjake/JAMboreeVision.git
cd JAMboreeVision
"$(brew --prefix python@3.12)/bin/python3.12" -m venv .venv
.venv/bin/python -m pip install --upgrade pip
.venv/bin/python -m pip install -r requirements-dev.txt -r requirements-ocr.txt
ffmpeg -version
ffprobe -version
.venv/bin/python verify.py
git branch --show-current
git rev-parse HEAD
JV_MAC_INSTALL
```

Start:

```bash
cd ~/JAMboreeVision
.venv/bin/python -m vision --host 0.0.0.0 --port 5004
```

For RF/DART, identify the attached serial adapter:

```bash
ls /dev/cu.*
```

Use the corresponding `/dev/cu.*` path in the settop editor. Install the adapter manufacturer's driver if required. macOS may prompt for Keychain access during pairing; use the keyring belonging to the account running Vision.

## 6. Fresh Windows installation

Install Git, Python 3.10+ with the `py` launcher, and FFmpeg/ffprobe using your approved installer or package manager. FFmpeg builds are linked from https://ffmpeg.org/download.html. Put the directory containing both executables on PATH, then open a **new PowerShell window**. Install any DART serial-adapter driver needed by your hardware.

The following example clones under your user profile. Change the parent directory if desired:

```powershell
$ErrorActionPreference = 'Stop'
Set-Location $HOME

git clone --branch montjac --single-branch git@github.com:askjake/JAMboreeVision.git
if ($LASTEXITCODE -ne 0) { throw 'Clone failed.' }
Set-Location JAMboreeVision

py -3 -m venv .venv
if ($LASTEXITCODE -ne 0) { throw 'Virtual environment creation failed.' }
.\.venv\Scripts\python.exe -m pip install --upgrade pip
if ($LASTEXITCODE -ne 0) { throw 'pip upgrade failed.' }
.\.venv\Scripts\python.exe -m pip install -r requirements-dev.txt -r requirements-ocr.txt
if ($LASTEXITCODE -ne 0) { throw 'Dependency installation failed.' }

Get-Command ffmpeg, ffprobe -ErrorAction Stop
ffmpeg -version
if ($LASTEXITCODE -ne 0) { throw 'FFmpeg check failed.' }
ffprobe -version
if ($LASTEXITCODE -ne 0) { throw 'ffprobe check failed.' }
.\.venv\Scripts\python.exe verify.py
if ($LASTEXITCODE -ne 0) { throw 'Verification failed; do not start.' }

git branch --show-current
git rev-parse HEAD
.\.venv\Scripts\python.exe -m vision --host 0.0.0.0 --port 5004
```

Open http://localhost:5004. For LAN browser access, allow this Python service through Windows Firewall on the intended private network when prompted. The server must also be able to reach receiver/stream-source networks.

Use Device Manager's **Ports (COM & LPT)** section to identify the DART adapter's actual COM port; configure that port (for example `COM7`) in the app.

## 7. Start, stop, and check health

### Normal start

Run from the checkout directory:

| OS | Command |
|---|---|
| Linux/macOS | `bash start.sh --host 0.0.0.0 --port 5004` |
| Windows PowerShell | `powershell -NoProfile -ExecutionPolicy Bypass -File .\start.ps1 -Port 5004 -BindAddress 0.0.0.0` |
| Windows CMD | `start.cmd` |

The launchers install runtime dependencies only if the virtualenv does not exist. They do **not** update an existing virtualenv; use section 13 after pulling changes.

To bind only to the server itself, use `127.0.0.1`. If 5004 is occupied, identify the process or choose another port (for example `--port 5005`). Do not stop an unrelated service just to free the port.

### Normal stop

Press **Ctrl+C** once in the service terminal and wait for the shell prompt. Playback is interrupted for clients; the registry remains saved. Normal shutdown cleans up the FFmpeg children owned by Vision. A forced process/OS kill may leave workers behind, so prefer normal shutdown.

The foreground service normally stops when its terminal/session ends. This MOP does not install a background service, Windows scheduled task, macOS launch agent, or systemd unit.

### Health and GUI checks

From another server terminal:

```bash
curl -fsS http://127.0.0.1:5004/api/health
curl -fsS http://127.0.0.1:5004/api/vision/state
```

Windows PowerShell equivalents:

```powershell
Invoke-RestMethod http://127.0.0.1:5004/api/health
Invoke-RestMethod http://127.0.0.1:5004/api/vision/state
```

Health must respond successfully. The state response reports `ffmpeg`, `ffprobe`, registry counts, and playback/discovery state. Treat state dumps as lab inventory; keep them within the intended audience.

In the browser, confirm all three tabs, **Import config…**, and **FFmpeg ready** if using video. An FFmpeg-unavailable message does not disable remote control. A new installation has zero assets until you import or create them. Health alone does not prove SGS/RF delivery or source reachability.

## 8. Import existing configuration

### GUI import — preferred for normal operators

1. Open **Settops & remotes**.
2. Click **Import config…** beside **+ Settop**.
3. Use the native file picker to select your JAMboreeLite `base.txt` or a JSON configuration.
4. Review the counts: settops to add/skip, remotes to add, streams to add/skip, and omitted credential fields.
5. Select **Import configuration**, or **Cancel** to leave the registry unchanged.
6. Inspect the populated tables. Choose stream links for the imported remotes.

The picker selects files on the **browser computer**. If your file exists only on the 3080 but the browser is on your laptop, copy the configuration to the laptop first or use the CLI from the server.

Accepted encoding/size: UTF-8 JSON (BOM tolerated by the GUI), up to **1 MiB** in the browser picker, with at most 2,000 input settops/streams. A `.txt` extension does not change the requirement for valid JSON.

Supported shapes:

```json
{
  "stbs": {
    "Bench Hopper": {
      "stb": "R0000000000-00",
      "ip": "192.0.2.10",
      "protocol": "SGS",
      "remote": "1",
      "role": "hopper",
      "model": "Hopper"
    }
  },
  "streams": [
    {"name": "Bench video", "url": "rtmp://192.0.2.20:1935/live0", "mode": "copy"}
  ]
}
```

Use real receiver IDs and addresses; `192.0.2.*` values are documentation examples. A bare alias-to-settop object is also supported, as is a JSON object containing only `streams`.

Import behavior:

| Situation | Result |
|---|---|
| New settop alias | Add row and create a remote if needed |
| Existing alias, including a casing variant | Preserve the existing row; do not overwrite its IP/pairing/topology |
| Existing stream URL | Skip duplicate source |
| New stream URL with an already-used stream name | Report a conflict before applying the import |
| Existing remote or stream link | Preserve it |
| New settop without a remote | Create a remote; leave stream association for the operator |
| Invalid JSON or invalid asset | Display an error; validation finishes before import writes begin |
| Reimport same file | Skip existing assets; avoid duplicate remotes |
| Credential fields in new settop entries | Omit recognized top-level secret fields in the GUI route |

The importer is additive, not a full replace/restore operation. It does not restore a remote/link table from JSON. It does not execute Python, PowerShell, shell scripts, or arbitrary global settings. Certificates/private keys are not imported through the file picker. Use the OS keyring, pair again, or use the CLI certificate option for an existing local install.

Preview is recalculated on apply because the registry can change while the dialog is open. The settop JSON file and SQLite registry are separate persistence stores; an I/O failure can leave a partially completed import. Correct the storage error and reimport the same file to finish additively.

### CLI import — server-side files and legacy certificates

Stop Vision first. From the checkout on Linux/macOS:

```bash
.venv/bin/python -m vision.migrate \
  --legacy-base "/actual/path/to/JAMboreeLite/base.txt" \
  --legacy-certs "/actual/path/to/JAMboreeLite/jamboree" \
  --sling-base "rtmp://10.79.86.118:1935"
```

Windows PowerShell:

```powershell
.\.venv\Scripts\python.exe -m vision.migrate `
  --legacy-base "C:\actual\path\to\JAMboreeLite\base.txt" `
  --legacy-certs "C:\actual\path\to\JAMboreeLite\jamboree" `
  --sling-base "rtmp://10.79.86.118:1935"
```

Omit flags you do not need. `--sling-base` registers `live0`–`live8`; `--first 0 --last 3` narrows the range. The address above is from the original Sling-viewer launcher, not a guarantee that the current network has that source. Registration does not probe or start playback. Select the remote links afterward.

`--legacy-certs` copies missing local `cert.pem` and `key.pem` files into Vision's data directory. Never commit these files to Git. The CLI retains legacy settop fields; it differs from the GUI's credential-field filtering. Plaintext credential usage remains disabled by default. Existing keyring credentials use the original alias and OS account. Moving to a different host/account commonly requires re-pairing.

## 9. Configure settops and control

### Add or edit a settop

Open **Settops & remotes → + Settop**. Enter the following, then **Save**:

| Field | Meaning / example |
|---|---|
| Settop alias | Stable unique identity, e.g. `Bench-Hopper-01`; cannot be renamed in the editor after creation |
| Receiver ID | Actual settop RxID; use the target Joey's own RxID for a Joey |
| IP address | Current device address; SGS sends to the configured host for a Joey |
| Verified MAC | Optional known MAC; leave blank if unknown rather than guessing |
| Transport | `SGS` or `RF / DART` |
| DART channel | Hardware remote channel **1–16**, not the remote's display name |
| Serial port | Server-side `COM7`, `/dev/ttyACM0`, or `/dev/cu.*` path |
| Model | Descriptive model used by topology handling |
| Role | Hopper/standalone or Joey/client |
| Host Hopper | Required existing host selection for a Joey |

For a Joey, save the host Hopper first. Then create the Joey with its own receiver ID, role **Joey**, and correct host Hopper. Do not model independent Hoppers as Joeys simply because they share a subnet.

**Edit** changes the saved fields. **Status** reports SGS configuration/pairing and RF/serial readiness. A configured transport is not necessarily reachable; check an actual command before accepting it.

### Manual SGS pairing

1. Save the receiver and topology first.
2. Click **Pair** for that settop.
3. Wait for the PIN screen on the intended receiver.
4. Read the PIN from its TV or known linked video.
5. Enter the PIN in the dialog and select **Complete pairing**.
6. Check **Status**, then send **Guide** from the remote console.

Joey requests use the retained host-Hopper pairing/routing logic. Pairing persistence requires the account running Vision to have a usable keyring/secure fallback. A visible PIN screen does not prove successful credential persistence; complete and test the process.

On headless Linux, an unavailable Secret Service/keyring can cause persistence failure. Resolve the OS keyring or run under the desktop account with a working keyring. Windows uses its normal keyring and, when needed, its retained machine-protected DPAPI fallback. Credentials are not displayed in the inventory.

### RF / DART

Connect supported DART hardware to the **server**. Save the correct serial port and channel. Ensure another JAMboreeLite instance or serial terminal is not holding that same adapter. Use **Status** to inspect readiness, then select **RF only** in the remote console and test **Guide** on the intended receiver.

A serial write/flush confirms submission to the adapter, not that the receiver acted on the RF command. Observe the actual video/TV.

### IP/MAC recovery

**Recover IP** schedules the retained receiver-recovery logic. Review status and the resulting receiver identity/address before accepting the repair. It can be asynchronous or already running/cooling down; do not repeatedly queue scans. In the current GUI, use **Refresh inventory** after recovery completes to retrieve updated fields.

Normal SGS failures can trigger background recovery and RF fallback when applicable. MAC learning occurs after verified SGS success. Forced **SGS only** and **RF only** are strict diagnostic choices and do not silently select another transport.

### Delete a settop

Reassign or delete its remotes first. Reassign any child Joeys before deleting their host Hopper. The API rejects deletion of referenced settops. Deletion removes the configuration row; it is not a physical factory reset or proof of unpairing.

## 10. Discover and manage streams

### Add a known stream

Open **Streams & discovery → + Add stream**. Enter a unique name, full source URL, and playback mode. Save, then **Probe** to verify a readable video track.

| Mode | Use |
|---|---|
| Transcode to H.264 | More CPU; converts source video for browser compatibility; good first troubleshooting choice |
| Copy video | Lower CPU; use for compatible H.264 sources with regular keyframes |

Audio is converted to AAC. Conversion cannot recover an unavailable source, missing credentials, incompatible source transport, or broken network route.

Supported source schemes are HTTP(S), RTMP(S), RTSP(S), and UDP. Embedded `user:password@host` URLs are rejected; use an appropriate upstream relay for an authenticated source. URL query values may contain sensitive source tokens, so treat saved/exported inventory accordingly.

### Discover by host or subnet

1. Enter **Host or IPv4 CIDR**.
2. Enter the stream **URL template** using `{host}` and, when needed, `{n}`.
3. Set first/last index for templates containing `{n}`.
4. Select **Find video streams**.
5. Watch checked/found counts. Select **Cancel scan** if needed; in-flight probes may take a few seconds to finish.
6. For each found source, select **Add stream**, review its name/URL/mode, and save.

Examples:

| Search | Target | Template | Indices |
|---|---|---|---|
| Original Sling source pattern | `10.79.86.118` | `rtmp://{host}:1935/live{n}` | 0–8 |
| Existing external HLS relay | `10.79.85.47` | `http://{host}:8088/live{n}/index.m3u8` | 0–8 |
| Known RTSP endpoint | `192.0.2.20` | `rtsp://{host}:554/path/to/video` | Ignored without `{n}` |
| One HLS path across a subnet | `192.168.1.0/24` | `http://{host}:8088/live0/index.m3u8` | Ignored without `{n}` |

Replace example values with sources on your network. The HLS example requires an already-running relay on 8088; Vision itself serves its own generated video on **5004** and does not start the old relay.

Discovery is bounded to at most **256 URLs**, an IPv4 `/24` or smaller, and eight concurrent probes. A `/24` combined with nine indices exceeds the URL limit; narrow the hosts or indices. One scan runs at a time. Each probe has a timeout; a large unsuccessful scan can take several minutes.

Discovery tests known paths for video tracks. It does not discover arbitrary application paths, enumerate every network video protocol, or prove which receiver appears in a video. A saved stream is not automatically a verified/online stream. Intentionally link each discovered source to the correct remote after watching it.

### Saved-stream actions

| Action | Effect |
|---|---|
| Watch | Opens that stream in the remote console |
| Probe | Checks for a readable video track now and reports codec/resolution on success |
| Edit | Changes name, source URL, or mode while retaining its stable ID and links |
| Delete | Removes the stream and clears all remote associations to it after confirmation |

Changing a saved source stops the old worker. Existing clients can reconnect on retry/lease renewal; use **Retry** or close/reopen the window if needed. Another already-open browser must refresh its inventory to retrieve edited names/settings.

## 11. Link remotes and watch video

### Create or edit a remote

**A stream is optional.** For control without video, choose **No stream — remote control only** under **Linked stream (optional)**. You can create remotes and send SGS/DART commands with zero saved streams and without FFmpeg installed. Only **Watch linked stream** stays disabled for an unlinked remote. Add a stream later, or clear a link using the same dropdown, without changing remote identity or disabling its controls.

Open **Settops & remotes → + Remote**. Enter a display name, choose an existing settop, and optionally choose a stream. Select **Save**. **Edit** can rename the remote, change its target settop, or change its stream association. **Delete** removes the remote, not the physical settop or video source.

The **Linked stream** dropdown in the remotes table saves the association immediately. It is also editable through **Linked video stream** in the active remote console.

A remote `Bench Remote` linked to a source exposes the reference **`Bench Remote-stream`**. Renaming it changes that reference; the link itself persists through UUIDs. Several remotes may reference the same source.

### Remote console

Every configured settop appears as a control target even without a saved remote profile. Use **Control** on its row under **Settops & remotes** to select it and open the console. The saved SGS/RF transport is sufficient; stream availability is never a control prerequisite. Named remote profiles remain available for custom names and optional stream associations. Choosing a stream for a direct settop target creates a saved profile only when needed. A hung/offline linked video affects its player window, not the receiver buttons; you may retain the association for later.

1. Find a remote in the searchable list and select it.
2. Verify the active remote name and settop alias.
3. Select a linked stream if needed; select **Watch linked stream** to open it.
4. Use the **Available streams** checkboxes to open additional independent video windows.
5. Choose the desired transport and press duration.
6. Press a control and observe the resulting action in the intended video window.

Changing a remote's linked stream does not automatically close an already selected video. Stream **association** and selected **viewing windows** are separate. This lets you monitor additional devices while controlling one receiver.

### Buttons and transport choices

The GUI contains Power, Home, Guide, DVR, directional arrows, OK, Back, Options, Info, playback/skip keys, volume, mute, channel up/down, Recall, digits, Record, and PiP. Button behavior uses the retained receiver/transport mappings; actual image/model support can differ.

| Transport selection | Behavior |
|---|---|
| Configured + fallback | Use the settop's saved transport; SGS may recover/fall back to RF when available |
| SGS only | Force SGS without RF fallback/recovery hiding the diagnostic outcome |
| RF only | Force the DART/RF path |

Press duration choices are **120 ms**, **1.2 s**, and **3 s**. Select a duration and click; the current UI sends that duration on a click rather than measuring how long you hold the mouse. Long-press mappings include receiver-specific behavior (for example Home can map to System Info). Use short taps for normal initial acceptance.

The result appears under the remote controls, e.g. `Sent via sgs` or `rf_fallback`. Commands are serialized while one is running. A fallback result means the configured SGS path did not deliver successfully; use Status/SGS-only testing to diagnose it.

### Video windows

Video starts muted. Use the native player controls to play/pause locally, unmute, adjust audio, or enter fullscreen. These player controls operate the browser player; remote buttons operate the receiver. Pausing the player does not pause the receiver.

Use **Retry** for a failed window. Use **×** or uncheck its source to close it. The label shows the linked remote reference(s), or the source name when unlinked. One stream can show multiple remote references when shared.

Vision starts workers for selected streams, up to **16** active source workers. Multiple browsers share one worker per source. A browser renews its lease every 20 seconds; an abandoned lease expires after 65 seconds, with periodic cleanup. Closing the last viewer normally stops that source's worker. HLS introduces several seconds of latency; rapid sequences may appear later on video.

Video selections are remembered per browser/origin in localStorage; opening the app later can restart those selections. Asset links persist on the server independently. Refreshing the inventory updates names and assets; it is not a guaranteed restart of an unchanged playback worker.

## 12. Acceptance checklist

Record server, branch, full commit SHA, timestamp, operator, and results. Check all applicable items:

- [ ] `git branch --show-current` is `montjac`; expected commit is recorded.
- [ ] Dependency installation and `verify.py` complete successfully; skips are reviewed.
- [ ] `ffmpeg` and `ffprobe` resolve in the same environment that launches Vision.
- [ ] Health/state endpoints respond; browser shows Import config and FFmpeg ready.
- [ ] GUI import preview makes no changes; Cancel leaves registry unchanged.
- [ ] Imported assets appear once; existing source links/IPs remain intact on repeat import.
- [ ] SGS pairing completes and credentials persist, if SGS is required.
- [ ] RF-only sends to the intended settop, if DART is required.
- [ ] Guide/arrows/OK/Back/Play-Pause produce expected real-device actions.
- [ ] The linked source depicts the same physical receiver being controlled.
- [ ] Multiple selected sources open and play; close/reopen and Retry work.
- [ ] Remote rename preserves its link and updates its `name-stream` reference.
- [ ] Two browser viewers share a source; closing one leaves the other playing.
- [ ] Restart preserves settops, remotes, sources, and links.
- [ ] Recovery behavior is checked against a known receiver if that feature is needed.

The development baseline had 103 Python tests passing before the cache-header addition; the cache update passed its 10 focused API tests. DOM integration covered asset creation and GUI import preview/cancel/apply/repeat/invalid JSON. A real loopback FFmpeg/HLS pipeline passed. Full browser execution was blocked by development-container socket restrictions; actual hardware/network and OS-specific acceptance remain deployment checks. Test counts can change in later revisions; record the result from the version you deploy.

## 13. Update an installation

### Change window and preconditions

Updating requires stopping this Vision instance, interrupting its remote/video users. Stop it normally with Ctrl+C. Leave other services and unrelated FFmpeg processes alone. Close competing configuration writers while taking the backup.

Confirm a clean checkout before changing code. The snippets stop if tracked or untracked files are present. Review/preserve local work instead of deleting it to satisfy this check. The deployment branch remains `montjac`.

The backup examples assume default data under `~/.jamboreevision`. If you use `--data-dir`, `JAMBOREE_VISION_DATA`, `JAMBOREE_BASE`, custom certificate paths, or a custom credential file, back up the actual locations and record those settings. OS-keyring/DPAPI credentials are not comprehensively captured by a data-directory copy.

### Linux / macOS update

Run after stopping Vision:

```bash
bash <<'JV_UPDATE'
set -Eeuo pipefail
cd "$HOME/JAMboreeVision"
if [[ -n "$(git status --porcelain)" ]]; then
  printf '%s\n' 'Checkout has local changes. Preserve/review them before updating.' >&2
  exit 1
fi
git switch montjac
jv_backup="$HOME/jamboreevision-backups/update-$(date +%Y%m%d-%H%M%S)"
mkdir -p "$jv_backup"
git rev-parse HEAD > "$jv_backup/previous-commit.txt"
.venv/bin/python -m pip freeze > "$jv_backup/requirements-before.txt"
if [[ -d "$HOME/.jamboreevision" ]]; then
  cp -a "$HOME/.jamboreevision" "$jv_backup/data"
fi
printf 'BACKUP=%s\n' "$jv_backup"
git pull --ff-only origin montjac
.venv/bin/python -m pip install -r requirements-dev.txt -r requirements-ocr.txt
.venv/bin/python verify.py
git rev-parse HEAD
printf '%s\n' 'Update verified. Start Vision with the command below.'
JV_UPDATE
```

Only after successful completion:

```bash
cd ~/JAMboreeVision
.venv/bin/python -m vision --host 0.0.0.0 --port 5004
```

### Windows PowerShell update

Run after stopping Vision, from the checkout:

```powershell
$ErrorActionPreference = 'Stop'
$jvChanges = git status --porcelain
if ($LASTEXITCODE -ne 0) { throw 'Not a usable Git checkout.' }
if ($jvChanges) { throw 'Preserve/review local changes before updating.' }
git switch montjac
if ($LASTEXITCODE -ne 0) { throw 'Could not select montjac.' }

$jvBackup = Join-Path $HOME ('jamboreevision-backups\update-' + (Get-Date -Format 'yyyyMMdd-HHmmss'))
New-Item -ItemType Directory -Path $jvBackup | Out-Null
$jvOld = git rev-parse HEAD
if ($LASTEXITCODE -ne 0) { throw 'Could not record current commit.' }
$jvOld | Set-Content (Join-Path $jvBackup 'previous-commit.txt')
$jvFreeze = & .\.venv\Scripts\python.exe -m pip freeze
if ($LASTEXITCODE -ne 0) { throw 'Could not capture dependencies.' }
$jvFreeze | Set-Content (Join-Path $jvBackup 'requirements-before.txt')
$jvData = Join-Path $HOME '.jamboreevision'
if (Test-Path $jvData) { Copy-Item -Recurse -LiteralPath $jvData -Destination (Join-Path $jvBackup 'data') }
Write-Host "BACKUP=$jvBackup"

git pull --ff-only origin montjac
if ($LASTEXITCODE -ne 0) { throw 'Pull failed; do not continue.' }
.\.venv\Scripts\python.exe -m pip install -r requirements-dev.txt -r requirements-ocr.txt
if ($LASTEXITCODE -ne 0) { throw 'Dependency installation failed.' }
.\.venv\Scripts\python.exe verify.py
if ($LASTEXITCODE -ne 0) { throw 'Verification failed; do not start.' }
git rev-parse HEAD
.\.venv\Scripts\python.exe -m vision --host 0.0.0.0 --port 5004
```

After startup, hard-refresh the browser (Ctrl+Shift+R on Windows/Linux; Cmd+Shift+R on macOS), check the Import button and FFmpeg status, and run the applicable section-12 acceptance checks. Compare asset counts/links to the pre-change state. Do not accept an update solely because the service listens on its port.

## 14. Rollback and restore

Stop Vision first. Retain the update backup path printed in section 13 and the failed candidate's commit. Choose code rollback for a code/dependency regression; restore data only if data itself needs to return to the earlier snapshot. Restoring data discards asset edits made after the snapshot, so preserve the current directory before restoring it.

### Code rollback while keeping montjac

A rollback can create a **local revert commit on montjac**, preserving history without force-pushing or resetting the branch. It will cause local/remote history to diverge, so resolve the rollback with the maintainer before the next update. Do not blindly run another fast-forward pull and expect the failed update to be removed.

Linux/macOS example for the linear commits in this repository:

```bash
bash <<'JV_CODE_ROLLBACK'
set -Eeuo pipefail
cd "$HOME/JAMboreeVision"
jv_backup="/actual/path/printed/as/BACKUP"
[[ -z "$(git status --porcelain)" ]] || { printf '%s\n' 'Preserve local changes first.' >&2; exit 1; }
[[ "$(git branch --show-current)" == montjac ]]
jv_old=$(tr -d '\r\n' < "$jv_backup/previous-commit.txt")
git merge-base --is-ancestor "$jv_old" HEAD
[[ "$(git rev-list --count "$jv_old..HEAD")" -gt 0 ]]
[[ "$(git rev-list --merges --count "$jv_old..HEAD")" -eq 0 ]]
git revert --no-commit "$jv_old..HEAD"
git commit -m "Rollback Vision to previously accepted application behavior"
.venv/bin/python -m pip install -r "$jv_backup/requirements-before.txt"
.venv/bin/python verify.py
JV_CODE_ROLLBACK
```

If Git reports conflicts, stop and inspect them; `git revert --abort` cancels an in-progress revert sequence. Merge-containing history needs a reviewed rollback rather than the simple range above. A commit needs your normal Git name/email configuration. Do not publish the local rollback automatically.

Windows uses the same Git operations from the checkout, with the actual backup path:

```powershell
$jvBackup = 'C:\actual\backup\directory'
$jvOld = (Get-Content (Join-Path $jvBackup 'previous-commit.txt') -Raw).Trim()
git status --short
git branch --show-current
git log --oneline "$jvOld..HEAD"
```

Confirm a clean checkout, `montjac`, and a linear range before continuing:

```powershell
git revert --no-commit "$jvOld..HEAD"
if ($LASTEXITCODE -ne 0) { throw 'Revert needs review; stop here.' }
git commit -m 'Rollback Vision to previously accepted application behavior'
if ($LASTEXITCODE -ne 0) { throw 'Rollback commit failed.' }
.\.venv\Scripts\python.exe -m pip install -r (Join-Path $jvBackup 'requirements-before.txt')
if ($LASTEXITCODE -ne 0) { throw 'Dependency restore failed.' }
.\.venv\Scripts\python.exe verify.py
if ($LASTEXITCODE -ne 0) { throw 'Verification failed.' }
```

### Data restore

With the service stopped, rename the current data directory and copy the snapshot back. Do not overlay SQLite/WAL files onto a running instance.

Linux/macOS:

```bash
bash <<'JV_DATA_RESTORE'
set -Eeuo pipefail
jv_backup="/actual/path/printed/as/BACKUP"
[[ -d "$jv_backup/data" ]]
if [[ -d "$HOME/.jamboreevision" ]]; then
  mv "$HOME/.jamboreevision" "$HOME/.jamboreevision-before-restore-$(date +%Y%m%d-%H%M%S)"
fi
cp -a "$jv_backup/data" "$HOME/.jamboreevision"
JV_DATA_RESTORE
```

Windows:

```powershell
$ErrorActionPreference = 'Stop'
$jvBackup = 'C:\actual\backup\directory'
$jvData = Join-Path $HOME '.jamboreevision'
if (-not (Test-Path (Join-Path $jvBackup 'data'))) { throw 'Data snapshot missing.' }
if (Test-Path $jvData) {
  Move-Item -LiteralPath $jvData -Destination ($jvData + '-before-restore-' + (Get-Date -Format 'yyyyMMdd-HHmmss'))
}
Copy-Item -Recurse -LiteralPath (Join-Path $jvBackup 'data') -Destination $jvData
```

A restored data folder does not restore OS-keyring changes. Recheck pairing and receiver identity after restoring. Start the accepted application and repeat the applicable acceptance checks.

## 15. Troubleshooting

| Symptom | Check / action |
|---|---|
| Import button missing | Confirm the exact server URL, hard-refresh, and compare browser HTML with the server response below |
| FFmpeg unavailable | Run `ffmpeg -version` and `ffprobe -version` in the same terminal used to launch Vision; correct PATH/install, then restart |
| GUI opens but has zero assets | Expected on first run; import/create assets. If unexpected, verify the launching account and actual data/base paths |
| Pair displays PIN but completion fails | Confirm target receiver/topology, entered PIN, account keyring, and server reachability |
| SGS fails but RF works | Test SGS-only, inspect Status, receiver IP/ID, host Hopper, and credential persistence |
| Wrong receiver changes | Stop sending commands; verify selected remote, settop target, Joey host, RF channel, and video association |
| RF unavailable | Check physical adapter, correct server-side port/channel, driver, permissions, and competing serial applications |
| Discovery finds nothing | Verify FFprobe, route/source host, complete application path, source availability, indices, and protocol; an open TCP port alone is insufficient |
| Too many discovery candidates | Narrow host CIDR or index range to stay within 256 URLs |
| Video waits/retries | Probe source; check FFmpeg availability; try Transcode; check upstream health and server CPU. A local media 404 can occur while the first segments are being generated |
| High CPU | Reduce simultaneous streams or use Copy for compatible sources |
| Video is delayed | HLS buffering/conversion adds latency; compare remote-command acknowledgment with observed video rather than expecting frame-synchronous control |
| Address already in use | Inspect the listener or choose a different port; do not kill unrelated services |
| Data-directory lock held | Another server/importer may be using that directory. Stop the known owner normally; the presence of a lock filename alone does not prove a live lock |
| Cross-origin writes rejected | Use the app's same-origin page/API rather than embedding controls in another site |
| Import reports name conflict | Rename the source in the input or edit the existing asset; different URLs cannot share a saved stream name |
| Update refuses fast-forward | Review local changes/branch divergence; preserve work rather than forcing/resetting |

### Read-only diagnostics

Linux/macOS, on the Vision server:

```bash
cd ~/JAMboreeVision
git branch --show-current
git rev-parse HEAD
.venv/bin/python --version
command -v ffmpeg ffprobe
curl -fsS http://127.0.0.1:5004/api/health
curl -fsS http://127.0.0.1:5004/ | grep -o 'id="import-config"'
```

From the browser console, this compares the current document with a fresh server response:

```javascript
(async () => {
  const button = document.getElementById('import-config');
  const html = await fetch('/?check=' + Date.now(), {cache: 'no-store'}).then(r => r.text());
  console.log({
    browserURL: location.href,
    buttonInPage: Boolean(button),
    buttonVisible: Boolean(button?.getClientRects().length),
    buttonInFreshResponse: html.includes('id="import-config"')
  });
})();
```

If the server has the button but the browser's fresh response does not, first check host/port/proxy routing. If the fresh response has it but the page does not, reload the document. If the element exists but is invisible, inspect browser styling/extensions. Do not diagnose all three cases as caching without evidence.

## 16. Advanced operations

### Custom listener / storage

Linux/macOS:

```bash
.venv/bin/python -m vision --host 0.0.0.0 --port 5005 --data-dir "$HOME/vision-lab2"
```

Windows:

```powershell
.\.venv\Scripts\python.exe -m vision --host 0.0.0.0 --port 5005 --data-dir "$HOME\vision-lab2"
```

Use a consistent launch command and account. If `JAMBOREE_BASE` is already set, it overrides the default settop-file location independently of the Vision data directory. Avoid unintentionally sharing a base file or serial adapter between instances. `JAMBOREE_VISION_DATA`, `JAMBOREE_VISION_HOST`, and `JAMBOREE_VISION_PORT` provide environment defaults.

### Resolve a remote-stream reference

For the example name `Bench Remote`:

```bash
curl -fsS 'http://127.0.0.1:5004/api/vision/resolve/Bench%20Remote-stream'
```

This returns the saved linked source definition, not an automatically started browser-playable stream. Normal GUI viewing acquires a managed playback session separately.

### Recovery / pairing status

These reads do not request a receiver button press:

```bash
curl -fsS 'http://127.0.0.1:5004/api/ip_recovery/status'
curl -fsS 'http://127.0.0.1:5004/api/sgs/pair/status'
curl -fsS 'http://127.0.0.1:5004/sgs/credentials/status?alias=Bench%20Hopper'
```

The inherited pairing/recovery API includes actions beyond the GUI. Requests that run recovery, start pairing, verify commands, or send remote keys can affect the receiver. Use POST with JSON. Legacy state-changing GET remote routes are disabled in Vision.

### Optional OCR pairing

The GUI's supported pairing flow is manual PIN entry. The inherited API also supports OCR automatic pairing with configured external frame/status endpoints. A stream association alone does not configure that frame source.

Prerequisites: OCR Python dependencies, a Tesseract executable, a working JPEG snapshot endpoint, and correct receiver selection at the frame source. Environment settings include `JAMBOREE_FRAME_URL`, optional `JAMBOREE_FRAME_STATUS_URL`, and `JAMBOREE_FRAME_TIMEOUT_S`. Vision defaults automatic background pairing to off; deliberately configure `JAMBOREE_AUTOPAIR` only when those prerequisites are ready. This advanced feature is not needed for normal manual pairing or stream viewing.

### Optional DOM/browser validation

With Node.js installed, from the checkout:

```bash
npm install
npx playwright install chromium
JV_PYTHON="$PWD/.venv/bin/python" npm run test:dom
JV_PYTHON="$PWD/.venv/bin/python" npm run test:browser
```

Windows PowerShell:

```powershell
npm install
npx playwright install chromium
$env:JV_PYTHON = (Resolve-Path .\.venv\Scripts\python.exe).Path
npm run test:dom
npm run test:browser
```

The fixture uses loopback test-pattern video and a test-only controller replacement; it sends no physical remote commands. Browser screenshots are written into the OS temporary directory. The fixture is not a production launcher.

## 17. Persistence and limits

| Item | Location / behavior |
|---|---|
| Settop transport configuration | `base.txt` under default data directory, unless `JAMBOREE_BASE` overrides it |
| Remotes, streams, links, discovery settings | `assets.sqlite3`, with SQLite WAL/foreign keys |
| Legacy settop backup | `base.txt.bak` as maintained by retained atomic configuration writes |
| Optional local certificate/key | Data directory when copied by CLI; environment paths can override |
| Credentials | OS keyring / retained Windows DPAPI path; not comprehensively included in an ordinary data-folder backup |
| Video selections | Browser localStorage, per origin |
| Video segments | Managed generation-specific subdirectories under `media/`; temporary playback data |
| Server process lock | `server.lock` under data directory |
| Imported associations | Remote/settop records created; stream links chosen explicitly |

Do not edit or restore a live SQLite file by overwriting it. Take backups with the service stopped, copy the full data directory, and record custom paths. Repository updates normally preserve user data because it lives outside the checkout. Newer releases can change schemas; restore a matching pre-change snapshot if a later code rollback requires the older schema.

This release has no GUI full-registry export/restore, user accounts, service installer, automatic arbitrary-path stream enumeration, or automatic proof that a stream depicts a receiver. The MOP describes current implemented behavior, including these operational boundaries.


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
