/* dart_core.cpp - portable DART bridge logic.  See dart_core.h for the
 * protocol analysis that constrains the correlation strategy. */

#include "dart_core.h"
#include "dart_version.h"

#include <string.h>
#include <stdio.h>

#if defined(ARDUINO)
#include <avr/pgmspace.h>
#define DART_TABLE PROGMEM
#define DART_TBL_RD(p) pgm_read_byte(p)
#else
#define DART_TABLE
#define DART_TBL_RD(p) (*(p))
#endif

/* --------------------------------------------------------------------------
 * Button table.  Flash-resident pairs of {down, up}.  The reference sketch
 * stored 80 "const char*" pointers to ASCII literals and re-parsed them with
 * strtoul() on every press; on AVR that costs SRAM for both the pointer array
 * and the literals and accepts garbage silently.
 * -------------------------------------------------------------------------- */
static const uint8_t kButtonCodes[40][2] DART_TABLE = {
  {0x81,0x01},{0x8C,0x0C},{0x8B,0x0B},{0x83,0x03},{0x95,0x15},{0x96,0x16},
  {0x97,0x17},{0x98,0x18},{0x99,0x19},{0x9A,0x1A},{0xA2,0x22},{0xA3,0x23},
  {0xA4,0x24},{0x9F,0x1F},{0xA0,0x20},{0xA1,0x21},{0xA9,0x29},{0xAA,0x2A},
  {0xAB,0x2B},{0xAC,0x2C},{0xAD,0x2D},{0xAE,0x2E},{0xB6,0x36},{0xB7,0x37},
  {0xB8,0x38},{0xB3,0x33},{0xB4,0x34},{0xB5,0x35},{0xBD,0x3D},{0xBE,0x3E},
  {0xBF,0x3F},{0xC0,0x40},{0xC1,0x41},{0xC2,0x42},{0xEF,0x6F},{0xF0,0x70},
  {0xF1,0x71},{0xF2,0x72},{0x8C,0x8B},{0x0B,0x03}
};

bool dartButtonCodes(uint8_t buttonNum, uint8_t* downCmd, uint8_t* upCmd) {
  if (buttonNum < 1 || buttonNum > 40) return false;
  *downCmd = (uint8_t)DART_TBL_RD(&kButtonCodes[buttonNum - 1][0]);
  *upCmd   = (uint8_t)DART_TBL_RD(&kButtonCodes[buttonNum - 1][1]);
  return true;
}

/* -------------------------- strict scalar parsing ------------------------ */
/* The reference sketch used String::toInt() and strtoul(), which map "abc",
 * "", "7x" and "0xZZ" to 0 with no error.  A silent 0 then reached
 * convertNumToPin()/the button table and the reset line. */

static bool parseDecU32(const char* s, uint32_t maxVal, uint32_t* out) {
  if (s == 0 || *s == 0) return false;
  uint32_t v = 0;
  for (const char* p = s; *p; ++p) {
    if (*p < '0' || *p > '9') return false;
    uint32_t digit = (uint32_t)(*p - '0');
    if (v > (0xFFFFFFFFu - digit) / 10u) return false;   /* overflow */
    v = v * 10u + digit;
    if (v > maxVal) return false;
  }
  *out = v;
  return true;
}

static int hexVal(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

static bool parseHexByteStrict(const char* s, uint8_t* out) {
  if (s == 0) return false;
  if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) s += 2;
  if (s[0] == 0) return false;
  uint16_t v = 0;
  uint8_t n = 0;
  for (const char* p = s; *p; ++p) {
    int d = hexVal(*p);
    if (d < 0) return false;
    if (++n > 2) return false;
    v = (uint16_t)(v << 4) | (uint16_t)d;
  }
  *out = (uint8_t)v;
  return true;
}

static bool ieq(const char* a, const char* b) {
  for (;; ++a, ++b) {
    char ca = *a, cb = *b;
    if (ca >= 'A' && ca <= 'Z') ca = (char)(ca + 32);
    if (cb >= 'A' && cb <= 'Z') cb = (char)(cb + 32);
    if (ca != cb) return false;
    if (ca == 0) return true;
  }
}

/* ----------------------------- construction ----------------------------- */
DartCore::DartCore(DartHal& hal)
    : hal_(hal),
      txnStage_(DART_TXN_IDLE), txnKey_(DART_I2C_KEY_NONE),
      clearInterrupt_(false), initSeen_(false), cfgReg_(0xAF),
      isrReads_(0), isrAckEarly_(0), isrAckStray_(0), isrAckDuplicate_(0),
      isrInitExchanges_(0),
      txnRemote_(0), txnPurpose_(DART_PURPOSE_DOWN), txnSlot_(-1), txnTag_(0),
      txnTriggerTry_(0), txnDeadline_(0), txnPulseActive_(false), txnPulseUntil_(0), nextWorkAt_(0),
      resetUntil_(0), resetting_(false),
      qHead_(0), qTail_(0), lineLen_(0), lineOverflow_(false),
      debug_(false), ackTimeoutMs_(DART_ACK_TIMEOUT_MS_DEFAULT),
      triggerRetries_(DART_TRIGGER_RETRIES_DEFAULT) {
  memset(held_, 0, sizeof(held_));
  memset(queue_, 0, sizeof(queue_));
  memset(&stats_, 0, sizeof(stats_));
  line_[0] = 0;
}

