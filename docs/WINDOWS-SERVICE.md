# Shared Windows service deployment

One Windows host runs JAMboreeVision; department users open
`http://HOSTNAME:5004` in their browsers. They do not install Python, FFmpeg,
Git, or this app on their own computers. The host remains powered on, awake,
network-connected, and attached to the DART USB hardware. Signing out of Windows
does not stop the service. All users share assets, configuration, video workers,
and command history. Console commands serialize; competing requests may receive
“another remote command is still running.”

This procedure supports a Git checkout or an extracted **montjac** ZIP.
It does not require Git on the service host. Application releases, shared data,
and logs are stored separately under `C:\ProgramData\JAMboreeVision`.
The Windows service runs as **LocalService**, starts automatically after boot,
and restarts following a process failure. WinSW wraps the existing Waitress server.

## 1. Prepare the host once

Use a stable lab workstation or approved Windows server with a stable hostname
and DHCP reservation/static address. Prevent sleep while it is serving the lab.
Confirm DART drivers are installed and identify the known working **COM port AND
DART channel**; they are independent settings. For Steve's HPR2-2, verify the
old application's COM3/channel 2 entry rather than guessing from the video URL.

Install Python **3.11 or 3.12, 64-bit, for all users**, for example
`C:\Program Files\Python312\python.exe`. A Python interpreter beneath
`C:\Users\...` is intentionally rejected: the service must not depend on a
user's profile or interactive PATH. The installer currently accepts 3.10–3.12;
use 3.11/3.12 for the documented deployment.

Extract FFmpeg into a host-wide directory, e.g.
`C:\ffmpeg\ffmpeg-master-latest-win64-gpl\bin`, containing both `ffmpeg.exe`
and `ffprobe.exe`. LocalService must have read/execute access to Python and FFmpeg
and access to the physical serial device. The service supplies its own PATH;
**do not use setx**.

Obtain the **WinSW v2.12.0 x64** executable from the official release:
https://github.com/winsw/winsw/releases/tag/v2.12.0
Download `WinSW-x64.exe`, not x86 or a v3 prerelease, to a staging folder.
Record its SHA-256 before installation; the installer checks that the supplied
file still matches. Download only from the official release above. Calculating a
hash is an identity check, not independent proof of origin:

```powershell
Get-FileHash -Algorithm SHA256 'C:\Install\WinSW-x64.exe'
```

Download/extract the montjac ZIP from:
https://github.com/askjake/JAMboreeVision/archive/refs/heads/montjac.zip
The selected source folder must directly contain `vision`, `jamboree`,
`requirements.txt`, and `deploy\windows\Manage-Service.ps1`.

Stop Steve's interactive Vision process with **Ctrl+C** and stop the other
JAMboree process using the same DART ports. Closing a browser tab does not stop
a server. Keep a copy of the working application's configuration. The service
installer refuses to install if port 5004 is already listening.

## 2. Install and import Steve's configuration

Run **PowerShell as Administrator**. Adjust the example paths and client subnet.
The subnet is the approved range of browser clients, which may differ from the
video server subnet. The example `192.168.19.0/24` is illustrative.

```powershell
$source = 'C:\Install\JAMboreeVision-montjac'
$winSWHash = (Get-FileHash -Algorithm SHA256 'C:\Install\WinSW-x64.exe').Hash

& "$source\deploy\windows\Manage-Service.ps1" -Action Install -SourceDirectory $source -PythonExe 'C:\Program Files\Python312\python.exe' -FFmpegDirectory 'C:\ffmpeg\ffmpeg-master-latest-win64-gpl\bin' -WinSWExe 'C:\Install\WinSW-x64.exe' -WinSWSha256 $winSWHash -AllowedSubnet '192.168.19.0/24' -ImportDataDirectory 'C:\Users\stgiam\.jamboreevision'
```

For a new empty installation, omit `-ImportDataDirectory`. If Steve used a custom
data directory, supply it explicitly. Import takes source/destination instance
locks and refuses to copy from a running Vision instance or overwrite existing
destination assets. It uses SQLite's backup API (including committed WAL data),
then copies known durable configuration/certificate/DPAPI files. It omits transient
media, locks, and unrelated files. A custom `JAMBOREE_BASE` outside the source data
directory must be migrated separately before first use.

