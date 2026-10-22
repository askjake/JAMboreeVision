#include "arduino_shim.h"

uint32_t shim_ms = 0;
uint32_t shim_us = 0;
int shim_pinState[32];
std::vector<std::pair<uint32_t, int> > shim_fallingEdges;
int shim_irqDepth = 0;
SerialShim Serial;
WireShim Wire;
RefMaster refMaster[16];
bool shim_mastersEnabled = true;
unsigned long shim_lastDelayRequested = 0;
bool shim_delayWasCapped = false;
unsigned long shim_delayPumpCapMs = 2000000UL;   /* ~33 min of REAL pump work, generous */
bool shim_scheduledAckArmed = false;
uint32_t shim_scheduledAckAtMs = 0;

void shim_armScheduledAck(uint32_t atMs) { shim_scheduledAckArmed = true; shim_scheduledAckAtMs = atMs; }

/* reference channel order, remote 1..16 */
static const int kRefPins[16] = {12,11,10,9,8,7,6,5,4,3,2,13,A0,A1,A2,A3};

void pinMode(int, int) {}

void digitalWrite(int pin, int value) {
  if (pin < 0 || pin >= 32) return;
  if (shim_pinState[pin] == HIGH && value == LOW)
    shim_fallingEdges.push_back(std::make_pair(shim_ms, pin));
  shim_pinState[pin] = value;
}

static int channelOfPin(int pin) {
  for (int i = 0; i < 16; ++i) if (kRefPins[i] == pin) return i;
  return -1;
}

/* Advance the virtual clock by 1 ms and let every simulated master act.
 *
 * Reentrancy guard: stepping a master can call Wire.masterWrite()/masterRead(),
 * which invoke the reference sketch's receiveEvent()/requestEvent() SYNCHRONOUSLY
 * (exactly as real hardware I2C interrupts would), and those handlers themselves
 * call micros()/millis() for logging. Without this guard that is direct
 * unbounded recursion (shim_pump -> master.step -> masterWrite -> receiveEvent
 * -> micros -> shim_pump -> ...). Real hardware has no such recursion because
 * the TWI ISR does not call millis() recursively into itself; this guard
 * reproduces that by only advancing the clock, without re-stepping masters,
 * for any pump call that happens while a pump is already in progress. */
static bool shim_pumping = false;

void shim_pump() {
  shim_ms += 1;
  shim_us += 1000;
  if (shim_pumping) return;
  if (!shim_mastersEnabled) return;
  shim_pumping = true;

  for (int c = 0; c < 16; ++c) {
    RefMaster& m = refMaster[c];
    const bool low = (shim_pinState[kRefPins[c]] == LOW);
    if (low && !m.wasLow) {
      m.wasLow = true;
      if (m.doRead) { m.pendRead = true; m.readAt = shim_ms + m.readDelay; }
    } else if (!low && m.wasLow) {
      m.wasLow = false;
    }
    if (m.pendRead && (int32_t)(shim_ms - m.readAt) >= 0) {
      m.pendRead = false;
      std::vector<byte> r = Wire.masterRead();
      m.reads++;
      if (r.size() >= 4) m.keysSeen.push_back(r[3]);
      if (m.doAck) { m.pendAck = true; m.ackAt = shim_ms + m.ackDelay; }
    }
    if (m.pendAck && (int32_t)(shim_ms - m.ackAt) >= 0) {
      m.pendAck = false;
      std::vector<byte> ack; ack.push_back(0x02);
      Wire.masterWrite(ack);
      m.acks++;
    }
    if (m.pollWhenIdle && (shim_ms - m.lastPoll) >= 25) {
      m.lastPoll = shim_ms;
      std::vector<byte> r = Wire.masterRead();
      m.reads++;
      if (r.size() >= 4) m.keysSeen.push_back(r[3]);
    }
    for (size_t i = 0; i < m.strayAcksAt.size(); ) {
      if (shim_ms >= m.strayAcksAt[i]) {
        std::vector<byte> ack; ack.push_back(0x02);
        Wire.masterWrite(ack);
        m.acks++;
        m.strayAcksAt.erase(m.strayAcksAt.begin() + (long)i);
      } else {
        ++i;
      }
    }
  }
  shim_pumping = false;
}

void shim_reset_all() {
  shim_ms = 0; shim_us = 0;
  for (int i = 0; i < 32; ++i) shim_pinState[i] = HIGH;
  shim_fallingEdges.clear();
  shim_irqDepth = 0;
  Serial.out.clear(); Serial.in.clear();
  Wire.txBuf.clear(); Wire.rxBuf.clear(); Wire.rxPos = 0;
  for (int c = 0; c < 16; ++c) refMaster[c] = RefMaster();
}

int refFallingEdges(int channel) {
  int n = 0;
  for (size_t i = 0; i < shim_fallingEdges.size(); ++i)
    if (shim_fallingEdges[i].second == kRefPins[channel]) n++;
  return n;
}
int refResetPulses() {
  int n = 0;
  for (size_t i = 0; i < shim_fallingEdges.size(); ++i)
    if (shim_fallingEdges[i].second == A7) n++;
  return n;
}
