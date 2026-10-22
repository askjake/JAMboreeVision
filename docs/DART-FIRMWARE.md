# DART Remote Bridge Firmware

Replacement firmware for the JAMboreeVision DART 16-remote serial-to-I2C
bridge, the verification environment that proves it, and the staged procedure
for a controlled hardware trial.

Everything in this document was produced by running the commands shown. Where a
claim could not be verified it is marked explicitly. **No physical hardware was
flashed, no port was opened, and no running service was restarted.**

---

## 1. Provenance and identity

### 1.1 The original sketch

#### Installed firmware identity: NOT VERIFIED

**No banner was ever read from the attached board during this work.** No serial
port was opened (see section 2.1), so there is no captured startup string, no
timestamp of a capture, and no command that produced one. Any statement about
what is *installed* on the board is therefore unverified.

The only installed-identity datum available is **operator-supplied**: the
captured hardware banner reported to this work as

```
11.05.2024 Jake Montgomery (That Was Good)
```

That string does **not** belong to either copy of
`DART_16Remotes_Buffered_Handshake_TxAck_Timestamps.ino`. Both of those print
`Jacob Montgomery - Spreading the JAM 2025-11-12[a]`. The supplied banner
matches a **different and older sketch family**, the legacy
`askjake/JAMboree DART_Dumb.ino`, which `docs/DART-TROUBLESHOOTING.md` already
records as printing exactly that line at startup. On the evidence available,
the installed firmware is more likely to be from the `DART_Dumb.ino` family
than from the 16-remote sketch.

No newer physical banner was observed. If a newer banner exists it was not
captured here, and nothing in this package should be read as implying one.

#### The two source copies

Both are **host-supplied reference material, installed identity unverified**:

| Copy | SHA-256 | Status |
|---|---|---|
| `DART_16Remotes_Buffered_Handshake_TxAck_Timestamps.ino` as found on the host (CRLF) | `faf8a220d5353c5d952e9e0943da020e78ef6c7a99d744fe00a117fc47ec3e8a` | **Host-supplied reference, installed identity unverified.** Banner `2025-11-12a`. Vendored verbatim as `firmware/test/reference/reference_sketch.ino.txt` (same hash). Used as the defect-reproduction subject. |
| Same file, LF-normalised | `017268ee1b28ca162a20d9331a456fc04734fda5e5c9d7a429a12e334b573dee` | For comparison only. |
| `askjake/JAMboreeLite` git copy (LF) | `4bef150b083d2c69659df1c861b1695455c02dd7e32aa3a6ad47f96b1a68e6f5` | Host-supplied reference. Banner `2025-11-12`. Differs in three lines. |

The three differences (LF-normalised `diff`):

```
122c122   const unsigned long MAX_HELD_MS = 5000;   (host)  vs  3000  (git)
152c152   banner "...2025-11-12a"                   (host)  vs  "...2025-11-12"
370c370   resetRemote(remoteNum, "80");             (host)  vs  resetRemote(remoteNum, 80);
```

**Attribution limits, which the rest of this document observes:**

* The **pointer-as-delay defect exists only in the host CRLF copy.** It is real
  and fully reproduced (section 3.1), and that reproduction stands on its own
  as a source-level finding.
* It is **not attributed to the installed firmware.** Neither the defect nor its
  ~32.3 second shared-reset consequence can be claimed for the deployed board,
  because the installed firmware was never identified.
* Because the supplied banner points at a different sketch family, the installed
  firmware may well not contain this call site at all.
* Determining what is actually installed requires opening the port or reading
  flash, both of which are excluded here and both of which appear as explicit
  steps in the hardware trial procedure (section 9).

### 1.2 The candidate

| File | SHA-256 |
|---|---|
| `firmware/dart_remote_bridge/dart_remote_bridge.ino` | `537ef8dd55ce448d8434719516dba608425152374cf60d8a08404db4d4cd3eca` |
| `firmware/dart_remote_bridge/dart_core.h` | `b9c2ef49f7ea490eabe4aae34852213ad896b11c1b8444153540911d28d49551` |
| `firmware/dart_remote_bridge/dart_core.cpp` | `33dbf8b4faabb824c235b37356feccab996f19207511f542d5353574d0cd5b72` |
| `firmware/dart_remote_bridge/dart_hal.h` | `af2e2caf233075fef907f6663376673a9622a64e4d4060a4eeb29ba2816102c4` |
| `firmware/dart_remote_bridge/dart_board_config.h` | `ed2c78405b80f78ffe1df9e3abf6f5b6656bb2ef6787ee526613f7e29354f612` |
| `firmware/dart_remote_bridge/dart_version.h` | `686aa1dbc2bf64aafc18b569039c5cb29031183c4953e68b195ff92db6c5679e` |

Firmware identity banner: `DART-BRIDGE 2.0.0 2026-10-21`, serial protocol `3`,
I2C protocol `1` (byte-compatible with the legacy 15-byte reply).

### 1.3 Toolchain and harness versions

| Component | Version |
|---|---|
| arduino-cli | 1.4.1 (commit `e39419312`, 2026-01-19) |
| Arduino core | `arduino:megaavr` 1.8.8 (SHA-256 `4ffba95eef2c217dfadbe545a81c898a184e8dc0ad3a963f15ef4f627de608de`) |
| AVR compiler | avr-gcc 7.3.0 (`7.3.0-atmel3.6.1-arduino5`) |
| avrdude | 6.3.0-arduino17 (**not invoked**) |
| Host compiler | g++ (Ubuntu 11.4.0-1ubuntu1~22.04.3) 11.4.0 |
| Emulator | simavr 1.6+dfsg-3 |
| Emulator debugger | avr-gdb 7.8 (Arduino-bundled) |
| Integration runtime | CPython 3.10, pyserial 3.5, Flask 3.1.0 |

### 1.4 Scope of the change

The change is **purely additive**. No existing repository file is modified; no
application Python was changed. The integration test in section 6 confirms the
candidate speaks the wire protocol the application **already** emits.

```
firmware/.gitignore                             (new)
firmware/dart_remote_bridge/*.{ino,h,cpp}       (new, 6 files)
firmware/test/{Makefile,dart_test_harness.h,dart_tests.cpp}        (new)
firmware/test/pty_sim.cpp                                          (new)
firmware/test/vision_virtual_serial_test.py                        (new)
firmware/test/emu/{Makefile,run_emulation.sh,avr_emu_tests.cpp}     (new)
firmware/test/reference/*                                          (new)
docs/DART-FIRMWARE.md                                              (new)
```

---

## 2. The hardware

### 2.1 Identification (read-only metadata only)

Obtained **without opening the port, toggling any control line, stopping any
service, or contacting the bootloader** — purely by reading sysfs attributes:

```
$ readlink -f /sys/class/tty/ttyACM0
/sys/devices/pci0000:40/.../usb7/7-4/7-4:1.0/tty/ttyACM0
$ cat /sys/.../usb7/7-4/{idVendor,idProduct,manufacturer,product,serial}
2341
0058
Arduino LLC
Arduino Nano Every
08E6600A5153433347202020FF0B3B1C
$ cat /sys/.../7-4:1.0/bInterfaceClass   -> 02 (CDC)   driver: cdc_acm
```

`arduino-cli board details -b arduino:megaavr:nona4809` lists identification
properties `vid=0x2341 pid=0x0058`, which match exactly.

**Conclusion:** the attached board is an **Arduino Nano Every**, FQBN
`arduino:megaavr:nona4809`, MCU **ATmega4809**, 48 KiB flash / 6144 B SRAM.

#### Evidence that the port was not opened — and the limits of that evidence

The full command inventory and its scope limits are in `hw-evidence.txt`. In
summary, the only things run against the device or its metadata were: `ls -la`
(stat, no open), sysfs attribute reads, `readlink` on the tty class symlink,
open-file-descriptor snapshots, `arduino-cli board details` (local index only),
and one `avrdude -v` invocation that printed its version banner with no part,
no programmer and no `-P` port argument.

Two point-in-time scans of `/proc/<pid>/fd/*` found no process holding
`/dev/ttyACM0`. That evidence is deliberately **not** overstated:

* the scans are **snapshots, not continuous monitoring**, so they cannot prove
  the node was never opened between them;
* `/proc/<pid>/fd` is readable only for processes owned by this user, so a root
  or other-user holder would not appear;
* the evidence covers **this session only** and says nothing about earlier use.

**Device-node mtime is not used as evidence anywhere in this package.** The
mtime of a character device reflects node creation and udev activity, not
`open()` calls, so it cannot establish that a port was never opened. An earlier
draft of this report used it that way; that claim has been withdrawn.

The defensible statement is narrower: **no command issued during this work
opens a serial port**, and the two snapshots found no holder.

### 2.2 Residual uncertainty — read this before flashing

The identification is strong but **not complete**, and the gaps matter:

1. **The USB descriptor is emitted by the on-board SAMD11 bridge, not by the
   ATmega4809.** It proves the board model, not the silicon behind it. Reading
   the actual MCU signature requires UPDI, which is excluded here — but it is
   achievable with no extra hardware over this same port, and it is stage 9.3
   step 10 of the trial procedure.
2. **The wiring is unverified.** Nothing in read-only USB metadata reveals
   which pins are connected to the 16 remote circuits or to the shared reset
   line. The pin map is inherited from the legacy sketch and is *asserted at
   compile time against the core's own `pins_arduino.h`*, which proves the pins
   are **capable**, not that they are **wired** as assumed.
3. **The number of remote circuits actually attached is unknown.**
4. Only one `/dev/ttyACM*` was present, so no ambiguity about which board is
   which — but that is a point-in-time observation.

### 2.3 Shared reset pin: capability confirmed

The original used `RESET_PIN = A7`. Verified against
`megaavr/1.8.8/variants/nona4809/pins_arduino.h`:

```
#define NUM_DIGITAL_PINS   22
#define PIN_A7             (21)
digital_pin_to_port[21] = PD      // PD5/AI5 -> a real PORT pin
#define PIN_WIRE_SDA       (22)   // PA2, its own index
#define PIN_WIRE_SCL       (23)   // PA3, its own index
```

So on **this** variant A7 is full digital GPIO and `digitalWrite()` works. This
is a genuine portability trap: on a classic ATmega328P Nano, A6/A7 are
**ADC-input-only pads with no PORT register bit**, so `pinMode()/digitalWrite()`
silently do nothing and "reset" would never pulse. `dart_board_config.h`
therefore refuses to build for any unqualified target, and the `.ino` enforces:

```cpp
static_assert(PIN_A7 < NUM_DIGITAL_PINS,
  "Shared reset pin A7 is not digital-capable on this variant");
static_assert(dartPinsOk(kChannelPin, DART_REMOTE_COUNT),
  "Channel pin map invalid: out of range, duplicated, or colliding with "
  "D0/D1 (UART header), TWI, or the shared reset pin");
```

A successful compile (section 5) means every one of these assertions held.

**The reset line is shared by all sixteen remote circuits.** Driving it reboots
all of them. The firmware says so in its own log output and reconciles all
sixteen channels, rather than pretending the reset is per-remote.

---

## 3. Firmware corrections

Each entry names the original defect, the evidence that it is real
(`make reference`, section 4.3), and the correction.

### R1 — a timed-out key leaked to a different remote
The original queued key events in a shared 2048-byte FIFO with no owner. A key
that timed out stayed queued and was consumed later by whichever remote read
next. *Confirmed: channel 8 was handed channel 1's obsolete `0x83` (GUIDE).*
**Fix:** the FIFO is gone. At most **one** key transaction is outstanding, and
`finishTxn()` stops presenting the key before it reports, so no stale key can
be read. Rationale is documented in `dart_core.h`: the I2C contract carries no
transaction id and the slave API exposes no master address, so correlation can
only be temporal — the only sound way to keep it correct is one at a time.

### R2 — `ackReceived` was a bare boolean
Any ACK satisfied any outstanding command. *Confirmed: an UP reported
"TX+ACK confirmed" using an ACK that was not a response to it.*
**Fix:** an explicit stage machine (`IDLE → PULSE_LOW → PRESENTED → READ →
ACKED`). An ACK is credited **only** from `READ`. From `PULSE_LOW`/`PRESENTED`
it is counted as `ackEarly` and ignored; with nothing outstanding it is
`ackStray`; after `ACKED` it is `ackDuplicate`.

### R3 — only one held key per remote
The second DOWN was deferred, so DVR+GUIDE / HOME+GUIDE — which RF unpair
requires — was impossible. *Confirmed: GUIDE `0x83` was never transmitted.*
**Fix:** `DART_HELD_SLOTS = 4` per remote. A fifth is refused explicitly.

### R4 — "per-remote" reset drove the shared line but cleared one slot
*Confirmed: after resetting remote 1, remote 5's state still claimed a held key
although the shared line had rebooted it too.*
**Fix:** `finishSharedReset()` calls `clearAllRemotes()`, flushes the queue and
clears `initSeen_`, and the log states that all 16 reboot.

### R5 — release-all sent a hard-coded `0x03`
*Confirmed: `allup` sent `0x03` (Guide UP) and never `0x6F`, the actual SAT
release.* **Fix:** release-all walks the held-key table and releases the keys
actually held, including `UNCERTAIN` ones.

### R6 — zero-duration Format A released twice
*Confirmed: two `0x03` release transmissions.* **Fix:** the slot is cleared on
the confirmed release, so the second cannot be scheduled.

