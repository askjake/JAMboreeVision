/* dart_test_harness.h - deterministic native harness around the REAL firmware.
 *
 * This harness links the unmodified dart_core.cpp translation unit that the
 * Arduino build also compiles.  It is NOT a reimplementation of the firmware:
 * every assertion below is observing the production state machine.
 *
 * It supplies:
 *   - FakeHal       : virtual millisecond clock, channel/reset pin edge logs,
 *                     captured UART output, nesting-checked interrupt state.
 *   - RemoteMaster  : a model of one DART remote-side I2C master, including
 *                     the init exchange, interrupt-line observation, register
 *                     reads, ACK writes, configurable delays, missing reads,
 *                     missing/duplicate/early/late ACKs, unexpected polling
 *                     and remote-side reset.
 *   - DartBus       : 16 independent masters plus the stepping driver.
 */
#ifndef DART_TEST_HARNESS_H
#define DART_TEST_HARNESS_H

#include "dart_core.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <cstdint>

/* --------------------- harness self-protection --------------------------
 * A deliberately defective firmware candidate must produce a BOUNDED, clearly
 * attributed test failure, never a harness crash.  Two things are therefore
 * guaranteed here:
 *
 *   1. Every accessor the tests use to look into the remote model or the pin
 *      logs is range-checked.  An out-of-range read reports a HARNESS-FAULT
 *      and returns a sentinel that cannot compare equal to any legal value,
 *      so the assertion that depended on it still fails - it just fails
 *      cleanly instead of reading past the end of a vector.
 *   2. The observation logs cannot grow without limit.  A firmware that spins
 *      (for example one that never stops presenting a key) hits a cap, the cap
 *      is reported, and settle() stops early instead of running the full
 *      bounded window while allocating.
 *
 * None of this relaxes an assertion: for a correct candidate every accessor is
 * in range and no cap is reached, which the suite asserts explicitly at the
 * end of the run via dartHarnessFaults() and the high-water report.
 * ---------------------------------------------------------------------- */
extern int g_pass, g_fail;
extern std::vector<std::string> g_failures;
extern std::string g_currentTest;
extern int g_harnessFaults;
/* High-water marks of the observation logs, for cap-headroom reporting. */
extern size_t g_hwChannelEdges, g_hwResetEdges, g_hwOut, g_hwKeysSeen;

/* Caps are set far above anything the correct firmware produces; the
 * high-water report at the end of a run shows the actual margin. */
static const size_t DART_MAX_PIN_EDGES   = 200000;
static const size_t DART_MAX_KEYS_SEEN   = 50000;
static const size_t DART_MAX_OUT_BYTES   = 8u * 1024u * 1024u;

inline void dartHarnessFault(const char* what) {
  g_harnessFaults++;
  g_fail++;
  char m[320];
  snprintf(m, sizeof(m), "HARNESS FAULT: %s   [%s]", what, g_currentTest.c_str());
  g_failures.push_back(m);
  printf("    HARNESS-FAULT %s   [%s]\n", what, g_currentTest.c_str());
}

/* Sentinels: deliberately outside the value domain of a key byte, a register
 * byte and a millisecond timestamp, so no assertion can accidentally pass. */
#define DART_NO_BYTE (-1)
#define DART_NO_TIME (0xFFFFFFFFu)

/* ----------------------------- FakeHal ---------------------------------- */
struct PinEdge { uint32_t t; uint8_t channel; bool high; };

class FakeHal : public DartHal {
 public:
  uint32_t now = 0;
  bool channelHigh[DART_REMOTE_COUNT];
  bool resetHigh = true;
  std::vector<PinEdge> channelEdges;
  std::vector<PinEdge> resetEdges;
  std::string out;
  int irqDepth = 0;
  int maxIrqDepth = 0;
  bool irqMisuse = false;
  /* Simulated host UART capacity.  0 = unlimited. */
  size_t txCapacity = 0;

  FakeHal() { for (int i = 0; i < DART_REMOTE_COUNT; ++i) channelHigh[i] = true; }