void DartCore::begin() {
  /* Seed every deadline from the live clock.  Initialising a deadline to the
   * constant 0 (as the reference sketch did with its lastTxTs/holdStart
   * sentinels) makes behaviour depend on the absolute value of millis() at
   * start-up and stalls the engine for up to 2^31 ms if it is not near zero. */
  nextWorkAt_ = hal_.millis();
  txnDeadline_ = nextWorkAt_;
  txnPulseUntil_ = nextWorkAt_;
  resetUntil_ = nextWorkAt_;

#if DART_RESET_ON_BOOT
  /* Compile-time opt-in only.  See dart_board_config.h for why this is OFF by
   * default: an MCU-only reset must not routinely reboot all 16 remotes. */
  pulseSharedReset(DART_RESET_MS_DEFAULT);
#endif
  printBanner();
}

/* ------------------------------- banner --------------------------------- */
void DartCore::printBanner() {
  char buf[96];
  snprintf(buf, sizeof(buf),
           "%s %s %s board=%s remotes=%u slots=%u\r\n",
           DART_FW_NAME, DART_FW_VERSION, DART_FW_DATE, DART_BOARD_NAME,
           (unsigned)DART_REMOTE_COUNT, (unsigned)DART_HELD_SLOTS);
  hal_.print(buf);
  snprintf(buf, sizeof(buf),
           "serial-proto=%s i2c-proto=%s addr=0x%02X reply=%u type 'help'\r\n",
           DART_SERIAL_PROTO, DART_I2C_PROTO,
           (unsigned)DART_I2C_SLAVE_ADDR, (unsigned)DART_I2C_REPLY_LEN);
  hal_.print(buf);
}

void DartCore::printHelp() {
  hal_.print("commands:\r\n");
  hal_.print("  <r> <downHex> <upHex> <holdMs>   format A, hold 0..30000\r\n");
  hal_.print("  <r> <btn> down | <r> <btn> up    btn 1..40, host-managed hold\r\n");
  hal_.print("  <r> 86 allup                     release every tracked key\r\n");
  hal_.print("  <r> 99 reset | <r> reset [ms]    SHARED reset line: all remotes\r\n");
  hal_.print("  reset                            same shared reset line\r\n");
  hal_.print("  version | help | status | qstat\r\n");
  hal_.print("  debug on | debug off | ack <ms> | retries <n>\r\n");
  hal_.print("  any command may end with #<id> to echo a correlation id\r\n");
  hal_.print("note: reset drives ONE physical line shared by all 16 remotes\r\n");
  hal_.print("note: 'confirmed' means read+ACK on I2C, NOT receiver/STB action\r\n");
}

void DartCore::err(const char* code, const char* detail) {
  char buf[80];
  snprintf(buf, sizeof(buf), "[ERR] %s %s\r\n", code, detail ? detail : "");
  hal_.print(buf);
  stats_.cmdRejected++;
}

/* --------------------------- serial line input -------------------------- */
void DartCore::feedSerial(char c) {
  if (c == '\r') return;
  if (c != '\n') {
    if (lineLen_ >= DART_LINE_MAX) {
      /* Bounded buffer. The reference sketch appended to an Arduino String
       * with no limit, so a host that never sent a newline grew the heap
       * until allocation failed. Mark and discard to the next newline. */
      lineOverflow_ = true;
      return;
    }
    line_[lineLen_++] = c;
    return;
  }
  if (lineOverflow_) {
    stats_.lineOverflows++;
    lineOverflow_ = false;
    lineLen_ = 0;
    err("line-too-long", "max 72 chars");
    return;
  }
  line_[lineLen_] = 0;
  uint8_t len = lineLen_;
  lineLen_ = 0;
  if (len) handleLine(line_, len);
}

void DartCore::handleLine(char* line, uint8_t len) {
  (void)len;
  char tok[DART_TOKEN_MAX][DART_TOKEN_LEN];
  uint8_t ntok = 0;
  uint16_t tag = 0;

  const char* p = line;
  bool tooManyTokens = false;
  while (*p) {
    while (*p == ' ' || *p == '\t') ++p;
    if (!*p) break;
    const char* start = p;
    while (*p && *p != ' ' && *p != '\t') ++p;
    uint8_t tlen = (uint8_t)(p - start);
    if (ntok >= DART_TOKEN_MAX) { tooManyTokens = true; break; }
    if (tlen >= DART_TOKEN_LEN) { err("token-too-long", start); return; }
    memcpy(tok[ntok], start, tlen);
    tok[ntok][tlen] = 0;
    ntok++;
  }
  if (tooManyTokens) { err("too-many-tokens", line); return; }
  if (ntok == 0) return;

  /* Optional trailing "#<id>" correlation tag.  Additive and backward
   * compatible: JAMboreeVision never sends it, and omitting it echoes id=0. */
  if (tok[ntok - 1][0] == '#') {
    uint32_t v = 0;
    if (!parseDecU32(tok[ntok - 1] + 1, 65535u, &v)) { err("bad-tag", tok[ntok - 1]); return; }
    tag = (uint16_t)v;
    ntok--;
    if (ntok == 0) { err("tag-only", line); return; }
  }

  if (debug_) {
    char buf[DART_LINE_MAX + 16];
    snprintf(buf, sizeof(buf), "[SER] %s\r\n", line);
    hal_.print(buf);
  }
  if (!dispatchTokens(tok, ntok, tag)) {
    /* dispatchTokens already reported the specific reason when it could. */
  }
}