### R7 — Format-B reset passed a pointer as the millisecond delay
Full analysis in section 3.1. **Fix:** the Format-B path sets
`holdMs = DART_RESET_MS_DEFAULT`, and `pulseSharedReset()` clamps every request
to `[DART_RESET_MS_MIN, DART_RESET_MS_MAX]` = `[50, 2000]` ms. The pulse is
non-blocking: `tick()` releases it on a deadline, so serial intake and release
deadlines continue during the pulse.

### R8 — unbounded `String` growth and permissive parsing
*Confirmed: 4000 bytes with no newline were appended to an Arduino `String`;
`1 ZZ GG 100` parsed silently as key `0x00` and that byte was transmitted.*
**Fix:** a fixed `char line_[DART_LINE_MAX + 1]` (72 B); overlong input is
marked, discarded to the next newline and counted (`lineOverflows`). Strict
parsers (`parseHexByteStrict`, `parseDecU32`) reject any non-conforming token
with `[ERR] <code> <token>`. A `0x00` down-key is refused. The candidate
contains **no** `String`, `malloc`, `new` or `alloca` (the original used
`String` in 16 places).

### R9 — the blocking ACK/retry loop stalled the main loop
*Confirmed: a single `loop()` iteration consumed 3395 virtual ms.*
**Fix:** `tick()` never blocks. Retries are paced by `nextWorkAt_`; all waiting
is deadline-based. `ArduinoDartHal::print()` drops a whole message rather than
blocking on a full UART FIFO, and reports the drop count.

### R10 — the static footprint could not fit a classic ATmega328P
*Confirmed: `eventQ + txLog + rxLog` = 3488 B alone, against 2048 B total.*
**Fix:** bounded structures, 1429 B of globals (section 5), and a build-time
refusal to target unqualified boards.

### Additional hardening not in the original defect list
* **Interrupt state is restored, not blindly enabled.** The original called
  `interrupts()` unconditionally; `DartCritical` saves and restores `SREG`.
* **Channels idle HIGH before the driver is enabled**, so enabling the output
  cannot emit a spurious trigger edge.
* **No reset on boot** (`DART_RESET_ON_BOOT = 0`). An MCU-only reset — for
  example a host opening the port — must not reboot all 16 remote circuits.
  Handshake recovery is instead achieved by accepting the ACK opcode regardless
  of init state; see the deviation note in `dart_core.cpp`.
* **Bounded I2C receive.** The original read into a 64-byte stack array from a
  128-byte Wire buffer; the trampoline now uses an 8-byte bounded buffer while
  still retaining the last byte (the init config byte).
* **Honest outcome vocabulary**: `confirmed` / `uncertain` / `failed`, plus a
  host-hold watchdog at 8000 ms (above the application's longest legitimate
  3000 ms leg) so a hold abandoned by a crashed client is still released.

### 3.1 The "pointer-as-delay" defect, in detail

> **Attribution scope.** Everything below is a verified property of the
> host-supplied source copy and of binaries built from it here. The installed
> firmware was never identified (section 1.1), and the operator-supplied banner
> points at a different sketch family, so **none of this is attributed to the
> board currently in service.** Read it as "this source, built with this
> toolchain, does this", not as "the deployed board does this".

**The expression.** `firmware/test/reference/reference_sketch.ino.txt:370`,
inside the Format-B `<r> 99 reset` / `<r> ... reset` handler:

```cpp
if (action == "reset" || buttonNum == 99) {
  int delayMs = 500;                       // computed, then never used
  if (debugOn) { Serial.print("[RST] per-remote via Format B, R="); Serial.println(remoteNum); }
  resetRemote(remoteNum, "80");            // <-- const char* passed as int
  ...
}
```

The callee is `void resetRemote(int remoteNum, int delayMs = 500)` (line 130),
whose body does `digitalWrite(pin, LOW); delay(delayMs); digitalWrite(pin, HIGH);`.

**Why it compiles at all.** `const char*` to `int` is ill-formed in C++, so
this *should* be an error. The Arduino megaAVR platform compiles sketches with
`-fpermissive`, which downgrades it to a warning. Reproduced on the real
toolchain:

```
$ arduino-cli compile -b arduino:megaavr:nona4809 --warnings all \
    baseline/DART_16Remotes_Buffered_Handshake_TxAck_Timestamps
.../DART_16Remotes_...ino:370:34: warning: invalid conversion from 'const char*' to 'int' [-fpermissive]
       resetRemote(remoteNum, "80");
.../DART_16Remotes_...ino:130:7: note:   initializing argument 2 of 'void resetRemote(int, int)'
.../DART_16Remotes_...ino:368:11: warning: unused variable 'delayMs' [-Wunused-variable]
Sketch uses 16278 bytes (33%) of program storage space.
Global variables use 4251 bytes (69%) of dynamic memory...
```

The second warning is the corroborating evidence: the intended `500` is dead
code. The sketch **builds and is flashable** — this is live behaviour, not a
build failure.

**Compiled behaviour of a Nano Every build made here.** This is a build
produced by this work from the host-supplied source; it is not a readback of
installed flash. From the linked ELF:

```
$ avr-objdump -d ... | grep -B6 '<_Z11resetRemoteii>'   # the Format-B site
    2762:  6d e1    ldi  r22, 0x1D    ; 29
    2764:  7e e7    ldi  r23, 0x7E    ; 126     -> r23:r22 = 0x7E1D = 32285
    2766:  c6 01    movw r24, r12
    2768:  0e 94 19 08  call 0x1032   ; <_Z11resetRemoteii>

$ avr-objdump -s -j .rodata ...
 7e1a 523d0038 30005b45 ...   "R=\0" "80\0" "[E..."
                 ^^^^^ 0x7E1D = '8','0',0
```

The other three call sites load `0x01F4` (500) or a parsed value. Only this one
loads the **address of the string literal `"80"`**.

So this build executes `delay(32285)`:

* the **shared reset line is held LOW for ~32.3 seconds**, not 80 ms;
* all sixteen remote circuits are held in reset for that whole time;
* because `delay()` blocks, the main loop is dead for ~32 s — serial intake
  stops and every pending release deadline is missed (compounding R9).

**It is worse than it looks — a latent sign flip.** `int` is 16 bits on AVR
(`INT16_MAX` = 32767) and the literal sits at 32285, only **482 bytes** below
the boundary. Flash is mapped into data space at `+0x4000`, and the sketch
currently uses 16278 of 16384 bytes below that line, so **~106 bytes** of extra
code or string data would push the literal past `0x7FFF`. The argument then
becomes negative, and `delay()` takes `unsigned long`, so the sign-extended
value is enormous: e.g. `-32251` becomes `4294935045` ms ≈ **49.7 days** of
continuous reset assertion. Any trivial edit to the original sketch can trip
this.

**Host-side manifestation.** The native reproduction compiles the same sketch
for x86-64 where `int` is 32 bits and pointers are truncated, so the value is
ASLR-dependent and differs every run (observed `127050890` and `3174392970`
virtual ms in two runs). The suite asserts only that the pulse is *far longer
than 80 ms* and prints the observed value with that caveat.