  uint32_t millis() override { return now; }

  /* Set when any observation log hit its cap: the firmware under test is
   * producing unbounded activity.  Reported, never silently ignored. */
  bool logOverflow = false;

  void writeChannel(uint8_t ch, bool high) override {
    if (ch >= DART_REMOTE_COUNT) { irqMisuse = true; return; }
    if (channelHigh[ch] != high) {
      if (channelEdges.size() >= DART_MAX_PIN_EDGES) logOverflow = true;
      else channelEdges.push_back(PinEdge{now, ch, high});
      if (channelEdges.size() > g_hwChannelEdges) g_hwChannelEdges = channelEdges.size();
    }
    channelHigh[ch] = high;
  }
  void writeSharedReset(bool high) override {
    if (resetHigh != high) {
      if (resetEdges.size() >= DART_MAX_PIN_EDGES) logOverflow = true;
      else resetEdges.push_back(PinEdge{now, 0xFF, high});
      if (resetEdges.size() > g_hwResetEdges) g_hwResetEdges = resetEdges.size();
    }
    resetHigh = high;
  }
  void print(const char* text) override {
    if (out.size() >= DART_MAX_OUT_BYTES) { logOverflow = true; return; }
    out += text;
    if (out.size() > g_hwOut) g_hwOut = out.size();
  }

  /* Range-checked pin-log readers. */
  uint32_t channelEdgeTime(int idx) const {
    if (idx >= 0 && (size_t)idx < channelEdges.size()) return channelEdges[(size_t)idx].t;
    dartHarnessFault("FakeHal::channelEdgeTime index out of range");
    return DART_NO_TIME;
  }
  uint32_t resetEdgeTime(int idx) const {
    if (idx >= 0 && (size_t)idx < resetEdges.size()) return resetEdges[(size_t)idx].t;
    dartHarnessFault("FakeHal::resetEdgeTime index out of range");
    return DART_NO_TIME;
  }

  uint8_t irqSave() override {
    uint8_t prev = (uint8_t)(irqDepth == 0 ? 0x80 : 0x00);
    irqDepth++;
    if (irqDepth > maxIrqDepth) maxIrqDepth = irqDepth;
    return prev;
  }
  void irqRestore(uint8_t state) override {
    irqDepth--;
    if (irqDepth < 0) irqMisuse = true;
    /* A correct implementation restores; it must not blindly enable. */
    if (irqDepth == 0 && state != 0x80) irqMisuse = true;
  }

  /* helpers */
  void clearOut() { out.clear(); }
  bool sawOut(const char* needle) const { return out.find(needle) != std::string::npos; }
  int countOut(const char* needle) const {
    int n = 0; size_t p = 0; const size_t L = strlen(needle);
    while ((p = out.find(needle, p)) != std::string::npos) { n++; p += L; }
    return n;
  }
  int fallingEdges(uint8_t ch) const {
    int n = 0; for (size_t i = 0; i < channelEdges.size(); ++i)
      if (channelEdges[i].channel == ch && !channelEdges[i].high) n++;
    return n;
  }
  int resetPulses() const {
    int n = 0; for (size_t i = 0; i < resetEdges.size(); ++i)
      if (!resetEdges[i].high) n++;
    return n;
  }
};

/* --------------------------- RemoteMaster ------------------------------- */
/* Models the remote-side I2C master for exactly one DART channel. */
class RemoteMaster {
 public:
  /* behaviour switches */
  bool enabled = true;
  bool doRead = true;          /* perform the register read at all */
  bool doAck = true;           /* acknowledge after reading */
  uint32_t readDelayMs = 3;    /* falling edge -> read */
  uint32_t ackDelayMs = 2;     /* read -> ACK */
  bool duplicateAck = false;   /* send the ACK twice */
  bool pollWhenIdle = false;   /* read even with no pending trigger */
  uint32_t pollPeriodMs = 25;

  /* observations */
  int reads = 0;
  int acks = 0;
  std::vector<uint8_t> keysSeen;
  std::vector<uint8_t> lastReply;

