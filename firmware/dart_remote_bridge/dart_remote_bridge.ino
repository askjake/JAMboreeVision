/* =====================================================================
 * dart_remote_bridge - JAMboreeVision DART 16-remote serial<->I2C bridge
 *
 * TARGET: Arduino Nano Every  (FQBN arduino:megaavr:nona4809, ATmega4809)
 *   Build: arduino-cli compile -b arduino:megaavr:nona4809 \
 *            firmware/dart_remote_bridge
 *   The "mode" menu option (ATMEGA328 register emulation) may be on or off;
 *   both are compiled in CI.  "off" is recommended (no emulation overhead).
 *
 * This file contains ONLY the hardware binding.  All protocol and state-machine
 * logic lives in dart_core.cpp, which is compiled unchanged by the native
 * regression harness and by the PTY simulator, so the tests exercise the real
 * firmware code rather than a reimplementation.
 *
 * See docs/DART-FIRMWARE.md for the defect list, the I2C contract and its
 * correlation limits, and the staged hardware-validation procedure.
 * ===================================================================== */

#include <Wire.h>
#include "dart_board_config.h"
#include "dart_core.h"
#include "dart_hal.h"

/* ---------------------------------------------------------------------
 * Hardware mapping.  Kept identical to the legacy DART_Dumb.ino and to
 * DART_16Remotes_* so existing DART wiring is unchanged, but now asserted
 * against the board's own pins_arduino.h instead of assumed.
 * ------------------------------------------------------------------- */
static constexpr uint8_t kChannelPin[DART_REMOTE_COUNT] = {
  12, 11, 10,  9,  8,  7,  6,  5,
   4,  3,  2, 13, PIN_A0, PIN_A1, PIN_A2, PIN_A3
};
/* ONE physical line shared by every remote circuit.  Driving it reboots all
 * sixteen; it is not a per-remote reset. */
static constexpr uint8_t kSharedResetPin = PIN_A7;

/* ---------------------- build-time hardware guards ------------------- */
#if defined(DART_BOARD_NANO_EVERY)

static_assert(NUM_DIGITAL_PINS >= 22,
  "Nano Every variant must expose digital indexes 0..21");
/* A7 must be a real PORT pin, not an ADC-only pad as on a classic ATmega328P
 * Nano, otherwise the shared reset would silently never pulse. */
static_assert(PIN_A7 < NUM_DIGITAL_PINS,
  "Shared reset pin A7 is not digital-capable on this variant");
static_assert(PIN_A3 < NUM_DIGITAL_PINS, "A3 not digital-capable");
static_assert(PIN_WIRE_SDA >= NUM_DIGITAL_PINS && PIN_WIRE_SCL >= NUM_DIGITAL_PINS,
  "This mapping assumes TWI has dedicated pin indexes outside the "
  "channel range; re-verify the channel map for this variant");

/* Compile-time validation of the channel table: in range, not the UART header
 * pins, not TWI, not the reset line, and no duplicates. */
constexpr bool dartPinUsable(uint8_t p) {
  return p >= 2 && p < NUM_DIGITAL_PINS &&
         p != PIN_WIRE_SDA && p != PIN_WIRE_SCL && p != kSharedResetPin;
}
constexpr bool dartPinsOk(const uint8_t* a, uint8_t n, uint8_t i = 0, uint8_t j = 1) {
  return i >= n ? true
       : !dartPinUsable(a[i]) ? false
       : (i + j) >= n ? dartPinsOk(a, n, i + 1, 1)
       : a[i] == a[i + j] ? false
       : dartPinsOk(a, n, i, j + 1);
}
static_assert(dartPinsOk(kChannelPin, DART_REMOTE_COUNT),
  "Channel pin map invalid: out of range, duplicated, or colliding with "
  "D0/D1 (UART header), TWI, or the shared reset pin");
static_assert(sizeof(kChannelPin) == 16, "16 channels required");

#else
#  error "dart_remote_bridge.ino supports only the Arduino Nano Every"
#endif