**Regression test.** `firmware/test/dart_tests.cpp` contains two dedicated
scenarios, which fail against a mutant that reintroduces a wrong width
(section 7.2, mutant M1):

```
[test] Format-B '<r> 99 reset' pulse is the documented default, not pointer-derived (REGR #7)
[test] every reset entry form produces the same bounded pulse width
```

and `firmware/test/emu/avr_emu_tests.cpp` re-proves it in **real AVR machine
code**, where the 16-bit `int` of the original actually applies:

```
EMU_RESET_PULSE_DEFAULT_MS=500      # '1 99 reset'
EMU_RESET_PULSE_50_MS=50            # '3 reset 50'
```

---

## 4. Verification layers, with exact commands

Layers are deliberately separate and must not be conflated. In particular the
native and PTY harnesses are **host-native C++**, not MCU emulation.

### 4.1 Native regression harness — PASS

Links the unmodified `dart_core.cpp`. Virtual clock, pin-edge logs, 16
independent edge-driven I2C master models.

```
$ cd firmware/test && make run
=== 607 checks passed, 0 failed ===
```

**607 individual assertions across 52 independent test scenarios.**
These are different units and are reported separately: a scenario constructs
one situation, and each `CHECK()` inside it is one assertion. Earlier runs of this
work reported `505/505` and then `606/606`; the count is now **607/607** —
the adversarial scenarios in section 7.3 were added first, and a
`harness integrity` scenario was added with the harness hardening in
section 7.2. The figure is regenerated by
the command above, not quoted from history.

### 4.2 Sanitizers — PASS

```
$ cd firmware/test && make sanitize
=== 607 checks passed, 0 failed ===
```

Same 607 assertions under `-fsanitize=address,undefined`. **Zero** sanitizer
diagnostics (`grep -ciE 'ERROR: AddressSanitizer|runtime error|LeakSanitizer'`
→ `0`).

### 4.3 Original-defect reproduction — PASS

The original sketch is vendored verbatim and `#include`d, so these observations
come from the original code itself, not a description of it. `-fpermissive` is
required (see section 3.1).

```
$ cd firmware/test && make reference
=== 20 defect behaviours confirmed, 0 not confirmed ===
```

**20 confirmed behaviours across 11 defect groups (R0–R10)** — again, distinct
units: the 20 are individual assertions, the 11 are the defect groups.

### 4.4 Arduino target compilation — PASS

See section 5.

### 4.5 Compiled-MCU emulation — PARTIAL (documented limitation)

```
$ cd firmware/test && make emulate
EMU_CHECKS=27
EMU_FAILS=0
EMU_RESET_PULSE_DEFAULT_MS=500
EMU_SIZEOF_INT=2
EMU_SIZEOF_PTR=2
EMU_SIZEOF_DARTCORE=925
mcu=atmega644p (NOT the ATmega4809 target; see Makefile header)
=== emulation: 27 checks passed, 0 failed ===
```

**What actually ran.** `dart_core.cpp` was compiled by `avr-g++` to AVR machine
code and **executed instruction by instruction inside simavr**. Results were
read out of the emulated SRAM over the gdb remote protocol. This is genuine
compiled-MCU execution — it is *not* the native harness.

**Exact limitation 1 — the MCU is not the target.** No available emulator
implements the megaAVR-0 series:

```
$ simavr --list-cores
... atmega128 ... atmega2560 ... atmega328 atmega328p ... atmega644 atmega644p ...
(no atmega4809; qemu-system-avr models only ATmega328P/ATmega2560 machines)
```

