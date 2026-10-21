#!/usr/bin/env bash
# Execute the AVR build of dart_core.cpp in simavr and read the results out
# over the gdb remote protocol.  Prints EMU_* lines and exits non-zero on any
# failed check, so CI can gate on it.
set -u
ELF="${1:?usage: run_emulation.sh <elf>}"
MCU="${MCU:-atmega644p}"
F_CPU="${F_CPU:-16000000}"
SIMAVR="${SIMAVR:-simavr}"
AVR_GDB="${AVR_GDB:-avr-gdb}"
PORT="${GDB_PORT:-1234}"

command -v "$SIMAVR" >/dev/null 2>&1 || { echo "SKIP: simavr not found ($SIMAVR)"; exit 77; }
command -v "$AVR_GDB" >/dev/null 2>&1 || { echo "SKIP: avr-gdb not found ($AVR_GDB)"; exit 77; }

"$SIMAVR" -m "$MCU" -f "$F_CPU" -g "$ELF" >/tmp/dart_emu_sim.$$.log 2>&1 &
SIM=$!
trap 'kill $SIM 2>/dev/null' EXIT
sleep 2

OUT=$("$AVR_GDB" -batch \
  -ex "set confirm off" -ex "set pagination off" \
  -ex "target remote :$PORT" \
  -ex "break emu_report" -ex "continue" \
  -ex 'printf "EMU_CHECKS=%d\n", emu_checks' \
  -ex 'printf "EMU_FAILS=%d\n", emu_fails' \
  -ex 'printf "EMU_FAILMASK=0x%x\n", emu_failmask' \
  -ex 'printf "EMU_LAST_CHECK_ID=%d\n", emu_lastid' \
  -ex 'printf "EMU_RESET_PULSE_DEFAULT_MS=%d\n", emu_reset_pulse_ms' \
  -ex 'printf "EMU_RESET_PULSE_50_MS=%d\n", emu_reset_pulse_50' \
  -ex 'printf "EMU_SIZEOF_INT=%d\n", emu_sizeof_int' \
  -ex 'printf "EMU_SIZEOF_PTR=%d\n", emu_sizeof_ptr' \
  -ex 'printf "EMU_SIZEOF_DARTCORE=%d\n", emu_sizeof_core' \
  "$ELF" 2>&1)

if ! echo "$OUT" | grep -qE '^EMU_'; then
  if echo "$OUT" | grep -q 'libncurses.so.5\|libtinfo.so.5'; then
    echo "SKIP: avr-gdb cannot start (needs ncurses5 compat libraries)."
    echo "      Install libncurses5/libtinfo5 or put a working avr-gdb on PATH."
    exit 77
  fi
  echo "FAILED: no results read from the emulator"
  echo "$OUT" | tail -20
  exit 1
fi
echo "$OUT" | grep -E '^EMU_'
echo "mcu=$MCU (NOT the ATmega4809 target; see Makefile header)"

FAILS=$(echo "$OUT" | sed -n 's/^EMU_FAILS=//p')
CHECKS=$(echo "$OUT" | sed -n 's/^EMU_CHECKS=//p')
[ -n "${FAILS:-}" ] && [ "$FAILS" = "0" ] && [ "${CHECKS:-0}" -gt 0 ] \
  && { echo "=== emulation: $CHECKS checks passed, 0 failed ==="; exit 0; } \
  || { echo "=== emulation: FAILED (checks=${CHECKS:-?} fails=${FAILS:-?}) ==="; exit 1; }