bool DartCore::dispatchTokens(char tok[DART_TOKEN_MAX][DART_TOKEN_LEN],
                              uint8_t ntok, uint16_t tag) {
  /* ---- console / introspection ---- */
  if (ntok == 1) {
    if (ieq(tok[0], "version") || ieq(tok[0], "ver")) { printBanner(); return true; }
    if (ieq(tok[0], "help") || ieq(tok[0], "?"))      { printHelp();   return true; }
    if (ieq(tok[0], "status"))                        { printStatus(); return true; }
    if (ieq(tok[0], "qstat"))                         { printQstat();  return true; }
    if (ieq(tok[0], "reset")) {
      DartCommand c; memset(&c, 0, sizeof(c));
      c.kind = DART_CMD_SHARED_RESET; c.remote = 0;
      c.holdMs = DART_RESET_MS_DEFAULT; c.tag = tag;
      return enqueue(c);
    }
  }
  if (ntok == 2 && ieq(tok[0], "debug")) {
    if (ieq(tok[1], "on"))  { debug_ = true;  hal_.print("[DBG] on\r\n");  return true; }
    if (ieq(tok[1], "off")) { debug_ = false; hal_.print("[DBG] off\r\n"); return true; }
    err("bad-debug-arg", tok[1]); return false;
  }
  if (ntok == 2 && ieq(tok[0], "ack")) {
    uint32_t v;
    if (!parseDecU32(tok[1], DART_ACK_TIMEOUT_MS_MAX, &v) || v < DART_ACK_TIMEOUT_MS_MIN) {
      err("bad-ack-timeout", tok[1]); return false;
    }
    ackTimeoutMs_ = v;
    char buf[48]; snprintf(buf, sizeof(buf), "[ACK] timeout=%lu\r\n", (unsigned long)v);
    hal_.print(buf); return true;
  }
  if (ntok == 2 && ieq(tok[0], "retries")) {
    uint32_t v;
    if (!parseDecU32(tok[1], DART_TRIGGER_RETRIES_MAX, &v)) { err("bad-retries", tok[1]); return false; }
    triggerRetries_ = (uint8_t)v;
    char buf[48]; snprintf(buf, sizeof(buf), "[RET] retries=%u\r\n", (unsigned)v);
    hal_.print(buf); return true;
  }

  /* Every remaining form starts with a remote number.  Validate it once,
   * strictly, BEFORE anything can touch a pin or the shared reset line. */
  uint32_t remote = 0;
  if (!parseDecU32(tok[0], 0xFFFFFFFFu, &remote) ||
      remote < 1 || remote > DART_REMOTE_COUNT) {
    err("bad-remote", tok[0]);
    return false;
  }
  DartCommand c; memset(&c, 0, sizeof(c));
  c.remote = (uint8_t)remote;
  c.tag = tag;

  /* ---- "<r> reset [ms]" ---- */
  if (ntok >= 2 && ieq(tok[1], "reset")) {
    uint32_t ms = DART_RESET_MS_DEFAULT;
    if (ntok >= 3) {
      if (!parseDecU32(tok[2], DART_RESET_MS_MAX, &ms) || ms < DART_RESET_MS_MIN) {
        err("bad-reset-ms", tok[2]); return false;
      }
    }
    if (ntok > 3) { err("reset-extra-args", tok[3]); return false; }
    c.kind = DART_CMD_SHARED_RESET; c.holdMs = (uint16_t)ms;
    return enqueue(c);
  }

  /* ---- Format A: "<r> <downHex> <upHex> <holdMs>" ---- */
  if (ntok == 4) {
    uint8_t dn, up; uint32_t hold;
    if (!parseHexByteStrict(tok[1], &dn)) { err("bad-down-hex", tok[1]); return false; }
    if (!parseHexByteStrict(tok[2], &up)) { err("bad-up-hex", tok[2]);   return false; }
    if (!parseDecU32(tok[3], DART_HOLD_MS_MAX, &hold)) { err("bad-hold-ms", tok[3]); return false; }
    if (dn == DART_I2C_KEY_NONE) { err("down-key-zero", tok[1]); return false; }
    c.kind = DART_CMD_FORMAT_A; c.downCmd = dn; c.upCmd = up; c.holdMs = (uint16_t)hold;
    return enqueue(c);
  }

  /* ---- Format B: "<r> <btn> down|up|allup|reset" ---- */
  if (ntok == 3) {
    uint32_t btn = 0;
    if (!parseDecU32(tok[1], 0xFFFFFFFFu, &btn)) { err("bad-button", tok[1]); return false; }
    const char* act = tok[2];

    if (ieq(act, "allup") || btn == 86) {
      c.kind = DART_CMD_RELEASE_ALL;
      return enqueue(c);
    }
    if (ieq(act, "reset") || btn == 99) {
      c.kind = DART_CMD_SHARED_RESET; c.holdMs = DART_RESET_MS_DEFAULT;
      return enqueue(c);
    }
    uint8_t dn, up;
    if (btn > 255 || !dartButtonCodes((uint8_t)btn, &dn, &up)) { err("bad-button", tok[1]); return false; }
    if (ieq(act, "down")) { c.kind = DART_CMD_KEY_DOWN; }
    else if (ieq(act, "up")) { c.kind = DART_CMD_KEY_UP; }
    else { err("bad-action", act); return false; }
    c.downCmd = dn; c.upCmd = up; c.holdMs = DART_HOLD_HOST_MANAGED;
    return enqueue(c);
  }

  err("unparsed", tok[0]);
  return false;
}