The installer prepares a new virtual environment, installs runtime dependencies,
checks dependency consistency/imports, writes WinSW configuration, restricts file
permissions, registers the service, and creates a named firewall rule limited
to the supplied subnet on **Domain/Private** network profiles. It starts the
service and checks the actual `/api/vision/state` endpoint. It does not open
Public-profile access or modify the machine/user PATH. No local foreground app
may subsequently use the same data directory or serial ports.

Existing SGS credentials in Steve's **Windows Credential Manager are not copied**.
They belong to Steve's account. After migration, pair SGS receivers again through
the running service. The service uses its own account's keyring, with the retained
machine-scoped DPAPI fallback at `data\sgs_credentials.dpapi.json`. Copying an old
user-scoped DPAPI record does not make it readable under LocalService; re-pair on
failure. RF configuration does not require SGS credentials.

If installation fails before registration, files may remain at the chosen root.
Inspect the error first. Preserve/rename that incomplete directory before retrying
with an empty root; do not delete an existing deployment's data. If registration
already succeeded, inspect logs and use the Start/Stop/Uninstall actions as needed.

If corporate execution policy blocks the script, use the approved signed-script
or IT deployment route. The procedure does not change execution policy.

## 3. Verify before inviting the department

Open `http://localhost:5004` on the server. Confirm imported settops/remotes/streams,
then test Guide/Select on the correct physical STB using the known COM port/channel.
The first serial-open/Arduino settle takes about two seconds. Check both terminal
logs and observed device behavior; “serial flushed” is not RF/receiver confirmation.
Probe/watch a known stream. Test SGS pairing if used.