/* --------------------------- Arduino HAL ----------------------------- */
class ArduinoDartHal : public DartHal {
 public:
  uint32_t txDropped = 0;

  uint32_t millis() override { return ::millis(); }

  void writeChannel(uint8_t channel, bool high) override {
    if (channel >= DART_REMOTE_COUNT) return;     /* never trust an index */
    digitalWrite(kChannelPin[channel], high ? HIGH : LOW);
  }

  void writeSharedReset(bool high) override {
    digitalWrite(kSharedResetPin, high ? HIGH : LOW);
  }

  /* Never block.  The reference sketch printed unconditionally, so with debug
   * enabled a full 64-byte TX FIFO stalled the main loop and therefore the ACK
   * and release deadlines.  A message that does not fit is dropped whole, so
   * lines are never truncated, and the loss is counted and reported. */
  void print(const char* text) override {
    const size_t n = strlen(text);
    if ((size_t)Serial.availableForWrite() < n) { txDropped++; return; }
    Serial.write(reinterpret_cast<const uint8_t*>(text), n);
  }

  /* Save and RESTORE: the reference sketch called interrupts()
   * unconditionally, which enables interrupts even if they were disabled. */
  uint8_t irqSave() override { uint8_t s = SREG; cli(); return s; }
  void irqRestore(uint8_t state) override { SREG = state; }
};

static ArduinoDartHal gHal;
static DartCore gCore(gHal);

/* ----------------------------- I2C glue ------------------------------ */
static void onRequestTrampoline() {
  uint8_t buf[DART_I2C_REPLY_LEN];
  const uint8_t n = gCore.onI2cRequest(buf, sizeof(buf));
  if (n) Wire.write(buf, n);
}

static void onReceiveTrampoline(int howMany) {
  /* Bounded read: the contract is opcode + a short payload.  The reference
   * sketch read into a 64-byte stack array from a 128-byte Wire buffer. */
  uint8_t buf[8];
  uint8_t n = 0;
  int available = howMany;
  while (Wire.available() > 0) {
    const int v = Wire.read();
    if (v < 0) break;
    if (n < sizeof(buf)) buf[n++] = (uint8_t)v;   /* keep opcode + first bytes */
    else buf[sizeof(buf) - 1] = (uint8_t)v;       /* keep the LAST byte too */
  }
  (void)available;
  if (n) gCore.onI2cReceive(buf, n);
}

void setup() {
  /* Idle-HIGH every channel BEFORE enabling the output driver, so enabling the
   * driver cannot emit a spurious LOW trigger edge on any remote. */
  for (uint8_t i = 0; i < DART_REMOTE_COUNT; ++i) {
    digitalWrite(kChannelPin[i], HIGH);
    pinMode(kChannelPin[i], OUTPUT);
    digitalWrite(kChannelPin[i], HIGH);
  }
  /* Reset is active LOW; idle HIGH before driving. */
  digitalWrite(kSharedResetPin, HIGH);
  pinMode(kSharedResetPin, OUTPUT);
  digitalWrite(kSharedResetPin, HIGH);

  Serial.begin(115200);
  Wire.begin(DART_I2C_SLAVE_ADDR);
  Wire.onRequest(onRequestTrampoline);
  Wire.onReceive(onReceiveTrampoline);

  gCore.begin();
}

void loop() {
  /* Drain the UART first, then advance the engine.  Both are non-blocking, so
   * a pending release deadline is never delayed by serial traffic and vice
   * versa.  Reading a bounded batch keeps one burst from starving tick(). */
  for (uint8_t i = 0; i < 32 && Serial.available() > 0; ++i) {
    gCore.feedSerial((char)Serial.read());
  }
  gCore.tick();

  static uint32_t lastDropReport = 0;
  if (gHal.txDropped && (uint32_t)(millis() - lastDropReport) >= 5000u) {
    lastDropReport = millis();
    char buf[48];
    snprintf(buf, sizeof(buf), "[DROP] %lu log lines dropped\r\n",
             (unsigned long)gHal.txDropped);
    gHal.txDropped = 0;
    gHal.print(buf);
  }
}