/* ------------------------------ queueing -------------------------------- */
bool DartCore::enqueue(const DartCommand& cmd) {
  uint8_t next = (uint8_t)((qHead_ + 1) % DART_CMD_QUEUE_LEN);
  if (next == qTail_) {
    /* Explicit rejection, no corruption and no silent drop. */
    stats_.cmdDropped++;
    char buf[48];
    snprintf(buf, sizeof(buf), "[BUSY] queue full r=%u\r\n", (unsigned)cmd.remote);
    hal_.print(buf);
    return false;
  }
  queue_[qHead_] = cmd;
  qHead_ = next;
  stats_.cmdAccepted++;
  return true;
}

bool DartCore::dequeue(DartCommand& cmd) {
  if (qTail_ == qHead_) return false;
  cmd = queue_[qTail_];
  qTail_ = (uint8_t)((qTail_ + 1) % DART_CMD_QUEUE_LEN);
  return true;
}

uint8_t DartCore::queueDepth() const {
  return (uint8_t)((qHead_ + DART_CMD_QUEUE_LEN - qTail_) % DART_CMD_QUEUE_LEN);
}

/* --------------------------- held-key helpers --------------------------- */
int8_t DartCore::findSlot(uint8_t remote1, uint8_t downCmd) const {
  for (uint8_t s = 0; s < DART_HELD_SLOTS; ++s)
    if (held_[remote1 - 1][s].state != DART_KEY_FREE &&
        held_[remote1 - 1][s].downCmd == downCmd) return (int8_t)s;
  return -1;
}

int8_t DartCore::findFreeSlot(uint8_t remote1) const {
  for (uint8_t s = 0; s < DART_HELD_SLOTS; ++s)
    if (held_[remote1 - 1][s].state == DART_KEY_FREE) return (int8_t)s;
  return -1;
}

uint8_t DartCore::heldCount(uint8_t remote1) const {
  uint8_t n = 0;
  for (uint8_t s = 0; s < DART_HELD_SLOTS; ++s)
    if (held_[remote1 - 1][s].state != DART_KEY_FREE) n++;
  return n;
}

bool DartCore::isHeld(uint8_t remote1, uint8_t downCmd) const {
  return findSlot(remote1, downCmd) >= 0;
}

void DartCore::clearRemote(uint8_t remote1) {
  memset(&held_[remote1 - 1][0], 0, sizeof(held_[remote1 - 1]));
}

void DartCore::clearAllRemotes() {
  memset(held_, 0, sizeof(held_));
}

/* ------------------------------ reporting ------------------------------- */
/* The key/remote/purpose are passed explicitly because finishTxn() must stop
 * presenting the key (clearing txnKey_) BEFORE it reports, so that no stale
 * key can be read while the report is being formatted and emitted. */
void DartCore::emitOutcome(uint8_t outcome, uint8_t remote1, uint8_t key,
                           uint8_t purpose, uint16_t tag) {
  static const char* kNames[3] = { "confirmed", "uncertain", "failed" };
  static const char* kPurpose[3] = { "down", "up", "allup" };
  char buf[96];
  snprintf(buf, sizeof(buf),
           "[EVT] r=%u key=0x%02X act=%s result=%s id=%u\r\n",
           (unsigned)remote1, (unsigned)key,
           kPurpose[purpose <= 2 ? purpose : 0],
           kNames[outcome <= 2 ? outcome : 2], (unsigned)tag);
  hal_.print(buf);
}

/* Publish the interrupt-owned counters into the main-loop snapshot. */
void DartCore::syncIsrCounters() {
  DartCritical cs(hal_);
  stats_.reads = isrReads_;
  stats_.ackEarly = isrAckEarly_;
  stats_.ackStray = isrAckStray_;
  stats_.ackDuplicate = isrAckDuplicate_;
  stats_.initExchanges = isrInitExchanges_;
}

void DartCore::printStatus() {
  char buf[96];
  snprintf(buf, sizeof(buf), "[STATUS] stage=%u queue=%u init=%u debug=%u ack=%lu\r\n",
           (unsigned)txnStage_, (unsigned)queueDepth(), (unsigned)(initSeen_ ? 1 : 0),
           (unsigned)(debug_ ? 1 : 0), (unsigned long)ackTimeoutMs_);
  hal_.print(buf);
  for (uint8_t r = 1; r <= DART_REMOTE_COUNT; ++r) {
    if (!heldCount(r)) continue;
    for (uint8_t s = 0; s < DART_HELD_SLOTS; ++s) {
      const DartHeldKey& k = held_[r - 1][s];
      if (k.state == DART_KEY_FREE) continue;
      snprintf(buf, sizeof(buf),
               "[HELD] r=%u down=0x%02X up=0x%02X state=%s hold=%u age=%lu\r\n",
               (unsigned)r, (unsigned)k.downCmd, (unsigned)k.upCmd,
               k.state == DART_KEY_CONFIRMED ? "confirmed" : "uncertain",
               (unsigned)k.holdMs, (unsigned long)(hal_.millis() - k.holdStart));
      hal_.print(buf);
    }
  }
}