From a second workstation in the allowed subnet, open `http://HOSTNAME:5004`
(replace HOSTNAME with the installation's printed computer name). `localhost` on
a client refers to that client, not the server. Verify commands and video from
that client, then sign out of Windows on the server and repeat from the client.
During an agreed maintenance window, reboot and repeat the checks to qualify
startup, USB/COM enumeration, credentials, and hostname/firewall behavior.

If COM numbers change after reconnect/reboot, edit the host port in the app while
retaining the correct DART channel. Remote users select the **server's** ports.
An empty disconnected-port dropdown is not evidence of a client-side USB issue.

The app currently has **no per-user login or role system**. Anyone who can reach
this service can control and edit its shared assets. The subnet firewall is a
network boundary, not individual authentication. If the department needs named
users/roles or access beyond the lab subnet, place it behind an approved access
proxy or add application authentication before expanding access. WinSW alone
does not add authentication or TLS.

## 4. Service operations and logs

Administrator PowerShell:

```powershell
$manager = 'C:\ProgramData\JAMboreeVision\service\Manage-Service.ps1'
& $manager -Action Status
& $manager -Action Stop
& $manager -Action Start
```

Inspect serial TX/RX, SGS, startup and failure output:

```powershell
Get-Content 'C:\ProgramData\JAMboreeVision\logs\JAMboreeVision.err.log' -Tail 100 -Wait
```

Stdout uses `JAMboreeVision.out.log`; WinSW also records wrapper diagnostics.
Output rolls at approximately 10 MiB with eight retained rolls per output stream.
Configuration lives in `data\base.txt` and `data\assets.sqlite3`; pairing files
and transient media also live under data. Administrator/System retain full
control; LocalService can read the release and modify data/logs. Browser users
need no filesystem permissions. The service executable and configuration are in
`service`; application releases are in `releases`.

Status includes the installation manifest; the WinSW XML `executable` is the
authoritative active-release path after a rollback. The manifest records the last
successful installation/update and can retain that release name after rollback.

## 5. Update from a new ZIP

Extract the new montjac ZIP into a **new staging folder**. Keep the old release
and shared data. Run the new copy of the manager as Administrator:

```powershell
$newSource = 'C:\Install\JAMboreeVision-next'
& "$newSource\deploy\windows\Manage-Service.ps1" -Action Update -SourceDirectory $newSource
```

Update prepares the new release/venv and verifies imports while the current
service continues serving. It then stops the service, makes a stopped-data backup
under `backups`, saves the previous WinSW configuration, switches to the candidate,
restarts, and checks HTTP health. A failure during switching/health attempts to
restart the previous release and reports the failure. Staging/dependency failures
before service stop leave the current service running. The same runtime identity,
FFmpeg path, listener port, shared data and firewall rule are retained. The new
source manager supports the current release format; future releases with explicit
schema-breaking migrations require their own documented data rollback procedure.

After HTTP readiness, repeat the physical Guide and video checks. Dependency and
HTTP checks cannot establish hardware correctness. A maintenance window still
covers the short service interruption and hardware acceptance.

To revert application code to the last saved release:

```powershell
& 'C:\ProgramData\JAMboreeVision\service\Manage-Service.ps1' -Action Rollback
```

Rollback leaves current data intact. To restore a data backup as well, stop the
service, preserve the current data directory, restore the desired backup's durable
files, restore data/temp directories and LocalService modify permissions, and
start the matching application release. Never copy a live SQLite database with
Explorer. Backups may contain pairing material; keep their restricted permissions.
After a new service format/manager change, retain the manager from that release
alongside its installation record.

## 6. Remove the service

```powershell
& 'C:\ProgramData\JAMboreeVision\service\Manage-Service.ps1' -Action Uninstall
```

This stops/removes the service and its named firewall rule. Application releases,
logs, backups and configuration remain for recovery. It does not delete user data
or uninstall Python/FFmpeg.

## Validation boundary

Portable tests cover generated XML, explicit paths/account/environment,
SQLite/WAL migration, refusal to overwrite, and locking against a running instance.
Native Windows SCM, LocalService, firewall, logout/reboot, serial hardware, and
SGS credential behavior require the host acceptance above. This environment
cannot claim those Windows service checks have passed.


## Resume an installation interrupted by the original ACL bug

The first service installer recursively removed inherited permissions while
applying directory inheritance flags. On the affected host, this left files such
as the virtual environment's `python.exe` with empty ACLs. The corrected routine
sets protected permissions on each boundary directory and enables inheritance
on descendants. System/Administrators retain full access; LocalService gets
read/execute on code and modify on data/logs. No Users/Everyone grant is added.

Replace only `deploy\windows\Manage-Service.ps1` in your extracted source with
the corrected montjac version and unblock that downloaded script under
RemoteSigned. For an installation that failed **before service registration**, use:

```powershell
$source = 'C:\Users\invidijobs.CTEST\Documents\JAMboreeVision-montjac'
Unblock-File -LiteralPath "$source\deploy\windows\Manage-Service.ps1"
& "$source\deploy\windows\Manage-Service.ps1" -Action ResumeInstall -SourceDirectory $source -ReleaseDirectory 'C:\ProgramData\JAMboreeVision\releases\20261020-112451-626' -PythonExe 'C:\Program Files\Python311\python.exe' -FFmpegDirectory 'C:\ffmpeg\ffmpeg-master-latest-win64-gpl\bin' -WinSWExe 'C:\Install\WinSW-x64.exe' -WinSWSha256 '05B82D46AD331CC16BDC00DE5C6332C1EF818DF8CEEFCD49C726553209B3A0DA' -AllowedSubnet '172.20.33.0/24'
```

These paths/hash/subnet identify the reported host and downloaded wrapper;
substitute the actual staged release for another installation. ResumeInstall
requires an existing staged venv under this root's releases directory and refuses
an already registered service or existing installation manifest. It repairs
permissions, checks the existing dependencies/imports, generates configuration,
and completes registration/startup. It does not recreate the venv or reinstall
dependencies. If configuration was meant to be imported and has not yet been
copied, include the same `-ImportDataDirectory` option as the original install.
The native file-ACL regression is included in the Windows CI job; host service
acceptance remains required.


### Updating for live-edge playback and DART recovery controls

Download the current `montjac` ZIP, extract it into a new source folder, and
use that source folder's `deploy/windows/Manage-Service.ps1` for Update. This
includes the startup-readiness polling correction. The source folder must be
the one containing `vision` and `requirements.txt`. In elevated PowerShell:

```powershell
$source = 'C:\Install\JAMboreeVision-montjac'
Unblock-File -LiteralPath "$source\deploy\windows\Manage-Service.ps1"
& "$source\deploy\windows\Manage-Service.ps1" -Action Update -SourceDirectory $source
```

The existing installation record supplies Python, FFmpeg and port settings.
Update prepares a release, stops the service, backs up shared configuration,
starts the new release, and checks HTTP readiness. Existing stream/settop/remote
assets remain in the shared data directory. An update interrupts current video
sessions; hard-refresh each browser afterward (Ctrl+F5 on Windows). Verify Go live,
pause/seek preservation, and the DART controls on the intended hardware.