  RemoteMaster() {}

  void attach(DartCore* core, FakeHal* hal, uint8_t channel) {
    core_ = core; hal_ = hal; channel_ = channel;
  }

  /* The remote circuit's own power-up exchange (opcode 0x01 + config byte). */
  void sendInit(uint8_t cfg = 0xAF) {
    const uint8_t buf[2] = { DART_I2C_CMD_INIT, cfg };
    core_->onI2cReceive(buf, 2);
  }
  void sendAck() {
    const uint8_t buf[1] = { DART_I2C_CMD_ACK };
    core_->onI2cReceive(buf, 1);
    acks++;
  }
  void doOneRead() {
    uint8_t buf[DART_I2C_REPLY_LEN];
    const uint8_t n = core_->onI2cRequest(buf, sizeof(buf));
    lastReply.assign(buf, buf + (n ? n : 0));
    if (n >= 4) {
      if (keysSeen.size() >= DART_MAX_KEYS_SEEN) keyOverflow = true;
      else keysSeen.push_back(buf[3]);
      if (keysSeen.size() > g_hwKeysSeen) g_hwKeysSeen = keysSeen.size();
    }
    reads++;
  }

  /* Set when this channel's key log hit its cap. */
  bool keyOverflow = false;

  /* Range-checked readers.  Out of range is a HARNESS FAULT plus a sentinel
   * that cannot equal a legal key or register byte, so the dependent
   * assertion still fails rather than reading out of bounds. */
  int keyAt(size_t i) const {
    if (i < keysSeen.size()) return (int)keysSeen[i];
    dartHarnessFault("RemoteMaster::keyAt index out of range");
    return DART_NO_BYTE;
  }
  int replyAt(size_t i) const {
    if (i < lastReply.size()) return (int)lastReply[i];
    dartHarnessFault("RemoteMaster::replyAt index out of range");
    return DART_NO_BYTE;
  }
  /* Simulate the remote circuit rebooting (it will re-init). */
  void remoteReset() { pendingRead_ = false; pendingAck_ = false; reads = 0; acks = 0; }

  void step() {
    if (!enabled) return;
    const uint32_t now = hal_->now;
    const bool low = !hal_->channelHigh[channel_];

    /* Detect the falling edge that offers a key to this channel. */
    if (low && !wasLow_) {
      wasLow_ = true;
      if (doRead) { pendingRead_ = true; readAt_ = now + readDelayMs; }
    } else if (!low && wasLow_) {
      wasLow_ = false;
    }

    if (pendingRead_ && (int32_t)(now - readAt_) >= 0) {
      pendingRead_ = false;
      doOneRead();
      if (doAck) { pendingAck_ = true; ackAt_ = now + ackDelayMs; }
    }
    if (pendingAck_ && (int32_t)(now - ackAt_) >= 0) {
      pendingAck_ = false;
      sendAck();
      if (duplicateAck) sendAck();
    }
    if (pollWhenIdle && (now - lastPoll_) >= pollPeriodMs) {
      lastPoll_ = now;
      doOneRead();
    }
  }

 private:
  DartCore* core_ = nullptr;
  FakeHal* hal_ = nullptr;
  uint8_t channel_ = 0;
  bool wasLow_ = false;
  bool pendingRead_ = false, pendingAck_ = false;
  uint32_t readAt_ = 0, ackAt_ = 0, lastPoll_ = 0;
};

/* ------------------------------ DartBus -------------------------------- */
class DartBus {
 public:
  FakeHal hal;
  DartCore core;
  RemoteMaster master[DART_REMOTE_COUNT];

  DartBus() : core(hal) {
    for (uint8_t i = 0; i < DART_REMOTE_COUNT; ++i) master[i].attach(&core, &hal, i);
  }

  void begin(bool sendInit = true) {
    core.begin();
    if (sendInit) master[0].sendInit();   /* one init exchange on the shared bus */
    hal.clearOut();
  }