void DartCore::printQstat() {
  syncIsrCounters();
  const uint32_t reads = stats_.reads, early = stats_.ackEarly;
  const uint32_t stray = stats_.ackStray, dup = stats_.ackDuplicate;
  const uint32_t inits = stats_.initExchanges;

  char buf[128];
  snprintf(buf, sizeof(buf),
           "[Q] queue=%u/%u acc=%lu rej=%lu drop=%lu\r\n",
           (unsigned)queueDepth(), (unsigned)(DART_CMD_QUEUE_LEN - 1),
           (unsigned long)stats_.cmdAccepted, (unsigned long)stats_.cmdRejected,
           (unsigned long)stats_.cmdDropped);
  hal_.print(buf);
  snprintf(buf, sizeof(buf),
           "[Q] ok=%lu uncertain=%lu failed=%lu wdog=%lu rst=%lu\r\n",
           (unsigned long)stats_.txnConfirmed, (unsigned long)stats_.txnUncertain,
           (unsigned long)stats_.txnFailed, (unsigned long)stats_.watchdogReleases,
           (unsigned long)stats_.sharedResets);
  hal_.print(buf);
  snprintf(buf, sizeof(buf),
           "[Q] reads=%lu ackEarly=%lu ackStray=%lu ackDup=%lu init=%lu ovf=%lu\r\n",
           (unsigned long)reads, (unsigned long)early, (unsigned long)stray,
           (unsigned long)dup, (unsigned long)inits,
           (unsigned long)stats_.lineOverflows);
  hal_.print(buf);
}

/* --------------------------- transaction engine ------------------------- */
void DartCore::beginTxn(uint8_t remote1, uint8_t key, uint8_t purpose,
                        uint8_t slot, uint16_t tag) {
  txnRemote_ = remote1;
  txnPurpose_ = purpose;
  txnSlot_ = (int8_t)slot;
  txnTag_ = tag;

  {
    DartCritical cs(hal_);
    txnKey_ = key;
    txnStage_ = DART_TXN_PULSE_LOW;
  }
  /* Pulse the channel's interrupt line LOW -> HIGH.  Non-blocking: the HIGH
   * edge happens in tick() once txnPulseUntil_ elapses, so a 2 ms pulse never
   * delays serial handling or another channel's release deadline. */
  txnPulseUntil_ = hal_.millis() + DART_PULSE_LOW_MS;
  txnDeadline_ = txnPulseUntil_ + ackTimeoutMs_;
  txnPulseActive_ = true;
  hal_.writeChannel((uint8_t)(remote1 - 1), false);

  if (debug_) {
    char buf[72];
    snprintf(buf, sizeof(buf), "[TRIG] r=%u key=0x%02X try=%u\r\n",
             (unsigned)remote1, (unsigned)key, (unsigned)(txnTriggerTry_ + 1));
    hal_.print(buf);
  }
}

void DartCore::cancelTxnPresentation() {
  /* Stop presenting the key immediately.  This is the single most important
   * difference from the reference sketch: there is no residual queued event
   * that a later read by ANOTHER remote could consume. */
  if (txnPulseActive_) {
    hal_.writeChannel((uint8_t)(txnRemote_ - 1), true);
    txnPulseActive_ = false;
  }
  DartCritical cs(hal_);
  txnStage_ = DART_TXN_IDLE;
  txnKey_ = DART_I2C_KEY_NONE;
}

