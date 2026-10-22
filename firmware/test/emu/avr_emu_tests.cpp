/* avr_emu_tests.cpp - executes the UNMODIFIED dart_core.cpp translation unit as
 * real AVR machine code inside simavr (instruction-level emulation).
 *
 * HONEST SCOPE (also recorded in docs/DART-FIRMWARE.md):
 *   - MCU emulated: ATmega644P.  This is NOT the ATmega4809 target.  No
 *     available emulator implements the megaAVR-0 series: simavr 1.6
 *     --list-cores has no atmega4809, and qemu-system-avr models only
 *     ATmega328P/ATmega2560 machines.  ATmega644P was chosen as the closest
 *     available core: same 8-bit AVR ABI, 16-bit int, 2-byte data pointers,
 *     <=64 KiB flash, and 4 KiB SRAM (enough for this firmware's footprint).
 *   - Peripherals modelled: NONE.  TWI, USART and timers are not used.  The
 *     I2C callbacks are invoked directly by this driver and millis() is a
 *     software counter.  This layer therefore proves AVR CODE GENERATION and
 *     ABI behaviour of the real core logic.  It is explicitly NOT an I2C bus
 *     simulation and not a substitute for hardware.
 */
#include <avr/io.h>
#include <avr/sleep.h>
#include <avr/interrupt.h>
#include <string.h>
#include <stdint.h>

#include "dart_core.h"
#include "dart_version.h"

extern "C" void __cxa_pure_virtual(void) { for(;;); }
void operator delete(void*) {}

volatile uint16_t emu_checks = 0;
volatile uint16_t emu_fails  = 0;
volatile uint32_t emu_failmask = 0;
volatile uint16_t emu_reset_pulse_ms = 0xFFFF;
volatile uint16_t emu_reset_pulse_50 = 0xFFFF;
volatile uint8_t  emu_sizeof_int = 0;
volatile uint8_t  emu_sizeof_ptr = 0;
volatile uint16_t emu_sizeof_core = 0;
volatile uint8_t  emu_finished = 0;
volatile uint8_t  emu_lastid = 0;

static void chk(uint8_t id, bool ok) {
  emu_lastid = id;
  emu_checks++;
  if (!ok) { emu_fails++; if (id < 32) emu_failmask |= (1UL << id); }
}

class AvrHal : public DartHal {
 public:
  uint32_t now;
  bool resetHigh;
  uint32_t resetLowAt;
  uint32_t lastPulseMs;
  uint16_t chanEdges;
  uint16_t resetEdges;
  char buf[160];
  uint8_t len;

  AvrHal() : now(0), resetHigh(true), resetLowAt(0), lastPulseMs(0xFFFFFFFFUL),
             chanEdges(0), resetEdges(0), len(0) { buf[0] = 0; }

  uint32_t millis() { return now; }
  void writeChannel(uint8_t ch, bool high) { (void)ch; (void)high; chanEdges++; }
  void writeSharedReset(bool high) {
    if (high == resetHigh) return;
    resetEdges++;
    if (!high) resetLowAt = now; else lastPulseMs = now - resetLowAt;
    resetHigh = high;
  }
  void print(const char* t) {
    while (*t) { if (len < sizeof(buf) - 1) buf[len++] = *t; t++; }
    buf[len] = 0;
  }
  uint8_t irqSave() { uint8_t s = SREG; cli(); return s; }
  void irqRestore(uint8_t s) { SREG = s; }

  void clear() { len = 0; buf[0] = 0; }
  bool has(const char* s) { return strstr(buf, s) != 0; }
};

static AvrHal hal;
static DartCore core(hal);

static void feed(const char* s) { while (*s) core.feedSerial(*s++); core.feedSerial('\n'); }

/* Raw time advance: no master activity at all. */
static void advance(uint32_t ms) { for (uint32_t i = 0; i < ms; ++i) { hal.now++; core.tick(); } }

static bool masterRead(uint8_t* key) {
  uint8_t b[DART_I2C_REPLY_LEN];
  uint8_t n = core.onI2cRequest(b, sizeof(b));
  if (n != DART_I2C_REPLY_LEN) return false;
  *key = b[3];
  return true;
}
static void masterAck() { uint8_t op = DART_I2C_CMD_ACK; core.onI2cReceive(&op, 1); }

/* Cooperative master: reads and acknowledges whatever is presented. */
static void serviceAdvance(uint32_t ms) {
  uint8_t k;
  for (uint32_t i = 0; i < ms; ++i) {
    hal.now++;
    core.tick();
    if (core.txnStage() == DART_TXN_PRESENTED) { if (masterRead(&k)) masterAck(); }
  }
}
/* Drain to a quiescent state so each scenario starts independent of the last. */
static bool idle(uint32_t maxMs) {
  for (uint32_t i = 0; i < maxMs; ++i) {
    if (core.txnStage() == DART_TXN_IDLE && core.queueDepth() == 0 && !core.resetting()) return true;
    serviceAdvance(1);
  }
  return core.txnStage() == DART_TXN_IDLE && core.queueDepth() == 0;
}