  /* Feed a host command line exactly as JAMboreeVision writes it. */
  void send(const char* line) {
    for (const char* p = line; *p; ++p) core.feedSerial(*p);
    core.feedSerial('\n');
  }
  void sendRaw(const char* s) { for (const char* p = s; *p; ++p) core.feedSerial(*p); }

  /* Advance virtual time in 1 ms steps, stepping every master and the core. */
  void advance(uint32_t ms) {
    for (uint32_t i = 0; i < ms; ++i) {
      core.tick();
      for (uint8_t c = 0; c < DART_REMOTE_COUNT; ++c) master[c].step();
      core.tick();
      hal.now++;
    }
    core.tick();
  }
  /* True while any key still has a firmware-owned release deadline pending.
   * Host-managed holds (0xFFFF) are excluded: those are released by the host. */
  bool releasePending() const {
    for (uint8_t r = 1; r <= DART_REMOTE_COUNT; ++r)
      for (uint8_t s = 0; s < DART_HELD_SLOTS; ++s) {
        const DartHeldKey& k = core.held(r, s);
        if (k.state != DART_KEY_FREE && k.holdMs != DART_HOLD_HOST_MANAGED) return true;
      }
    return false;
  }

  /* Settle: run until nothing is outstanding - no transaction, no queued
   * command, no reset pulse and no scheduled release.  Bounded. */
  /* True when any observation log hit its cap on this bus. */
  bool logOverflowed() const {
    if (hal.logOverflow) return true;
    for (uint8_t c = 0; c < DART_REMOTE_COUNT; ++c)
      if (master[c].keyOverflow) return true;
    return false;
  }

  void settle(uint32_t maxMs = 40000) {
    for (uint32_t i = 0; i < maxMs; ++i) {
      if (logOverflowed()) {
        dartHarnessFault("firmware produced unbounded activity; settle() aborted "
                         "at an observation-log cap");
        return;
      }
      if (core.txnStage() == DART_TXN_IDLE && core.queueDepth() == 0 &&
          !core.resetting() && !releasePending()) {
        advance(2);
        if (core.txnStage() == DART_TXN_IDLE && core.queueDepth() == 0 &&
            !releasePending()) return;
      }
      advance(1);
    }
  }
  void allMastersIdeal() {
    for (uint8_t c = 0; c < DART_REMOTE_COUNT; ++c) {
      master[c].doRead = true; master[c].doAck = true;
      master[c].duplicateAck = false; master[c].pollWhenIdle = false;
      master[c].readDelayMs = 3; master[c].ackDelayMs = 2;
    }
  }
};

/* --------------------------- tiny test runner --------------------------- */

#define CHECK(cond)                                                            \
  do {                                                                         \
    if (cond) { g_pass++; }                                                    \
    else {                                                                     \
      g_fail++;                                                                \
      char dart__msg[400];                                                             \
      snprintf(dart__msg, sizeof(dart__msg), "%s:%d  %s   [%s]", __FILE__,     \
               __LINE__, #cond, g_currentTest.c_str());                        \
      g_failures.push_back(dart__msg);                                                 \
      printf("    FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);               \
    }                                                                          \
  } while (0)

#define CHECK_EQ(a, b)                                                         \
  do {                                                                         \
    long long va = (long long)(a), vb = (long long)(b);  /* NOLINT */                         \
    if (va == vb) { g_pass++; }                                                \
    else {                                                                     \
      g_fail++;                                                                \
      char dart__msg[400];                                                            \
      snprintf(dart__msg, sizeof(dart__msg), "%s:%d  %s == %s (%lld vs %lld) [%s]",\
               __FILE__, __LINE__, #a, #b, va, vb, g_currentTest.c_str());     \
      g_failures.push_back(dart__msg);                                                \
      printf("    FAIL %s:%d  %s == %s  (%lld vs %lld)\n", __FILE__, __LINE__, \
             #a, #b, va, vb);                                                  \
    }                                                                          \
  } while (0)

#define TEST(name)                                                             \
  do { g_currentTest = name; printf("  [test] %s\n", name); } while (0)

#endif /* DART_TEST_HARNESS_H */