void DartCore::finishTxn(uint8_t outcome) {
  const uint8_t purpose = txnPurpose_;
  const int8_t  slot    = txnSlot_;
  const uint8_t remote  = txnRemote_;
  const uint8_t key     = txnKey_;
  const uint16_t tag    = txnTag_;

  cancelTxnPresentation();

  if (outcome == DART_OUTCOME_CONFIRMED) stats_.txnConfirmed++;
  else if (outcome == DART_OUTCOME_UNCERTAIN) stats_.txnUncertain++;
  else stats_.txnFailed++;

  if (purpose == DART_PURPOSE_DOWN) {
    if (slot >= 0) {
      DartHeldKey& k = held_[remote - 1][slot];
      if (outcome == DART_OUTCOME_CONFIRMED) {
        k.state = DART_KEY_CONFIRMED;
        k.holdStart = hal_.millis();
        k.upAttempts = 0;
      } else if (outcome == DART_OUTCOME_UNCERTAIN) {
        /* The master READ the key, so the remote may now be holding it.  We
         * must not replay the DOWN (that could double-press) and we must not
         * forget it (that would leave a key held forever).  Track it as
         * possibly-held so release-all and the watchdog both cover it. */
        k.state = DART_KEY_UNCERTAIN;
        k.holdStart = hal_.millis();
        k.upAttempts = 0;
      } else {
        /* Never read: nothing was delivered, so nothing can be held. */
        k.state = DART_KEY_FREE;
      }
    }
  } else if (purpose == DART_PURPOSE_UP || purpose == DART_PURPOSE_RELEASE_ALL) {
    if (slot >= 0) {
      DartHeldKey& k = held_[remote - 1][slot];
      if (outcome == DART_OUTCOME_CONFIRMED) {
        /* Released.  Clearing the slot here is what prevents the reference
         * sketch's duplicate UP on a zero-duration Format A command. */
        k.state = DART_KEY_FREE;
      } else {
        k.upAttempts++;
        if (k.upAttempts >= DART_RELEASE_RETRIES_MAX) {
          /* Bounded.  Stop retrying and say so instead of looping forever or
           * resetting every remote because one release failed.
           * REGRESSION #8: do NOT force the slot to DART_KEY_FREE here. The
           * release itself was never confirmed, so we do not know the key is
           * actually up; forcing FREE previously let a fresh DOWN reuse the
           * slot (and a duplicate DOWN report "confirmed") while the physical
           * remote might still be holding the old key. Leave k.state as-is
           * (CONFIRMED or UNCERTAIN) so findSlot()/duplicate-DOWN reporting
           * and release-all keep treating it as held pending an explicit
           * "<r> 99 reset". startReleaseWork() stops auto-retrying it once
           * upAttempts reaches the bound (see matching guard there). */
          char buf[96];
          snprintf(buf, sizeof(buf),
                   "[WARN] r=%u up=0x%02X unreleased after %u tries; "
                   "use '%u 99 reset' (SHARED line) if needed\r\n",
                   (unsigned)remote, (unsigned)k.upCmd,
                   (unsigned)k.upAttempts, (unsigned)remote);
          hal_.print(buf);
        } else {
          /* Re-arm: due immediately, paced by nextWorkAt_. */
          k.holdMs = 0;
          k.holdStart = hal_.millis();
        }
      }
    }
  }

  emitOutcome(outcome, remote, key, purpose, tag);
  txnSlot_ = -1;
  txnTriggerTry_ = 0;
  nextWorkAt_ = hal_.millis() + DART_RETRY_GAP_MS;
}

bool DartCore::startReleaseWork() {
  const uint32_t now = hal_.millis();
  for (uint8_t r = 1; r <= DART_REMOTE_COUNT; ++r) {
    for (uint8_t s = 0; s < DART_HELD_SLOTS; ++s) {
      DartHeldKey& k = held_[r - 1][s];
      if (k.state == DART_KEY_FREE) continue;
      /* REGRESSION #8 (companion guard): a slot whose release retries were
       * already exhausted (see finishTxn) stays tracked as held/uncertain on
       * purpose, but must not be auto-retried again here - only an explicit
       * "<r> 99 reset" or a fresh release-all (which clears upAttempts) may
       * re-arm it. Without this guard the slot would be picked up again
       * immediately, which is exactly the unbounded retry loop the original
       * exhaustion check was meant to prevent. */
      if (k.upAttempts >= DART_RELEASE_RETRIES_MAX) continue;
      /* Unsigned difference: correct across the 2^32 ms wraparound. */
      const uint32_t age = now - k.holdStart;
      uint32_t limit;
      bool watchdog = false;
      if (k.holdMs == DART_HOLD_HOST_MANAGED) {
        limit = DART_HOST_HOLD_WATCHDOG_MS;
        watchdog = true;
      } else {
        limit = k.holdMs;
      }
      if (age < limit) continue;
      if (watchdog) {
        stats_.watchdogReleases++;
        char buf[88];
        snprintf(buf, sizeof(buf),
                 "[WDOG] r=%u up=0x%02X abandoned after %lums; releasing\r\n",
                 (unsigned)r, (unsigned)k.upCmd, (unsigned long)age);
        hal_.print(buf);
      }
      beginTxn(r, k.upCmd, DART_PURPOSE_UP, s, 0);
      return true;
    }
  }
  return false;
}

