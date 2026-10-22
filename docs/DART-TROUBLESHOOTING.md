# DART stops responding after an idle period

## What is established

The host was toggling DTR after 30 seconds with no serial TX/RX. A quiet remote
control connection is normal, not evidence of a failed board. DTR may reset an
Arduino MCU, interrupt a command or restart its I2C handshake. That automatic
idle reset has been removed. Actual serial read/write failures still close and
reopen the port; opening a serial connection itself can reset some Arduino boards.
The normal open/bootloader settling delay remains in place.

The host also discarded the input buffer after its bootloader settling delay.
That could discard the startup/version banner. Input is now retained and logged
at INFO, making board identification easier after a connection is opened.

Automated regression tests simulate a prolonged idle period followed by a command,
verify no DTR transitions, preserve a boot banner, and retain existing read-failure
recovery. These tests do not establish the cause of every hardware ACK timeout.

## What the supplied field logs imply

USB/serial is still communicating when a board echoes `Received input: ...`.
`TX flushed` means the host wrote bytes, not that the remote/STB acted on them.
The repeated `Timeout waiting for acknowledgment` lines are board-side reports.
Reset changing those replies is a diagnostic observation, not proof of RF pairing.

The echo/timeout/numeric reply style matches the legacy
[JAMboree DART_Dumb.ino](https://github.com/askjake/JAMboree/blob/6f3b026db89933a23dc8c65b3f54d22d11324419/DART_Dumb.ino).
That source prints `11.05.2024 Jake Montgomery (That Was Good)` at startup.
After its send/wait sequence succeeds it concatenates decimal values without
separators: `218153` corresponds to remote 2, down 181 (0xB5), up 53 (0x35).
This is evidence of that code family's output format, not an independently
verified firmware identity or proof that the STB received RF.

In that legacy source, `initFlag` starts true after an MCU reset. Its I2C receive
handler ignores commands until it receives the command-1 initialization exchange.
Resetting the MCU alone while remote circuits remain running can therefore
leave the devices in mismatched handshake states. This is a plausible mechanism
for the idle symptom and must be checked against the installed firmware.

Other source-review findings to investigate after identification: a 100 ms ACK
limit, a second release transmission, release-all sending only one fixed release
code, and a host write receipt that does not wait for completion of the board's
entire ACK sequence. The timed host hold delay is not a board-completion protocol.
Rapid presses can arrive while legacy firmware is still waiting. Do not solve
this by automatically replaying commands or assuming an arbitrary numeric RX
line acknowledges the most recent request.

## Free the COM ports for Arduino IDE

Do this on the machine physically connected to the DART board. If Vision runs
as a service there, stop it in administrator PowerShell. This pauses shared
remote/video availability on that server while inspecting the hardware:

```powershell
Stop-Service -Name JAMboreeVision
(Get-Service JAMboreeVision).WaitForStatus('Stopped', [TimeSpan]::FromSeconds(35))
Get-Service JAMboreeVision
```

Close any separate legacy JAMboree, Vision console process, or other serial
monitor on that same machine. Do not change settop COM mappings or delete data.
If Arduino IDE still reports the port busy, inspect processes before retrying:

```powershell
Get-CimInstance Win32_Process |
    Where-Object { $_.Name -match '^(pythonw?|JAMboreeVision)\.exe$' } |
    Select-Object ProcessId, Name, ExecutablePath, CommandLine
```

Do not terminate every Python process. SCM Stopped can precede final child-process
cleanup; identify any remaining owner by its actual executable/command line.

## Identify the installed board/firmware

1. Select **COM3** in Arduino IDE for the board from the supplied logs.
2. Open Serial Monitor at **115200 baud**, with **Newline** line ending.
3. Opening the monitor may reboot the MCU. If no banner appears, press the
   Arduino's physical reset button once and copy the complete startup output.
4. Record the model/processor marked on the actual board, the IDE-selected board
   and processor, and the original `.ino` file if it is available. The IDE's open
   sketch is not necessarily what was uploaded previously.
5. If a banner confirms support for a `version`/`help` command, use that documented
   command. The legacy DART_Dumb sketch has no version-query command; its boot
   banner is the available identification output.

Serial Monitor does not recover the original source sketch from flash. A banner
only identifies what the firmware chooses to print. Do not upload a replacement
based on output style alone: pin assignments, reset wiring and SRAM requirements
must match the board. In particular, the newer 16-remote buffered reference has
a 2,048-byte event queue plus other buffers; it is not a safe drop-in for every
2 KB SRAM Arduino variant. Its use of A7 as an output must also match the MCU.

## Reproduce without rapid-command overlap

With Vision stopped and the monitor open, verify that the selected channel is
actually the intended receiver. For the current COM3/channel-2 example, send
**one Guide command** followed by a newline:

```text
2 83 03 120
```

Wait at least two seconds and capture all output. Repeat after 15, 45 and 120
seconds of inactivity. Record whether the board reboots, the remote's response,
and whether the live STB picture changes. Use the player Go live function to
exclude delayed video as an explanation. If the command fails, preserve that
output before trying `2 86 allup` or `2 99 reset`; these have different meanings,
and reset may affect all remote circuits sharing the board's reset line.

Close Serial Monitor before resuming Vision:

```powershell
Start-Service -Name JAMboreeVision
(Get-Service JAMboreeVision).WaitForStatus('Running', [TimeSpan]::FromSeconds(35))
Invoke-RestMethod 'http://127.0.0.1:5004/api/vision/state' -TimeoutSec 15 | Out-Null
```

Opening the connection can itself reset the board, so identify firmware before
interpreting differences between the monitor and Vision. After installing the
host idle-reset fix, repeat the idle test through Vision with a single selected
settop. Watch only new log lines with a local observation timestamp:

```powershell
Get-Content 'C:\ProgramData\JAMboreeVision\logs\JAMboreeVision.err.log' -Tail 0 -Wait |
    ForEach-Object { '{0:o} {1}' -f (Get-Date), $_ }
```

Ctrl+C stops log watching, not the service. Return the boot banner, actual board
model/processor, original sketch if available, and before/after-idle output.
Firmware changes and upload instructions should be based on that evidence.
