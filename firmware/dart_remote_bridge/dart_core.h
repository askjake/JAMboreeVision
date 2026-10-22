/* dart_core.h - portable DART bridge logic (no Arduino headers).
 *
 * PROTOCOL REALITY AND ITS LIMITS  (read before changing correlation logic)
 * ------------------------------------------------------------------------
 * The DART remote circuits are I2C *masters*; this MCU is the slave at 0x34.
 * Derived from the legacy firmware that is actually installed
 * (askjake/JAMboree DART_Dumb.ino, banner "11.05.2024 Jake Montgomery
 * (That Was Good)") and from DART_16Remotes_Buffered_Handshake_TxAck_Timestamps:
 *
 *   - A key is offered by pulsing that remote's interrupt line LOW then HIGH.
 *   - The master then reads DART_I2C_REPLY_LEN bytes:
 *       [0] 0xAF normally, 0x00 immediately after an ACK (interrupt cleared)
 *       [1] INT_REG (0x01)
 *       [2] EVT_REG (0x01)
 *       [3] the key code
 *       [4..14] 0x00
 *   - The master acknowledges by writing opcode 0x02.
 *   - At its own power-up the master writes opcode 0x01 with a config byte.
 *
 * There is exactly ONE key register in this contract.  The contract carries
 * NO transaction identifier and the slave Wire API exposes NO master address,
 * so the firmware CANNOT prove which remote performed a given read or ACK.
 * Correlation is therefore purely temporal, and the only safe way to keep it
 * sound is to allow at most ONE outstanding key transaction at a time and to
 * stop presenting a key the instant that transaction ends.  That is why this
 * implementation has no event FIFO: the shared 2048-byte FIFO in the reference
 * sketch is precisely what let a timed-out key be consumed later by a
 * different remote's read.
 *
 * Consequences that are reported honestly rather than hidden:
 *   - "read" means some master read the presented key.
 *   - "acked" means some master acknowledged after that read.
 *   - Neither proves the receiver executed RF, and this firmware never claims
 *     a set-top box was controlled.
 *   - An ACK that arrives BEFORE anything read the current key must belong to
 *     an earlier transaction, so it is counted (ackEarly) and ignored.  But an
 *     ACK that arrives INSIDE the current transaction's READ window is
 *     INDISTINGUISHABLE from the expected one, whichever remote sent it.  No
 *     correlation logic here or anywhere else can resolve that case with this
 *     contract; allowing only one outstanding transaction bounds the damage to
 *     that single transaction instead of a FIFO of stale keys, and the
 *     ackEarly/ackStray/ackDuplicate counters in "qstat" expose the symptom.
 *     This firmware therefore does NOT claim per-remote ACK correlation.
 */
#ifndef DART_CORE_H
#define DART_CORE_H

#include <stdint.h>
#include "dart_board_config.h"
#include "dart_hal.h"

/* ---- Held-key bookkeeping ---------------------------------------------- */
enum DartKeyState : uint8_t {
  DART_KEY_FREE = 0,
  DART_KEY_CONFIRMED,   /* DOWN was read AND acknowledged */
  DART_KEY_UNCERTAIN    /* DOWN was read but never acknowledged: may be held */
};

struct DartHeldKey {
  uint32_t holdStart;
  uint16_t holdMs;      /* 0 = release as soon as possible, 0xFFFF = host-managed */
  uint8_t  downCmd;
  uint8_t  upCmd;
  uint8_t  state;       /* DartKeyState */
  uint8_t  upAttempts;
};

/* ---- Transaction stages ------------------------------------------------ */
enum DartTxnStage : uint8_t {
  DART_TXN_IDLE = 0,
  DART_TXN_PULSE_LOW,   /* interrupt line held LOW, key already presented */
  DART_TXN_PRESENTED,   /* line released HIGH, waiting for the master to read */
  DART_TXN_READ,        /* a master read the key, waiting for ACK */
  DART_TXN_ACKED        /* ACK observed after the read: success */
};

enum DartTxnPurpose : uint8_t {
  DART_PURPOSE_DOWN = 0,
  DART_PURPOSE_UP,
  DART_PURPOSE_RELEASE_ALL
};

enum DartOutcome : uint8_t {
  DART_OUTCOME_CONFIRMED = 0,
  DART_OUTCOME_UNCERTAIN,   /* read but not acknowledged */
  DART_OUTCOME_FAILED       /* never read: the key was not delivered */
};

/* ---- Bounded host command queue ---------------------------------------- */
enum DartCmdKind : uint8_t {
  DART_CMD_FORMAT_A = 0,   /* <r> <downHex> <upHex> <holdMs> */
  DART_CMD_KEY_DOWN,       /* <r> <btn> down */
  DART_CMD_KEY_UP,         /* <r> <btn> up */
  DART_CMD_RELEASE_ALL,    /* <r> 86 allup */
  DART_CMD_SHARED_RESET    /* <r> 99 reset | <r> reset [ms] | reset */
};

struct DartCommand {
  uint16_t holdMs;
  uint16_t tag;        /* optional host correlation id from "#<n>"; 0 = none */
  uint8_t  kind;
  uint8_t  remote;     /* 1..16, or 0 for the global shared reset */
  uint8_t  downCmd;
  uint8_t  upCmd;
};