void emu_report(void) { emu_finished = 1; }

int main(void) {
  emu_sizeof_int = (uint8_t)sizeof(int);
  emu_sizeof_ptr = (uint8_t)sizeof(void*);
  emu_sizeof_core = (uint16_t)sizeof(DartCore);

  /* E1 banner */
  hal.clear(); core.begin();
  chk(1, hal.has(DART_FW_NAME) && hal.has(DART_FW_VERSION));

  /* E6 (run FIRST, deliberately): the 32-bit millisecond counter must be
   * allowed to wrap.  The scheduler compares times with signed differences,
   * which are valid only for a MONOTONIC counter, so this scenario walks the
   * clock through 0xFFFFFFFF one millisecond at a time instead of jumping it
   * (a discontinuous jump is not a behaviour real hardware can produce). */
  hal.now = 0xFFFFFE00UL;
  serviceAdvance(400);
  hal.clear(); feed("2 84 04 200");
  serviceAdvance(2500);
  chk(14, hal.now < 0x2000UL);              /* the counter really did wrap */
  chk(15, !core.isHeld(2, 0x84));           /* hold straddling the wrap released */
  chk(16, core.stats().txnConfirmed >= 2);  /* DOWN and UP both confirmed */
  chk(2, idle(3000));

  /* E2 Format-A tap: delivered, read, acknowledged -> confirmed, reset untouched */
  uint16_t re0 = hal.resetEdges;
  uint32_t conf0 = core.stats().txnConfirmed;
  hal.clear(); feed("1 83 03 0");
  serviceAdvance(5);
  chk(3, core.stats().reads >= 1);
  chk(4, idle(5000));
  chk(5, core.stats().txnConfirmed > conf0);
  chk(6, hal.resetHigh && hal.resetEdges == re0);

  /* E3 Format-B '<r> 99 reset' pulses the DOCUMENTED default width.
   * On the reference sketch this path passed a const char* as the int delay. */
  chk(7, idle(5000));
  hal.lastPulseMs = 0xFFFFFFFFUL;
  hal.clear(); feed("1 99 reset");
  serviceAdvance(DART_RESET_MS_DEFAULT + 200);
  emu_reset_pulse_ms = (uint16_t)hal.lastPulseMs;
  chk(8, hal.lastPulseMs == DART_RESET_MS_DEFAULT);
  chk(9, hal.resetHigh);

  /* E4 explicit bounded width */
  chk(10, idle(5000));
  hal.lastPulseMs = 0xFFFFFFFFUL;
  feed("3 reset 50"); serviceAdvance(250);
  emu_reset_pulse_50 = (uint16_t)hal.lastPulseMs;
  chk(11, hal.lastPulseMs == 50);

  /* E5 below-minimum width refused, line never driven */
  chk(12, idle(5000));
  uint16_t re1 = hal.resetEdges;
  hal.clear(); feed("4 reset 10"); serviceAdvance(200);
  chk(13, hal.resetEdges == re1);

  /* E7 two keys held at once on one remote (RF unpair needs this) */
  chk(27, idle(5000));
  hal.clear(); feed("5 8 down"); serviceAdvance(400);
  feed("5 4 down"); serviceAdvance(400);
  chk(28, core.heldCount(5) == 2);
  feed("5 86 allup"); serviceAdvance(2000);
  chk(29, core.heldCount(5) == 0);

  /* E8 stray ACK with nothing outstanding changes nothing */
  chk(19, idle(5000));
  uint32_t conf1 = core.stats().txnConfirmed;
  uint32_t stray0 = core.stats().ackStray;
  masterAck(); advance(5);
  chk(20, core.stats().ackStray > stray0);
  chk(21, core.stats().txnConfirmed == conf1);

  /* E9 AVR ABI facts */
  chk(22, emu_sizeof_int == 2);
  chk(23, emu_sizeof_ptr == 2);

  /* E10 read but never acknowledged -> uncertain, never confirmed */
  chk(24, idle(5000));
  uint32_t unc0 = core.stats().txnUncertain;
  uint32_t conf2 = core.stats().txnConfirmed;
  uint8_t k2 = 0;
  hal.clear(); feed("7 85 05 0");
  for (uint32_t i = 0; i < 4000; ++i) {
    hal.now++; core.tick();
    if (core.txnStage() == DART_TXN_PRESENTED) { masterRead(&k2); }  /* read, never ACK */
  }
  chk(25, core.stats().txnUncertain > unc0);
  chk(26, core.stats().txnConfirmed == conf2);

  emu_report();
  cli(); sleep_enable(); sleep_cpu();
  for(;;);
}