bool DartCore::startQueuedCommand() {
  DartCommand c;
  while (dequeue(c)) {
    switch (c.kind) {
      case DART_CMD_SHARED_RESET:
        pulseSharedReset(c.holdMs);
        return true;   /* occupies the engine until the pulse completes */

      case DART_CMD_RELEASE_ALL: {
        /* Release the keys actually tracked for this remote - including the
         * uncertain ones.  The reference sketch sent a single fixed 0x03
         * (Guide UP) and the legacy sketch sent a single fixed 0x01
         * (Power UP); neither releases what is really held. */
        uint8_t n = heldCount(c.remote);
        if (n == 0) {
          char buf[64];
          snprintf(buf, sizeof(buf), "[EVT] r=%u act=allup result=confirmed id=%u\r\n",
                   (unsigned)c.remote, (unsigned)c.tag);
          hal_.print(buf);
          continue;
        }
        /* Make every tracked key due now; the release scheduler drains them
         * one transaction at a time, which keeps correlation sound. */
        for (uint8_t s = 0; s < DART_HELD_SLOTS; ++s) {
          DartHeldKey& k = held_[c.remote - 1][s];
          if (k.state == DART_KEY_FREE) continue;
          k.holdMs = 0;
          k.holdStart = hal_.millis();
          k.upAttempts = 0;
        }
        return startReleaseWork();
      }

      case DART_CMD_KEY_UP: {
        int8_t slot = findSlot(c.remote, c.downCmd);
        if (slot < 0) {
          /* Duplicate/unsolicited UP.  Still transmit the release once (the
           * remote may be holding the key from a lost DOWN) but do not invent
           * a held-key record for it. */
          slot = findFreeSlot(c.remote);
          if (slot < 0) { err("held-slots-full", "up"); continue; }
          DartHeldKey& k = held_[c.remote - 1][slot];
          k.downCmd = c.downCmd; k.upCmd = c.upCmd;
          k.state = DART_KEY_UNCERTAIN; k.holdMs = 0;
          k.holdStart = hal_.millis(); k.upAttempts = 0;
        } else {
          DartHeldKey& k = held_[c.remote - 1][slot];
          k.holdMs = 0; k.holdStart = hal_.millis(); k.upAttempts = 0;
        }
        beginTxn(c.remote, c.upCmd, DART_PURPOSE_UP, (uint8_t)slot, c.tag);
        return true;
      }

      case DART_CMD_FORMAT_A:
      case DART_CMD_KEY_DOWN: {
        int8_t slot = findSlot(c.remote, c.downCmd);
        if (slot >= 0) {
          /* Duplicate DOWN for a key already held: refresh the deadline
           * instead of pressing twice or rejecting the whole command.
           * REGRESSION #9: findSlot() matches CONFIRMED and UNCERTAIN alike,
           * so report the slot's actual state instead of hardcoding
           * "confirmed" - an uncertain key must stay reported as uncertain
           * even on a duplicate DOWN, or the host loses the only signal that
           * the original press was never acknowledged. */
          DartHeldKey& k = held_[c.remote - 1][slot];
          k.holdMs = c.holdMs;
          k.holdStart = hal_.millis();
          const char* dupResult = (k.state == DART_KEY_CONFIRMED) ? "confirmed" : "uncertain";
          char buf[72];
          snprintf(buf, sizeof(buf), "[EVT] r=%u key=0x%02X act=down result=%s id=%u\r\n",
                   (unsigned)c.remote, (unsigned)c.downCmd, dupResult, (unsigned)c.tag);
          hal_.print(buf);
          continue;
        }
        slot = findFreeSlot(c.remote);
        if (slot < 0) {
          /* Bounded: 4 simultaneous keys per remote.  Reject explicitly. */
          char buf[64];
          snprintf(buf, sizeof(buf), "[ERR] held-slots-full r=%u\r\n", (unsigned)c.remote);
          hal_.print(buf);
          stats_.cmdRejected++;
          continue;
        }
        DartHeldKey& k = held_[c.remote - 1][slot];
        k.downCmd = c.downCmd;
        k.upCmd   = c.upCmd;
        k.holdMs  = c.holdMs;
        k.holdStart = hal_.millis();
        k.upAttempts = 0;
        /* Provisional: promoted to CONFIRMED/UNCERTAIN, or cleared, in
         * finishTxn() once the outcome is actually known. */
        k.state = DART_KEY_UNCERTAIN;
        beginTxn(c.remote, c.downCmd, DART_PURPOSE_DOWN, (uint8_t)slot, c.tag);
        return true;
      }

      default:
        continue;
    }
  }
  return false;
}

void DartCore::startNextWork() {
  if ((int32_t)(hal_.millis() - nextWorkAt_) < 0) return;   /* not due yet */
  /* Releases first: a pending release has a real deadline, a new press does not. */
  if (startReleaseWork()) return;
  startQueuedCommand();
}

void DartCore::tick() {
  const uint32_t now = hal_.millis();
  syncIsrCounters();

  if (resetting_) {
    if ((int32_t)(now - resetUntil_) < 0) return;   /* still asserted */
    finishSharedReset();
    nextWorkAt_ = hal_.millis() + DART_RETRY_GAP_MS;
    return;
  }

  /* Reads/ACKs may arrive while LOW. They must not skip the HIGH edge or
   * completion deadline. Finish the physical pulse independently of ISR state. */
  if (txnPulseActive_) {
    if ((int32_t)(now - txnPulseUntil_) < 0) return;
    hal_.writeChannel((uint8_t)(txnRemote_ - 1), true);
    txnPulseActive_ = false;
    DartCritical cs(hal_);
    if (txnStage_ == DART_TXN_PULSE_LOW) txnStage_ = DART_TXN_PRESENTED;
  }

  uint8_t stage;
  { DartCritical cs(hal_); stage = txnStage_; }

  if (stage == DART_TXN_IDLE) { startNextWork(); return; }

  if (stage == DART_TXN_ACKED) { finishTxn(DART_OUTCOME_CONFIRMED); return; }

  /* PRESENTED or READ: wait for the bounded ACK deadline. */
  if ((int32_t)(now - txnDeadline_) < 0) return;

  if (stage == DART_TXN_READ) {
    /* Read but not acknowledged: delivery is genuinely uncertain. */
    finishTxn(DART_OUTCOME_UNCERTAIN);
    return;
  }

  /* Never read.  Nothing was delivered, so re-triggering is safe and cannot
   * double-press.  Bounded by triggerRetries_. */
  if (txnTriggerTry_ < triggerRetries_) {
    txnTriggerTry_++;
    uint8_t key = txnKey_;
    uint8_t remote = txnRemote_, purpose = txnPurpose_;
    int8_t slot = txnSlot_;
    uint16_t tag = txnTag_;
    cancelTxnPresentation();
    beginTxn(remote, key, purpose, (uint8_t)(slot < 0 ? 0 : slot), tag);
    txnSlot_ = slot;
    return;
  }
  finishTxn(DART_OUTCOME_FAILED);
}