/* ---- Counters surfaced by "qstat" (diagnostics, bounded, non-blocking) -- */
struct DartStats {
  uint32_t cmdAccepted;
  uint32_t cmdRejected;
  uint32_t cmdDropped;      /* queue full */
  uint32_t txnConfirmed;
  uint32_t txnUncertain;
  uint32_t txnFailed;
  uint32_t ackEarly;        /* ACK arrived while still PRESENTED: not credited */
  uint32_t ackStray;        /* ACK arrived with no transaction outstanding */
  uint32_t ackDuplicate;    /* extra ACK after the transaction already ACKed */
  uint32_t reads;           /* master reads of the key register */
  uint32_t initExchanges;   /* opcode 0x01 seen */
  uint32_t lineOverflows;
  uint32_t watchdogReleases;
  uint32_t sharedResets;
};

class DartCore {
 public:
  explicit DartCore(DartHal& hal);

  /* Call once after the HAL is usable.  Prints the identity banner. */
  void begin();
  /* Call as often as possible from the main loop.  Never blocks on ACKs. */
  void tick();
  /* Feed raw host bytes (one at a time).  Safe to call from the main loop. */
  void feedSerial(char c);

  /* --- I2C callbacks.  Both run in interrupt context. --- */
  /* Fills out[0..DART_I2C_REPLY_LEN-1]; returns the number of bytes written. */
  uint8_t onI2cRequest(uint8_t* out, uint8_t cap);
  void onI2cReceive(const uint8_t* data, uint8_t len);

  /* --- Introspection used by the regression harness --- */
  const DartStats& stats() const { return stats_; }
  uint8_t txnStage() const { return txnStage_; }
  bool debugEnabled() const { return debug_; }
  uint32_t ackTimeoutMs() const { return ackTimeoutMs_; }
  const DartHeldKey& held(uint8_t remote1, uint8_t slot) const {
    return held_[remote1 - 1][slot];
  }
  uint8_t heldCount(uint8_t remote1) const;
  bool isHeld(uint8_t remote1, uint8_t downCmd) const;
  uint8_t queueDepth() const;
  bool initSeen() const { return initSeen_; }
  bool resetting() const { return resetting_; }

 private:
  /* parsing */
  void handleLine(char* line, uint8_t len);
  bool dispatchTokens(char tok[DART_TOKEN_MAX][DART_TOKEN_LEN], uint8_t ntok, uint16_t tag);
  bool enqueue(const DartCommand& cmd);
  bool dequeue(DartCommand& cmd);

  /* command execution */
  void startNextWork();
  bool startReleaseWork();
  bool startQueuedCommand();
  void beginTxn(uint8_t remote1, uint8_t key, uint8_t purpose, uint8_t slot, uint16_t tag);
  void finishTxn(uint8_t outcome);
  void cancelTxnPresentation();

  /* held-key bookkeeping */
  int8_t findSlot(uint8_t remote1, uint8_t downCmd) const;
  int8_t findFreeSlot(uint8_t remote1) const;
  void clearRemote(uint8_t remote1);
  void clearAllRemotes();

  /* actions */
  void pulseSharedReset(uint16_t ms);
  void finishSharedReset();

  /* reporting */
  void emitOutcome(uint8_t outcome, uint8_t remote1, uint8_t key,
                   uint8_t purpose, uint16_t tag);
  void syncIsrCounters();
  void printBanner();
  void printHelp();
  void printStatus();
  void printQstat();
  void err(const char* code, const char* detail);

  DartHal& hal_;

  /* --- state shared with interrupt context (volatile) --- */
  volatile uint8_t  txnStage_;
  volatile uint8_t  txnKey_;
  volatile bool     clearInterrupt_;
  volatile bool     initSeen_;
  volatile uint8_t  cfgReg_;
  volatile uint32_t isrReads_;
  volatile uint32_t isrAckEarly_;
  volatile uint32_t isrAckStray_;
  volatile uint32_t isrAckDuplicate_;
  volatile uint32_t isrInitExchanges_;

  /* --- main-loop-only transaction context --- */
  uint8_t  txnRemote_;      /* 1..16 */
  uint8_t  txnPurpose_;
  int8_t   txnSlot_;        /* held_ slot index, or -1 */
  uint16_t txnTag_;
  uint8_t  txnTriggerTry_;
  uint32_t txnDeadline_;
  bool     txnPulseActive_; /* independent of ISR-updated transaction stage */
  uint32_t txnPulseUntil_;
  uint32_t nextWorkAt_;     /* retry pacing without blocking */
  uint32_t resetUntil_;     /* shared-reset pulse deadline */
  bool     resetting_;      /* shared-reset pulse in progress */

  DartHeldKey held_[DART_REMOTE_COUNT][DART_HELD_SLOTS];

  DartCommand queue_[DART_CMD_QUEUE_LEN];
  uint8_t qHead_, qTail_;

  char    line_[DART_LINE_MAX + 1];
  uint8_t lineLen_;
  bool    lineOverflow_;

  bool     debug_;
  uint32_t ackTimeoutMs_;
  uint8_t  triggerRetries_;
  DartStats stats_;
};

/* Button number 1..40 -> {down, up} codes.  Identical to the table used by
 * JAMboreeVision jamboree/commands.py button_commands and by both legacy
 * sketches, held in flash so it costs no SRAM. */
bool dartButtonCodes(uint8_t buttonNum, uint8_t* downCmd, uint8_t* upCmd);

#endif /* DART_CORE_H */