ATmega644P was chosen as the closest available core: same 8-bit AVR ABI, 16-bit
`int`, 2-byte data pointers, ≤64 KiB flash, and 4 KiB SRAM (the ATmega328P's
2 KiB cannot hold this firmware's 3303 B of `.data`+`.bss` in the test build).
**The ATmega4809 target binary was never emulated.**

**Exact limitation 2 — no peripherals are modelled.** TWI, USART and timers are
unused in the emulation build: the I2C callbacks are invoked **directly** and
`millis()` is a software counter. Therefore **full I2C testing is not possible
in the emulator**: there is no TWI state machine, no bus arbitration, no clock
stretching, no address matching and no real ISR timing.

**Retained coverage.** What the emulator does prove, and the native harness
cannot:

* the real AVR code generation of the state machine behaves correctly —
  `sizeof(int) == 2` and `sizeof(void*) == 2`, where the original's
  pointer-to-`int` truncation actually bites;
* the Format-B reset pulse is **500 ms** and an explicit `reset 50` is 50 ms,
  in compiled AVR code (the R7 fix on real 16-bit arithmetic);
* 32-bit `millis()` wraparound, walked one millisecond at a time through
  `0xFFFFFFFF`, does not strand a hold;
* two simultaneous held keys, release-all, stray-ACK rejection and
  read-but-never-acknowledged → `uncertain` all hold on AVR;
* `sizeof(DartCore) == 925` bytes, measured on an AVR ABI.

I2C-contract coverage (register layout, ACK correlation, early/late/duplicate
ACKs, cross-channel stale events) is retained in the native harness (4.1) and
in the PTY integration layer (4.6), both of which drive the same
`onI2cRequest`/`onI2cReceive` entry points.

**Note on the toolchain's own gdb.** The Arduino-bundled `avr-gdb` 7.8 links
against `libncurses.so.5`/`libtinfo.so.5`, which current distributions no
longer ship. `run_emulation.sh` detects this and exits **77 (skip)** with
instructions rather than reporting a false pass.

### 4.6 JAMboreeVision virtual-serial integration — PASS

See section 6.

### 4.7 Physical hardware validation — NOT RUN (out of scope)

Excluded by instruction. Staged procedure in section 9.

No serial port was opened and no programming operation was performed. `avrdude`
**was** executed exactly once, as `avrdude -C <conf> -v`, to read its version
banner: no `-p` part, no `-c` programmer and no `-P` port, so no device was
contacted. The full command inventory and the limits of that evidence are in
`hw-evidence.txt` and summarised in section 2.1.

---

## 5. Target compilation, memory and stack headroom

The Nano Every FQBN has a `mode` menu option and its **default is `mode=on`**
(ATmega328 register emulation). Both are built; `off` is recommended.

```
$ arduino-cli compile -b arduino:megaavr:nona4809:mode=on  --clean --warnings all firmware/dart_remote_bridge
$ arduino-cli compile -b arduino:megaavr:nona4809:mode=off --clean --warnings all firmware/dart_remote_bridge
```

| Build | Flash | SRAM (globals) | Free for stack+locals |
|---|---|---|---|
| Candidate `mode=on` | 17987 B (36.6 % of 49152) | 1429 B (23.3 %) | 4715 B of 6144 |
| Candidate `mode=off` | 17987 B (36.6 %) | 1429 B (23.3 %) | 4715 B of 6144 |
| Original sketch (same target) | 16278 B (33.1 %) | **4251 B (69.2 %)** | 1893 B |

The candidate uses **2822 fewer bytes of SRAM** than the original. Both builds
are warning-clean apart from the core's own `mode=on` speed notice.

### 5.1 Stack budget — calculated, not measured

Full arithmetic and inputs: `stackusage/STACK-BUDGET.txt`, with the raw
`.su` files alongside it. Three distinct evidence classes are kept separate
because they have very different strength.

**Class 1 — compiler-reported per-function frames.** `avr-g++ 7.3.0
-fstack-usage`, `-mmcu=atmega4809`, with the same `-Os`/`-D`/`-I` flags as the
shipped build. **Arduino core, Wire, twi, UART and Print are included**, each
from its own `.su` file — not just `dart_core.cpp`. Every frame is reported
`static` or `dynamic,bounded`: no VLA, no `alloca`, no runtime recursion (the
only recursion is the `constexpr` pin-map validator, evaluated at compile time).

| function | bytes | TU |
|---|---|---|
| `DartCore::handleLine` | 223 | dart_core |
| `DartCore::printQstat` | 205 | dart_core |
| `DartCore::printStatus` | 133 | dart_core |
| `DartCore::finishTxn` | 126 | dart_core |
| `DartCore::emitOutcome` | 122 | dart_core |
| `DartCore::startReleaseWork` | 120 | dart_core |
| `DartCore::startQueuedCommand` | 113 | dart_core |
| `DartCore::err` | 98 | dart_core |
| `DartCore::beginTxn` | 96 | dart_core |
| `DartCore::dispatchTokens` | 86 | dart_core |
| `loop()` | 64 | sketch `.ino` |
| `onRequestTrampoline` | 19 | sketch `.ino` |
| `__vector_15` / `__vector_14` (TWI ISRs) | 17 / 17 | Wire `twi.c` |
| `DartCore::tick` | 17 | dart_core |
| `onReceiveTrampoline` | 13 | sketch `.ino` |
| `TwoWire::write(const uint8_t*, size_t)` | 10 | Wire |
| `UartClass::write(uint8_t)` | 8 | core UART |
| `ArduinoDartHal::print` | 8 | sketch `.ino` |
| `digitalWrite` | 8 | core wiring_digital |
| `DartCore::onI2cRequest` / `onI2cReceive` | 2 / 2 | dart_core |

**Caveat on Class 1:** the shipped build uses `-flto`, and with `-flto` the
compiler emits **no** `.su` at all, so these numbers come from an otherwise
identical **non-LTO** compile. Post-LTO inlining in the shipped image can
change individual frame sizes.

**Class 2 — avr-libc frames read from the linked image.** avr-libc ships
precompiled, so there is no `.su` for the formatting path that `snprintf()`
pulls in. These were derived from the disassembly of
`FINAL_off/dart_remote_bridge.ino.elf`:

| symbol | bytes | derivation |
|---|---|---|
| `snprintf` | 18 | 4 registers via `__prologue_saves__+0x1c`, + 14 B locals |
| `vfprintf` | 29 | 18 registers via `__prologue_saves__+0x00`, + 11 B locals |
| `__ultoa_invert` | 6 | inline pushes, no frame pointer |

An earlier draft covered this with a blanket "200 B ISR/library allowance".
That guess has been replaced by the figures above.

**Class 3 — calculated call-chain budget.** This is **arithmetic performed
here**, summing Class 1 and Class 2 frames along call chains **identified by
reading the source**. It is not a compiler whole-program analysis.

```
WORST  loop(64) + feedSerial(4) + handleLine(223) + dispatchTokens(86)
       + printQstat(205) + snprintf(18) + vfprintf(29) + __ultoa_invert(6)
       + 7 return addresses (14)                                    = 649 B
       transaction reporting chain                                  = 394 B
       release-start chain                                          = 331 B
worst ISR chain (AVR does not nest interrupts by default, so only
one is ever live): __vector_15(17) + onRequestTrampoline(19)
       + onI2cRequest(2) + TwoWire::write(10) + 8                   =  56 B

calculated worst case = 649 + 56                                   = 705 B
available for stack + locals (6144 - 1429 globals)                  = 4715 B
calculated headroom                                                = 4010 B
```

**705 B of a 4715 B budget, i.e. 15.0 %.**

**What is NOT established.** No **stack high-water measurement was performed**,
so the margin above must be described as a *calculated budget*, never as
experimentally measured. No measurement of any kind was taken on an ATmega4809
(no emulator supports it and no hardware was used). The chain enumeration is
manual and could miss a deeper path. Obtaining a real high-water figure means
painting SRAM at reset and reading back the unpainted boundary on hardware;
that is listed as a step in section 9, not claimed here.

## 6. JAMboreeVision against an isolated virtual serial endpoint

### 6.1 Isolation

* The endpoint is a **PTY** created by `firmware/test/pty_sim`
  (e.g. `/dev/pts/18`). `/dev/ttyACM0` is **never opened** — asserted by the
  test itself.
* `JAMBOREE_BASE` and `JAMBOREE_VISION_DATA` are redirected to a fresh
  `mkdtemp()` directory **before the application is imported**, so the
  operator's `base.txt` is neither read nor written.
* Port registration goes through the application's **own**
  `jamboree.app.init_serial_from_base()`.
* The running application was not restarted (it was not running; nothing held
  the port).

### 6.2 What the simulator is

`pty_sim` links the same `dart_core.cpp` and the same edge-driven
`RemoteMaster` bus model as the native suite, with virtual time slaved to the
host monotonic clock so the application's real `time.sleep()`-based holds behave
realistically. It is **host-native code, not an MCU emulator**.

Two simulator bugs were found and fixed while building this layer, both
simulator-side, neither a firmware defect:
1. `DartBus::begin()` clears captured output (correct for unit tests), which
   discarded the start-up banner; `pty_sim` now retains it.
2. A PTY defaults to `ECHO` on, which fed the firmware's own output back into
   its parser (observed as `[ERR] bad-remote`). `pty_sim` now sets the PTY to
   raw mode, as a real UART would be.

### 6.3 Results

```
$ cd firmware/test && make vision
=== 18 passed, 0 failed ===
```

| Scenario | Result |
|---|---|
| Virtual port opened by the application's serial worker | PASS |
| Application resolved the PTY, not `/dev/ttyACM0` | PASS |
| Firmware emits an identity banner at start-up | PASS |
| Firmware identity retrievable on demand after the host attaches | PASS |
| **Tap** — confirmed DOWN and UP on remote 1 | PASS |
| **Hold** — 3 s hold on remote 2 released only after the hold (3.06 s) | PASS |
| **Unpair `dvr`** — SAT, then DVR+GUIDE together, then release all | PASS |
| **Unpair `home`** — SAT, then HOME+GUIDE together, then release all | PASS |
| Both unpair paths report serial delivery only, not receiver confirmation | PASS |
| **Release-all** — `1 86 allup` | PASS |
| **Shared reset** — `1 99 reset`, firmware reconciles all 16 channels | PASS |
| **Multi-settop** — 3 settops sharing one board, per-target results | PASS |
| Multi-settop issued exactly one reset per settop, serialised on the port | PASS |
| Multi-settop feedback never claims receiver confirmation | PASS |
| **Honest failure** — master never reads → `failed`, never success | PASS |
| **Honest uncertainty** — master reads, never ACKs → `uncertain` | PASS |

Observed application-generated traffic, captured by the simulator:

```
unpair dvr : 1 35 down | 1 35 up | 1 3 down | 1 4 down | 1 35 up | 1 3 up | 1 4 up | 1 86 allup
unpair home: 1 35 down | 1 35 up | 1 2 down | 1 4 down | 1 35 up | 1 2 up | 1 4 up | 1 86 allup
multi      : 1 99 reset | 2 99 reset | 3 99 reset
```

Honesty of reporting, end to end:

```
master never reads : [EVT] r=2 key=0x83 act=down result=failed
master never ACKs  : [EVT] r=2 key=0x83 act=down result=uncertain
                     [WARN] r=2 up=0x03 unreleased after 3 tries; use '2 99 reset' (SHARED line) if needed
application        : {"status":"sent","via":"dart",
                      "feedback":"Serial write completed; receiver action not confirmed"}
```

The multi-settop endpoint groups targets by `com_port`, so settops sharing one
board are **serialised** ("a DART port owns a complete hold/chord before the
next channel") while distinct ports proceed concurrently. The three resets above
appear in order on the single shared port, which is the intended behaviour.

---

## 7. Test independence

### 7.1 Evidence vs. assumption in the remote model

**From protocol/source evidence** (legacy `DART_Dumb.ino` and
`DART_16Remotes_*`, plus the application's `serial_bridge.py` / `commands.py`):

* the remote circuits are I2C **masters**, this MCU is the slave at `0x34`;
* a key is offered by pulsing that remote's interrupt line LOW then HIGH;
* the master reads a **15-byte** reply, `[0]=0xAF` (`0x00` right after an ACK),
  `[1]=INT_REG 0x01`, `[2]=EVT_REG 0x01`, `[3]=key`, `[4..14]=0x00`;
* the master acknowledges by writing opcode `0x02`; at its power-up it writes
  opcode `0x01` plus a config byte;
* the host wire formats are `"<r> <downHex> <upHex> <holdMs>"` and
  `"<r> <btn> down|up|reset|allup"` with `reset`→99, `allup`→86 — taken from the
  application, and confirmed by the live traffic captured in section 6.3.

**Assumptions, chosen to be pessimistic rather than convenient:**

* `readDelayMs = 3`, `ackDelayMs = 2` — plausible, not measured. Real timing is
  unknown; this is why the firmware uses a bounded ACK deadline (default 300 ms,
  range 50–2000) rather than relying on any particular latency.
* that a master reads **once** per falling edge;
* that one init exchange represents the shared bus;
* that a reset really reboots the remote circuits (inferred from the wiring
  description, **not** verified — see section 2.2).

### 7.2 The model does not mirror the implementation

`RemoteMaster` observes only `hal.channelHigh[]` (pin edges) and the 15 bytes
returned by `onI2cRequest()`. It never reads `DartCore` state, and the
firmware never reads the model's state. The behaviour switches (`doRead`,
`doAck`, `duplicateAck`, `pollWhenIdle`, delays) are set by tests, not derived
from the firmware.

That argument is structural, so it was also **tested empirically**. Four
single-defect mutants of the *current* candidate were built and the unmodified
suite was run against each. A suite that merely mirrored the implementation
would still pass:

| Mutant | Injected defect | Suite result |
|---|---|---|
| M1 | Format-B reset width 80 ms instead of the documented default | **3 assertions fail** (604 pass) |
| M2 | credit an ACK that arrives before any read | **13 assertions fail** (594 pass) |
| M3 | shared reset clears only the named remote | **5 assertions fail** (602 pass) |
| M4 | keep presenting the key after the transaction ends | **198 assertions fail** (422 pass), including 14 `HARNESS-FAULT` reports |

All four are caught. Raw logs: `logs/MUT_*.txt`.

**Harness hardening (this revision).** M4 previously crashed the harness with a
SIGSEGV instead of reporting a failure. AddressSanitizer localised it exactly:
a heap-buffer-overflow at `dart_tests.cpp:152`, where a `CHECK_EQ(size, 2)`
failed and execution then went on to index `keysSeen[1]` on a shorter vector.
The same pattern existed at ten other sites, plus a `channelEdges[upIdx]` read
where `upIdx` can legitimately remain `-1`.

The fix is entirely harness-side and **weakens nothing**:

* every accessor the tests use to look into the remote model or the pin logs is
  now range-checked (`keyAt`, `replyAt`, `channelEdgeTime`, `resetEdgeTime`).
  Out of range reports a `HARNESS-FAULT` and returns a sentinel (`-1`, or
  `0xFFFFFFFF` for a timestamp) that cannot compare equal to any legal value,
  so the dependent assertion **still fails** — it just fails cleanly;
* the observation logs are capped (200000 pin edges, 50000 keys per channel,
  8 MiB of captured output) so a firmware that spins cannot exhaust memory, and
  `settle()` aborts early with a `HARNESS-FAULT` when a cap is hit;
* a new final scenario, `harness integrity`, asserts `g_harnessFaults == 0` and
  prints the observation high-water marks.

For the real candidate every accessor is in range and no cap is approached, so
the assertions are unchanged in effect. The high-water report proves the margin:

```
observation high-water: channelEdges=22/200000 resetEdges=2/200000
                        keysSeen=80/50000 out=25907/8388608 bytes
```

M4 now terminates with `rc=1`, a complete summary and attributed failures —
including `g_harnessFaults == 0 (14 vs 0)` — under a deliberately imposed 2 GiB
address-space limit, and runs clean under AddressSanitizer/UBSan with **zero**
sanitizer diagnostics. The crash is gone; the detection is not.

### 7.3 Adversarial scenarios

Late and stray ACKs. The two late-ACK scenarios were **renamed** so their
names state the window they actually cover instead of implying general ACK
attribution, which this protocol cannot provide (section 7.4). The assertions
were not changed:

* `an ACK with no transaction outstanding is counted stray and changes nothing`
* `a stray ACK during the shared-reset pulse neither confirms nor shortens it`
* `a cross-channel late ACK before the next read is counted early, not credited`
  — was *"a late ACK from channel 1 cannot confirm channel 2's in-flight DOWN"*
* `a late ACK arriving before the next transaction's read is not credited`
  — was *"a late ACK from an earlier transaction cannot confirm the next one"*
* `a storm of stray ACKs cannot fabricate a confirmation under load`
* `ACK arriving before the read is not credited (REGRESSION #2)`
* `duplicate ACK is counted and harmless`

In **both** late-ACK scenarios the stray ACK is injected while the next
transaction is still only `PRESENTED` — nobody has read it yet. That is the
window the firmware can detect, because an ACK before any read cannot belong to
the current transaction. Neither scenario demonstrates, and neither name now
claims, attribution of an ACK to a particular remote. A source comment at each
site states this scope.

The cross-channel case asserts the full chain, including the **negative**:

```cpp
b.master[0].doAck = false;  b.send("1 35 down");  b.settle();   // ch1 -> uncertain
b.master[1].doRead = false; b.master[1].doAck = false;
b.send("2 4 down"); b.advance(6);
b.master[0].sendAck();                                          // ch1's late ACK
CHECK(!b.hal.sawOut("r=2 key=0x83 act=down result=confirmed")); // not credited
CHECK( b.hal.sawOut("r=2 key=0x83 act=down result=failed"));
CHECK( b.core.stats().ackEarly >= 1u);
CHECK(!b.core.isHeld(2, 0x83));
```

Cross-channel stale events:
* `a timed-out key is never delivered to another channel (REGRESSION #1)`
* `only one transaction is outstanding at a time across 16 channels`
* `unexpected idle polling never yields a key`
* `UP for a key that was never pressed still only releases that key`

Release deadlines under load:
* `release deadlines are met while serial input and debug logging flood`
* `release deadlines are met on many channels under combined load`
* `reset pulse width is bounded and does not block serial intake`
* `queue saturation is rejected explicitly with no corruption`
* `a hold abandoned by a disconnected client is released by the watchdog`

Plus `millisecond counter wraparound`, `recovery after a simulated MCU-only
reset`, `recovery after a remote-side reset`, and `interrupt state is always
restored, never blindly enabled`.

### 7.4 ACK ambiguity the protocol cannot resolve

This is a **permanent limitation of the I2C contract**, not a firmware bug, and
no amount of testing removes it:

* the contract carries **no transaction identifier**;
* the slave Wire API exposes **no master address**;
* there is exactly **one** key register.

Therefore the firmware **cannot prove** which remote performed a given read or
ACK. Correlation is purely temporal. Three consequences are reported rather
than hidden:

1. `read` means *some* master read the presented key; `acked` means *some*
   master acknowledged after that read.
2. An ACK arriving **before** anything read the current key must belong to an
   earlier transaction, so it is counted (`ackEarly`) and ignored — that is the
   only late-ACK case the firmware can detect, and it is the case the two
   renamed tests cover. An ACK arriving **inside** the current transaction's
   `READ` window is **indistinguishable** from the expected one, whichever
   remote sent it. **No cross-channel or per-remote ACK correlation is claimed
   anywhere in this firmware, its tests, or this document.** The mitigation is structural:
   only one outstanding transaction, and the key stops being presented the
   instant the transaction ends, so the blast radius is one transaction rather
   than a FIFO of stale keys. The tests above pin the realistic cases
   (`ackEarly`, `ackStray`, `ackDuplicate` counters), and `qstat` exposes the
   counters so an operator can see anomalies.
3. **No ACK ever proves a set-top box acted.** It proves bytes moved on the
   I2C bus. The firmware never claims RF success, and the application's
   `"Serial write completed; receiver action not confirmed"` matches.

---

## 8. Reproducing the whole environment

```
git checkout montjac

# every layer that needs no hardware
cd firmware/test && make all-layers        # run + sanitize + reference + emulate + vision

# or individually
make run                                   # 607 assertions / 52 scenarios
make sanitize                              # same, ASan + UBSan
make reference                             # 20 original-defect confirmations
make emulate                               # compiled AVR in simavr (exit 77 if absent)
make vision                                # JAMboreeVision over a PTY, temp app data

# target builds
export ARDUINO_CONFIG_FILE=<your arduino-cli.yaml>
arduino-cli core install arduino:megaavr@1.8.8
arduino-cli compile -b arduino:megaavr:nona4809:mode=off --clean --warnings all firmware/dart_remote_bridge
arduino-cli compile -b arduino:megaavr:nona4809:mode=on  --clean --warnings all firmware/dart_remote_bridge
```

Prerequisites: g++ ≥ 9, Python 3 with `pyserial` and `flask`, arduino-cli 1.4.1
with `arduino:megaavr` 1.8.8; for `make emulate` also simavr ≥ 1.6 and a working
`avr-gdb` on `PATH`.

### 8.1 Known limitations of the environment

* No ATmega4809 emulator exists, so the target binary is never executed
  anywhere except, eventually, on real hardware (section 4.5).
* The emulation layer models **no** peripherals; I2C-contract behaviour is
  covered only by the native and PTY layers.
* `pty_sim` timing is host-scheduler dependent; it is not a timing-accurate
  model of a 16 MHz ATmega4809.
* The remote-side model rests on the assumptions in section 7.1.
* `avr-gdb` 7.8 needs ncurses5 compat libraries.
* The wiring and the number of attached remote circuits remain unverified.
* The installed firmware was never identified, so no finding here is attributed
  to the board in service (section 1.1).
* The stack figure is a calculated budget, not a measured high-water mark, and
  the `.su` inputs come from a non-LTO compile (section 5.1).

---

## 9. Controlled hardware trial and rollback

**Not performed.** No stage below was executed. The stages are deliberately
separated by the kind of access each one needs, so a reviewer can authorise them
independently and stop at any boundary.

### 9.0 Backup and readback capability — corrected

An earlier draft of this report claimed a byte-exact backup "cannot be read over
CDC" and "requires the onboard mEDBG/UPDI programmer" as if that were extra
hardware. **That was wrong on both counts.** Verified against the installed
tools and the official platform implementation:

* `arduino:megaavr` 1.8.8 `boards.txt` defines
  `nona4809.upload.protocol=jtag2updi`, `nona4809.upload.speed=115200`,
  `nona4809.upload.use_1200bps_touch=true`,
  `nona4809.upload.extra_params=-P{serial.port}`. The official upload path is
  therefore **avrdude speaking jtag2updi over the same `/dev/ttyACM0` CDC
  port** — the SAMD11 bridge is the UPDI programmer. No extra hardware.
* The installed `avrdude 6.3-20190619` (`6.3.0-arduino17`) defines part `m4809`
  with `signature = 0x1E 0x96 0x51` and `memory "flash"` of `size = 0xC000`,
  `offset = 0x4000`, `page_size = 0x80`, **`readsize = 0x100`** — i.e. flash
  **read** is defined, not just write — and programmer `jtag2updi`
  (`type = jtagmkii_pdi`, serial, 115200).
* The official `platform.txt` upload recipe uses `-e` (full chip erase) before
  `-Uflash:w:`, so **any backup must be taken before flashing**; it also writes
  `fuse2`, `fuse5` and `fuse8`, so those are worth capturing too.

So a byte-exact backup is **expected to be possible with no additional
hardware**. What is **not** verified, because it needs the programmer:

* that the SAMD11 bridge firmware implements UPDI **read** (the upload path only
  ever exercises erase/write);
* that UPDI access is not locked on this specific board;
* that the readback actually succeeds and verifies.

Those are the first things stage 9.3 proves, and the trial must stop if they
fail.

**Do not assume the installed sketch has a `version` command.** It probably does
not: the operator-supplied banner points at the `DART_Dumb.ino` family, whose
console is a numeric/echo style, not a command set. The identity of installed
firmware is captured by reading its **startup banner** (stage 9.2) and, more
reliably, from the **flash image itself** (stage 9.3) — not by sending `version`.

### 9.1 Read-only preparation — no port access

No open, no control line, no programmer.

1. Confirm the board is still enumerated and unchanged: re-read
   `idVendor`/`idProduct`/`serial` from sysfs and compare with section 2.1.
2. Confirm the candidate artefacts match this package: verify the SHA-256
   manifest, and that `firmware/` is at the commit recorded in the report.
3. Re-run every virtual layer (`make all-layers`) on the build host.
4. **Verify the wiring by inspection** against section 2.3 — 16 channels on
   D2–D13/A0–A3, shared reset on A7, TWI on its own pins — and record how many
   remote circuits are actually attached. This is the largest unverified
   assumption in the package.
5. Schedule a maintenance window: stage 9.5 reboots **all** remote circuits.
6. Confirm who owns rollback authority and that an irreversible outcome is
   acceptable if stage 9.3 fails.

### 9.2 Port opening — read-only serial, no programmer, no flashing

First action that touches the device. Opening the CDC port may or may not reset
the MCU depending on DTR behaviour; the application deliberately no longer
toggles DTR (commit `b833c02`), but a terminal might.

7. Free the port: ensure JAMboreeVision is not holding it.
8. Open at 115200 **read-only** and capture whatever the board emits, plus a
   timestamp and the exact command used. Record the banner verbatim — this is
   the first real evidence of installed identity, and it should be compared
   against `11.05.2024 Jake Montgomery (That Was Good)` and against
   `Jacob Montgomery - Spreading the JAM 2025-11-12[a]`.
9. Do **not** send `version` expecting a reply. If a quiescent board prints
   nothing, that is itself consistent with a banner printed only at power-up.

### 9.3 Programmer access — readback only, still no flashing

This is the gate. It uses UPDI but writes nothing.

10. Confirm the MCU signature, which finally removes the ATmega4809 uncertainty
    of section 2.2 (the USB descriptor comes from the SAMD11, not the MCU):
    ```
    avrdude -C <conf> -c jtag2updi -p m4809 -P /dev/ttyACM0 -b 115200 -v
    ```
    Expect `0x1e9651`.
11. Take the byte-exact backup, **before any erase**:
    ```
    avrdude -C <conf> -c jtag2updi -p m4809 -P /dev/ttyACM0 -b 115200 \
            -U flash:r:dart_installed_$(date -u +%Y%m%dT%H%M%SZ).hex:i \
            -U fuse2:r:fuse2.hex:h -U fuse5:r:fuse5.hex:h -U fuse8:r:fuse8.hex:h
    ```
12. Record the backup's SHA-256 and store it off-box. Sanity-check that the
    image is not all `0xFF` and that its size is plausible.
13. Identify the installed firmware from the image: search it for the banner
    strings above. This answers section 1.1 definitively.
14. **STOP if the readback failed, is empty, or cannot be verified.** Flashing
    then becomes irreversible for firmware, which requires explicit written
    acceptance before continuing.

### 9.4 Flashing the candidate

15. Flash the identical artefact that was verified — same FQBN, same files:
    ```
    arduino-cli compile -b arduino:megaavr:nona4809:mode=off --clean firmware/dart_remote_bridge
    arduino-cli upload  -b arduino:megaavr:nona4809:mode=off -p /dev/ttyACM0 firmware/dart_remote_bridge
    ```
    Note this performs the 1200-baud touch and a **chip erase**.
16. Confirm the new banner:
    `DART-BRIDGE 2.0.0 2026-10-21 board=NANO_EVERY remotes=16 slots=4`.
    Then `status` and `qstat` should respond — on the candidate, unlike the
    installed firmware, these commands do exist.
17. Optional but recommended, and the only way to turn section 5.1 from a
    calculated budget into a measurement: take a **stack high-water** reading by
    painting SRAM at reset and reading back the unpainted boundary after a
    representative load.

### 9.5 Shared reset — reboots all sixteen remote circuits

Only inside the maintenance window.

18. Single channel first, escalating, watching each `[EVT] ... result=`:
    `1 83 03 120` (tap) → `1 4 down` / `1 4 up` (host-managed hold) →
    `1 86 allup`. **`confirmed` means the I2C handshake completed, not that a
    set-top box acted** (section 7.4).
19. `1 99 reset` — this asserts the line shared by all sixteen. Verify the pulse
    is ~500 ms on a scope or logic analyser. This is the first physical proof
    both that the R7 fix behaves and that A7 is actually wired to the reset line.
20. Confirm all sixteen channels recover and that `qstat` shows the reset.

### 9.6 RF pairing changes — affects receiver state

Last, because this changes state outside the board.

21. Run RF unpair for `dvr` and then `home` against **one** receiver, and
    confirm the receiver's observed behaviour separately from the board's
    `result=` reports.
22. Then the multi-settop batch across at least two channels sharing the board.
23. Watch `qstat` for `ackEarly`, `ackStray`, `ackDuplicate`, `cmdDropped`,
    `lineOverflows`, `watchdogReleases`. Non-zero `ackStray`/`ackEarly` means
    real remote-side timing worth investigating before wider rollout.

### 9.7 Rollback

Re-flash the image captured in stage 9.3 — the authoritative rollback source is
that **readback**, not either host-supplied source copy:

```
avrdude -C <conf> -c jtag2updi -p m4809 -P /dev/ttyACM0 -b 115200 \
        -U flash:w:dart_installed_<timestamp>.hex:i
```

Then confirm the original banner returns and power-cycle the remote circuits
once so handshake state is re-synchronised. If no readback exists, rollback can
only approximate the original from a host-supplied source copy, and section 1.1
explains why that is **not** known to be what was installed.

Rolling back also reinstates whatever defects the installed firmware has. Where
those are the defects reproduced in section 3 — which has not been established
for the installed image — prefer fixing forward unless the candidate is the
proven cause.

**Abort immediately if** the new banner does not match, any channel reports
`result=failed` consistently, the reset pulse is not ~500 ms, or the shared
reset does not recover all sixteen circuits.