/* ---------------------------- shared reset ------------------------------ */
/* The reset pulse is driven as an explicit state so the main loop keeps
 * draining the host UART for the whole 50..2000 ms assertion.  The reference
 * sketch used delay(), and its Format-B path reached delay() with a value
 * derived from a pointer (see resetRemote(remoteNum, "80")), which could hold
 * the shared reset line LOW for an arbitrary, very long time. */
void DartCore::pulseSharedReset(uint16_t ms) {
  if (ms < DART_RESET_MS_MIN) ms = DART_RESET_MS_MIN;
  if (ms > DART_RESET_MS_MAX) ms = DART_RESET_MS_MAX;

  /* Abandon any outstanding transaction first: after the pulse every remote
   * circuit reboots and re-runs its own command-0x01 initialisation. */
  cancelTxnPresentation();
  txnSlot_ = -1;
  txnTriggerTry_ = 0;

  hal_.print("[RST] SHARED reset line: ALL 16 remotes reboot\r\n");
  hal_.writeSharedReset(false);
  resetting_ = true;
  resetUntil_ = hal_.millis() + ms;
}

void DartCore::finishSharedReset() {
  hal_.writeSharedReset(true);
  resetting_ = false;

  /* The line is physically shared, so ALL remote state must be reconciled -
   * not just the one remote named in the command.  The reference sketch
   * cleared a single element of sm[] and left the other fifteen stale, so
   * queued work and other held-key records survived a reset that had in fact
   * rebooted their hardware. */
  clearAllRemotes();
  while (qTail_ != qHead_) { qTail_ = (uint8_t)((qTail_ + 1) % DART_CMD_QUEUE_LEN); }
  { DartCritical cs(hal_); initSeen_ = false; }
  stats_.sharedResets++;
  hal_.print("[RST] done; all held-key state cleared, queue flushed\r\n");
}

/* ------------------------------ I2C (ISR) ------------------------------- */
uint8_t DartCore::onI2cRequest(uint8_t* out, uint8_t cap) {
  if (cap < DART_I2C_REPLY_LEN) return 0;

  uint8_t key = DART_I2C_KEY_NONE;
  if (txnStage_ == DART_TXN_PULSE_LOW || txnStage_ == DART_TXN_PRESENTED ||
      txnStage_ == DART_TXN_READ) {
    key = txnKey_;
    if (txnStage_ != DART_TXN_READ) txnStage_ = DART_TXN_READ;
    isrReads_++;
  }
  /* else: no transaction outstanding -> present "no key".  The legacy
   * firmware kept presenting the previous key indefinitely, which is how a
   * stale press reached a different remote. */

  uint8_t n = 0;
  if (clearInterrupt_) { out[n++] = DART_I2C_REPLY_BLANK; clearInterrupt_ = false; }
  else                 { out[n++] = DART_I2C_REPLY_READY; }
  out[n++] = DART_I2C_INT_REG;
  out[n++] = DART_I2C_EVT_REG;
  out[n++] = key;
  while (n < DART_I2C_REPLY_LEN) out[n++] = DART_I2C_REPLY_BLANK;
  return n;
}

void DartCore::onI2cReceive(const uint8_t* data, uint8_t len) {
  if (len == 0) return;
  const uint8_t command = data[0];
  const uint8_t lastByte = data[len - 1];

  if (command == DART_I2C_CMD_INIT) {
    /* Remote-side power-up exchange.  Legacy honoured this exactly once
     * (guarded by initFlag) and ignored everything else until then.  We honour
     * it whenever it arrives so that a remote circuit which reboots on its own
     * can re-register without needing the MCU to be reset too. */
    cfgReg_ = lastByte;
    initSeen_ = true;
    isrInitExchanges_++;
    return;
  }

  if (command == DART_I2C_CMD_ACK) {
    clearInterrupt_ = true;
    /* DELIBERATE DEVIATION, documented in docs/DART-FIRMWARE.md:
     * the legacy handler returned early while initFlag was true and therefore
     * discarded every ACK until a command-0x01 exchange was seen.  After an
     * MCU-only reset the remote masters never repeat that exchange, so the
     * board ignored all ACKs forever and every command reported
     * "Timeout waiting for acknowledgment".  Accepting the ACK regardless of
     * init state removes that deadlock and cannot break a remote that does
     * send the init exchange. */
    switch (txnStage_) {
      case DART_TXN_READ:
        txnStage_ = DART_TXN_ACKED;
        break;
      case DART_TXN_ACKED:
        isrAckDuplicate_++;
        break;
      case DART_TXN_PULSE_LOW:
      case DART_TXN_PRESENTED:
        /* ACK before anyone read the current key: it belongs to an EARLIER
         * transaction (a late ACK).  Crediting it here is exactly the
         * reference sketch's false-success bug, so it is counted and ignored. */
        isrAckEarly_++;
        break;
      default:
        isrAckStray_++;
        break;
    }
    return;
  }
  /* Any other opcode is not part of the contract: ignore it, do not guess. */
}
