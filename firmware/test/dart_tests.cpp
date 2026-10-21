/* dart_tests.cpp - deterministic regression suite for the real DART firmware.
 *
 * Build/run:  make -C firmware/test run
 *
 * Numbered REGRESSION tests correspond 1:1 to the defects reproduced in the
 * reference sketch DART_16Remotes_Buffered_Handshake_TxAck_Timestamps.ino.
 */
#include "dart_test_harness.h"

int g_pass = 0, g_fail = 0;
std::vector<std::string> g_failures;
std::string g_currentTest;
int g_harnessFaults = 0;
size_t g_hwChannelEdges = 0, g_hwResetEdges = 0, g_hwOut = 0, g_hwKeysSeen = 0;

/* Button numbers used by JAMboreeVision (jamboree/commands.py). */
enum {
  BTN_POWER = 1, BTN_HOME = 2, BTN_DVR = 3, BTN_GUIDE = 4, BTN_MIC = 7,
  BTN_SAT = 35, BTN_TV = 36, BTN_AUX = 37, BTN_INPUT = 38
};

/* ====================================================================== */
/* 1. Command forms, validation, channel range                            */
/* ====================================================================== */
static void test_command_forms() {
  TEST("accepted command forms produce exactly one transaction each");
  {
    DartBus b; b.begin();
    b.send("1 83 03 120");                 /* Format A */
    b.settle();
    CHECK_EQ(b.hal.fallingEdges(0), 2);    /* one DOWN + one UP */
    CHECK(b.hal.sawOut("key=0x83 act=down result=confirmed"));
    CHECK(b.hal.sawOut("key=0x03 act=up result=confirmed"));
    CHECK_EQ(b.core.heldCount(1), 0);
  }
  {
    DartBus b; b.begin();
    b.send("1 4 down"); b.settle();
    CHECK_EQ(b.core.heldCount(1), 1);
    CHECK(b.core.isHeld(1, 0x83));
    b.send("1 4 up"); b.settle();
    CHECK_EQ(b.core.heldCount(1), 0);
  }
  {
    DartBus b; b.begin();
    b.send("1 4 down"); b.settle();
    b.hal.clearOut();
    b.send("1 86 allup"); b.settle();
    CHECK(b.hal.sawOut("key=0x03 act=up result=confirmed"));
    CHECK_EQ(b.core.heldCount(1), 0);
  }
  {
    DartBus b; b.begin();
    b.send("1 99 reset"); b.settle();
    CHECK_EQ(b.hal.resetPulses(), 1);
    CHECK(b.hal.sawOut("SHARED reset line"));
  }
  {
    DartBus b; b.begin();
    b.send("reset"); b.settle();
    CHECK_EQ(b.hal.resetPulses(), 1);
  }
  {
    DartBus b; b.begin();
    b.send("2 reset 250"); b.settle();
    CHECK_EQ(b.hal.resetPulses(), 1);
  }

  TEST("version/help/status/qstat respond and identify the firmware");
  {
    DartBus b; b.begin();
    b.send("version"); b.core.tick();
    CHECK(b.hal.sawOut("DART-BRIDGE"));
    CHECK(b.hal.sawOut("board=HOST_TEST"));
    CHECK(b.hal.sawOut("i2c-proto=1"));
    b.hal.clearOut(); b.send("help");   CHECK(b.hal.sawOut("allup"));
    CHECK(b.hal.sawOut("SHARED"));      /* reset semantics documented inline */
    b.hal.clearOut(); b.send("status"); CHECK(b.hal.sawOut("[STATUS]"));
    b.hal.clearOut(); b.send("qstat");  CHECK(b.hal.sawOut("[Q] queue="));
  }

  TEST("invalid, malformed and overlong input is rejected without side effects");
  {
    DartBus b; b.begin();
    const char* bad[] = {
      "0 4 down", "17 4 down", "-1 4 down", "abc 4 down", "1x 4 down",
      "1 0 down", "1 41 down", "1 4 sideways", "1 ZZ 03 100", "1 83 GG 100",
      "1 83 03 abc", "1 83 03 -5", "1 83 03 30001", "1 123 03 100",
      "1 reset 10", "1 reset 99999", "1 reset 500 extra", "garbage",
      "1", "1 4", "ack 5", "ack 99999", "retries 9", "debug maybe",
      "1 00 03 100", "#5", "1 4 down #abc"
    };
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i) {
      b.send(bad[i]);
      b.settle(200);
    }
    CHECK_EQ(b.hal.resetPulses(), 0);          /* no reset line movement */
    for (uint8_t c = 0; c < DART_REMOTE_COUNT; ++c) CHECK_EQ(b.hal.fallingEdges(c), 0);
    for (uint8_t r = 1; r <= DART_REMOTE_COUNT; ++r) CHECK_EQ(b.core.heldCount(r), 0);
    CHECK(b.core.stats().cmdRejected >= 20);
    CHECK_EQ(b.core.stats().cmdAccepted, 0u);

    /* Overlong line: rejected explicitly, and the NEXT line still parses. */
    b.hal.clearOut();
    std::string huge(400, '9');
    b.send(huge.c_str());
    CHECK(b.hal.sawOut("line-too-long"));
    CHECK_EQ(b.core.stats().lineOverflows, 1u);
    b.send("1 4 down"); b.settle();
    CHECK_EQ(b.core.heldCount(1), 1);          /* no corruption of parser state */
  }

  TEST("channels 1, 13 and 16 all work; 0/17 are refused");
  {
    const uint8_t chans[3] = {1, 13, 16};
    for (int i = 0; i < 3; ++i) {
      DartBus b; b.begin();
      char cmd[32]; snprintf(cmd, sizeof(cmd), "%u 4 down", (unsigned)chans[i]);
      b.send(cmd); b.settle();
      CHECK_EQ(b.core.heldCount(chans[i]), 1);
      CHECK_EQ(b.hal.fallingEdges(chans[i] - 1), 1);
      CHECK_EQ(b.master[chans[i] - 1].reads, 1);
      /* no other channel was pulsed */
      for (uint8_t c = 0; c < DART_REMOTE_COUNT; ++c)
        if (c != chans[i] - 1) CHECK_EQ(b.hal.fallingEdges(c), 0);
    }
  }

  TEST("button code mapping matches jamboree/commands.py (Guide, Mic, SAT...)");
  {
    struct { uint8_t btn, dn, up; } k[] = {
      {BTN_POWER,0x81,0x01},{BTN_HOME,0x8C,0x0C},{BTN_DVR,0x8B,0x0B},
      {BTN_GUIDE,0x83,0x03},{BTN_MIC,0x97,0x17},{BTN_SAT,0xEF,0x6F},
      {BTN_TV,0xF0,0x70},{BTN_AUX,0xF1,0x71},{BTN_INPUT,0xF2,0x72},
      {39,0x8C,0x8B},{40,0x0B,0x03}
    };
    for (size_t i = 0; i < sizeof(k)/sizeof(k[0]); ++i) {
      uint8_t dn = 0, up = 0;
      CHECK(dartButtonCodes(k[i].btn, &dn, &up));
      CHECK_EQ(dn, k[i].dn);
      CHECK_EQ(up, k[i].up);
    }
    uint8_t dn, up;
    CHECK(!dartButtonCodes(0, &dn, &up));
    CHECK(!dartButtonCodes(41, &dn, &up));

    /* Mic end-to-end: down then up emits 0x97 then 0x17 on the wire. */
    DartBus b; b.begin();
    b.send("5 7 down"); b.settle();
    CHECK_EQ(b.master[4].keysSeen.size(), 1u);
    CHECK_EQ(b.master[4].keyAt(0), 0x97);
    b.send("5 7 up"); b.settle();
    CHECK_EQ(b.master[4].keysSeen.size(), 2u);
    CHECK_EQ(b.master[4].keyAt(1), 0x17);
  }
}

/* ====================================================================== */
/* 2. Hold durations                                                      */
/* ====================================================================== */
static void test_hold_durations() {
  TEST("zero-duration Format A releases exactly once (REGRESSION #6)");
  {
    DartBus b; b.begin();
    b.send("1 83 03 0");
    b.settle();
    /* Reference bug: trySendUpFor() ran immediately AND unattendedTick() ran
     * again because sm[] was never cleared, sending a second UP. */
    CHECK_EQ(b.hal.fallingEdges(0), 2);                 /* DOWN + exactly one UP */
    CHECK_EQ(b.hal.countOut("key=0x03 act=up"), 1);
    CHECK_EQ(b.core.heldCount(1), 0);
    b.advance(2000);
    CHECK_EQ(b.hal.countOut("key=0x03 act=up"), 1);     /* still only one */
  }

  TEST("normal tap, 3 s hold and maximum 30 s hold release at the right time");
  {
    struct { uint32_t hold; } cases[] = {{80}, {120}, {3000}, {30000}};
    for (size_t i = 0; i < sizeof(cases)/sizeof(cases[0]); ++i) {
      DartBus b; b.begin();
      char cmd[48]; snprintf(cmd, sizeof(cmd), "1 8B 0B %lu", (unsigned long)cases[i].hold);
      b.send(cmd);
      b.advance(60);                       /* DOWN completes */
      CHECK_EQ(b.core.heldCount(1), 1);
      /* Must still be held just before the deadline. */
      b.advance(cases[i].hold - 50);
      CHECK_EQ(b.core.heldCount(1), 1);
      CHECK_EQ(b.hal.countOut("key=0x0B act=up"), 0);
      b.advance(200);
      CHECK_EQ(b.core.heldCount(1), 0);
      CHECK_EQ(b.hal.countOut("key=0x0B act=up result=confirmed"), 1);
    }
  }

  TEST("a 3 s host-managed hold is NOT cut short by the watchdog");
  {
    DartBus b; b.begin();
    b.send("1 35 down");                   /* SAT down, host owns the release */
    b.advance(3000);
    /* A watchdog firing at exactly 3000 ms would break JAMboreeVision's
     * three-second unpair legs. */
    CHECK_EQ(b.core.heldCount(1), 1);
    CHECK_EQ(b.core.stats().watchdogReleases, 0u);
    b.advance(500);
    CHECK_EQ(b.core.heldCount(1), 1);
    b.send("1 35 up"); b.settle();
    CHECK_EQ(b.core.heldCount(1), 0);
    CHECK_EQ(b.core.stats().watchdogReleases, 0u);
  }

  TEST("hold above the documented maximum is refused, not clamped silently");
  {
    DartBus b; b.begin();
    b.send("1 83 03 30001");
    b.settle(100);
    CHECK(b.hal.sawOut("bad-hold-ms"));
    CHECK_EQ(b.hal.fallingEdges(0), 0);
  }
}

/* ====================================================================== */
/* 3. Multiple simultaneous keys (the unpair combinations)                */
/* ====================================================================== */
static void run_unpair_combo(uint8_t comboBtn, uint8_t comboDown, uint8_t comboUp) {
  /* Mirrors JAMboreeController.unpair() in jamboree/controller.py exactly:
   *   sat down, sleep 3.0, sat up, sleep 0.20,
   *   <combo> down, guide down, sleep 3.0,
   *   sat up, <combo> up, guide up, allup
   */
  DartBus b; b.begin();
  char cmd[32];

  b.send("3 35 down");  b.advance(50);
  CHECK(b.core.isHeld(3, 0xEF));
  b.advance(3000);
  b.send("3 35 up");    b.advance(50);
  CHECK_EQ(b.core.heldCount(3), 0);
  b.advance(200);

  snprintf(cmd, sizeof(cmd), "3 %u down", (unsigned)comboBtn);
  b.send(cmd);
  b.send("3 4 down");                       /* GUIDE down while combo is held */
  b.advance(120);

  /* REGRESSION #3: the reference sketch tracked a single held key per remote
   * and canStartDown() rejected the second DOWN with
   * "pending UP -> deferring DOWN", so GUIDE was never transmitted and the
   * DVR+GUIDE / HOME+GUIDE unpair combination could not happen at all. */
  CHECK_EQ(b.core.heldCount(3), 2);
  CHECK(b.core.isHeld(3, comboDown));
  CHECK(b.core.isHeld(3, 0x83));
  CHECK(b.hal.sawOut("act=down result=confirmed"));

  b.advance(3000);
  CHECK_EQ(b.core.heldCount(3), 2);          /* both still held through 3 s */
  CHECK_EQ(b.core.stats().watchdogReleases, 0u);

  b.hal.clearOut();
  b.send("3 35 up");
  snprintf(cmd, sizeof(cmd), "3 %u up", (unsigned)comboBtn);
  b.send(cmd);
  b.send("3 4 up");
  b.send("3 86 allup");
  b.settle();

  CHECK_EQ(b.core.heldCount(3), 0);
  char needle[48];
  snprintf(needle, sizeof(needle), "key=0x%02X act=up result=confirmed", (unsigned)comboUp);
  CHECK(b.hal.sawOut(needle));
  CHECK(b.hal.sawOut("key=0x03 act=up result=confirmed"));
}

static void test_combinations() {
  TEST("SAT hold/release then DVR + GUIDE held together (REGRESSION #3)");
  run_unpair_combo(BTN_DVR, 0x8B, 0x0B);

  TEST("SAT hold/release then HOME + GUIDE held together (REGRESSION #3)");
  run_unpair_combo(BTN_HOME, 0x8C, 0x0C);

  TEST("each key of a combination releases independently");
  {
    DartBus b; b.begin();
    b.send("2 3 down"); b.send("2 4 down"); b.settle();
    CHECK_EQ(b.core.heldCount(2), 2);

    b.hal.clearOut();
    b.send("2 3 up"); b.settle();
    CHECK_EQ(b.core.heldCount(2), 1);
    CHECK(b.core.isHeld(2, 0x83));            /* GUIDE still held */
    CHECK(!b.core.isHeld(2, 0x8B));
    CHECK(b.hal.sawOut("key=0x0B act=up result=confirmed"));
    CHECK_EQ(b.hal.countOut("key=0x03 act=up"), 0);   /* GUIDE not disturbed */

    b.hal.clearOut();
    b.send("2 4 up"); b.settle();
    CHECK_EQ(b.core.heldCount(2), 0);
    CHECK(b.hal.sawOut("key=0x03 act=up result=confirmed"));
  }

  TEST("four simultaneous keys fit; a fifth is refused explicitly");
  {
    DartBus b; b.begin();
    b.send("1 2 down"); b.send("1 3 down"); b.send("1 4 down"); b.send("1 5 down");
    b.settle();
    CHECK_EQ(b.core.heldCount(1), 4);
    b.hal.clearOut();
    b.send("1 6 down"); b.settle();
    CHECK(b.hal.sawOut("held-slots-full"));
    CHECK_EQ(b.core.heldCount(1), 4);
  }
}

/* ====================================================================== */
/* 4. Release-all semantics                                               */
/* ====================================================================== */
static void test_release_all() {
  TEST("release-all releases the keys actually held, not a fixed code (REGR #5)");
  {
    DartBus b; b.begin();
    b.send("4 3 down");    /* DVR  0x8B/0x0B */
    b.send("4 35 down");   /* SAT  0xEF/0x6F */
    b.settle();
    CHECK_EQ(b.core.heldCount(4), 2);

    b.master[3].keysSeen.clear();
    b.hal.clearOut();
    b.send("4 86 allup");
    b.settle();

    /* Reference sent a single hard-coded 0x03 (Guide UP); legacy sent a single
     * hard-coded 0x01 (Power UP).  Neither releases DVR or SAT. */
    CHECK_EQ(b.core.heldCount(4), 0);
    bool saw0B = false, saw6F = false, saw03 = false;
    for (size_t i = 0; i < b.master[3].keysSeen.size(); ++i) {
      if (b.master[3].keysSeen[i] == 0x0B) saw0B = true;
      if (b.master[3].keysSeen[i] == 0x6F) saw6F = true;
      if (b.master[3].keysSeen[i] == 0x03) saw03 = true;
    }
    CHECK(saw0B);
    CHECK(saw6F);
    CHECK(!saw03);     /* no invented universal release opcode */
  }

  TEST("release-all after a confirmed, a failed and an uncertain DOWN");
  {
    /* (a) confirmed */
    { DartBus b; b.begin();
      b.send("1 4 down"); b.settle();
      b.send("1 86 allup"); b.settle();
      CHECK_EQ(b.core.heldCount(1), 0);
      CHECK(b.hal.sawOut("key=0x03 act=up result=confirmed")); }

    /* (b) failed DOWN: never read, so nothing can be held and allup is a no-op */
    { DartBus b; b.begin();
      b.master[0].doRead = false;
      b.send("1 4 down"); b.settle();
      CHECK(b.hal.sawOut("act=down result=failed"));
      CHECK_EQ(b.core.heldCount(1), 0);
      b.hal.clearOut();
      b.master[0].doRead = true;
      b.send("1 86 allup"); b.settle();
      CHECK(b.hal.sawOut("act=allup result=confirmed"));
      CHECK_EQ(b.hal.countOut("key=0x03 act=up"), 0); }

    /* (c) uncertain DOWN: read but never acknowledged -> may be held, so
     *     release-all MUST still transmit its release. */
    { DartBus b; b.begin();
      b.master[0].doAck = false;
      b.send("1 4 down"); b.settle();
      CHECK(b.hal.sawOut("act=down result=uncertain"));
      CHECK_EQ(b.core.heldCount(1), 1);
      CHECK_EQ(b.core.held(1, 0).state, DART_KEY_UNCERTAIN);
      b.master[0].doAck = true;
      b.master[0].keysSeen.clear();
      b.send("1 86 allup"); b.settle();
      bool saw03 = false;
      for (size_t i = 0; i < b.master[0].keysSeen.size(); ++i)
        if (b.master[0].keysSeen[i] == 0x03) saw03 = true;
      CHECK(saw03);
      CHECK_EQ(b.core.heldCount(1), 0); }
  }
}

/* ====================================================================== */
/* 5. ACK / read fault injection                                          */
/* ====================================================================== */
static void test_ack_faults() {
  TEST("missing ACK reports uncertain and does NOT replay the DOWN");
  {
    DartBus b; b.begin();
    b.master[0].doAck = false;
    b.send("1 4 down"); b.settle();
    CHECK_EQ(b.master[0].reads, 1);            /* read exactly once */
    CHECK_EQ(b.hal.fallingEdges(0), 1);        /* no automatic DOWN replay */
    CHECK(b.hal.sawOut("act=down result=uncertain"));
    CHECK_EQ(b.core.stats().txnUncertain, 1u);
  }

  TEST("missing I2C read reports failed and retries the trigger only");
  {
    DartBus b; b.begin();
    b.master[0].doRead = false;
    b.send("1 4 down"); b.settle();
    CHECK_EQ(b.master[0].reads, 0);
    /* Nothing was delivered, so re-triggering cannot double-press. Bounded. */
    CHECK_EQ(b.hal.fallingEdges(0), 1 + (int)DART_TRIGGER_RETRIES_DEFAULT);
    CHECK(b.hal.sawOut("act=down result=failed"));
    CHECK_EQ(b.core.heldCount(1), 0);
  }

  TEST("ACK arriving before the read is not credited (REGRESSION #2)");
  {
    DartBus b; b.begin();
    b.master[0].doRead = false; b.master[0].doAck = false;
    b.send("1 4 down");
    b.advance(5);
    /* Stray ACK lands while the key is still merely PRESENTED. */
    b.master[0].sendAck();
    b.advance(5);
    CHECK(b.core.txnStage() != DART_TXN_ACKED);
    b.settle();
    /* Reference bug: ackReceived was already true, so once txSeenForCmd later
     * became true the command reported success without an ACK for THIS key. */
    CHECK(!b.hal.sawOut("act=down result=confirmed"));
    CHECK(b.core.stats().ackEarly >= 1u);
  }

  /* SCOPE OF THIS TEST - read with docs/DART-FIRMWARE.md "ACK ambiguity".
   * The late ACK below is injected while the NEXT transaction is still only
   * PRESENTED (nobody has read it), which is the window the firmware can
   * detect: an ACK arriving before any read must belong to an earlier
   * transaction, so it is counted as ackEarly and ignored.
   * This does NOT demonstrate - and the I2C contract cannot provide - immunity
   * to a late ACK that arrives INSIDE the next transaction's READ window.
   * There is no transaction id and no master address, so that case is
   * genuinely indistinguishable from the expected ACK. */
  TEST("a late ACK arriving before the next transaction's read is not credited");
  {
    DartBus b; b.begin();
    b.master[0].doAck = false;                 /* txn 1 will go uncertain */
    b.send("1 4 down");
    b.settle();
    CHECK(b.hal.sawOut("act=down result=uncertain"));

    b.master[0].doRead = false;                /* txn 2: nobody reads */
    b.hal.clearOut();
    b.send("1 2 down");
    b.advance(5);
    b.master[0].sendAck();                     /* the delayed ACK for txn 1 */
    b.advance(10);
    b.settle();
    CHECK(!b.hal.sawOut("key=0x8C act=down result=confirmed"));
    CHECK(b.core.stats().ackEarly >= 1u);
  }

  TEST("duplicate ACK is counted and harmless");
  {
    DartBus b; b.begin();
    b.master[0].duplicateAck = true;
    b.send("1 4 down"); b.settle();
    CHECK(b.hal.sawOut("act=down result=confirmed"));
    CHECK_EQ(b.core.heldCount(1), 1);
    b.send("qstat"); b.core.tick();
    CHECK(b.core.stats().ackDuplicate >= 1u || b.core.stats().ackStray >= 1u);
    /* No extra transaction was invented by the duplicate. */
    CHECK_EQ(b.hal.fallingEdges(0), 1);
  }

  TEST("delayed but valid ACK within the timeout still confirms");
  {
    DartBus b; b.begin();
    b.master[0].readDelayMs = 5; b.master[0].ackDelayMs = 200;
    b.send("1 4 down"); b.settle();
    CHECK(b.hal.sawOut("act=down result=confirmed"));
  }

  TEST("unexpected idle polling never yields a key");
  {
    DartBus b; b.begin();
    b.master[5].pollWhenIdle = true; b.master[5].pollPeriodMs = 10;
    b.advance(300);
    CHECK(b.master[5].reads > 10);
    for (size_t i = 0; i < b.master[5].keysSeen.size(); ++i)
      CHECK_EQ(b.master[5].keysSeen[i], DART_I2C_KEY_NONE);
  }

  TEST("I2C reply length and register layout stay legacy-compatible");
  {
    DartBus b; b.begin();
    b.master[0].doOneRead();
    CHECK_EQ(b.master[0].lastReply.size(), (size_t)DART_I2C_REPLY_LEN);
    CHECK_EQ(b.master[0].lastReply.size(), (size_t)15);
    CHECK_EQ(b.master[0].replyAt(0), 0xAF);
    CHECK_EQ(b.master[0].replyAt(1), 0x01);
    CHECK_EQ(b.master[0].replyAt(2), 0x01);
    CHECK_EQ(b.master[0].replyAt(3), DART_I2C_KEY_NONE);
    for (size_t i = 4; i < b.master[0].lastReply.size(); ++i)
      CHECK_EQ(b.master[0].replyAt(i), 0x00);

    /* Byte 0 becomes BLANK on the first read after an ACK, as in legacy. */
    b.send("1 4 down");
    b.settle();
    b.master[0].doOneRead();
    CHECK_EQ(b.master[0].replyAt(0), 0x00);
    b.master[0].doOneRead();
    CHECK_EQ(b.master[0].replyAt(0), 0xAF);
  }
}

/* ====================================================================== */
/* 6. Stale-event isolation across channels                               */
/* ====================================================================== */
static void test_no_stale_cross_channel() {
  TEST("a timed-out key is never delivered to another channel (REGRESSION #1)");
  {
    DartBus b; b.begin();
    /* Channel 1 is dead: it never reads and never ACKs. */
    b.master[0].doRead = false; b.master[0].doAck = false;
    b.send("1 4 down");          /* 0x83 GUIDE down, will time out + retry */
    b.settle();
    CHECK(b.hal.sawOut("act=down result=failed"));

    /* Now a completely different channel does its own command. */
    b.master[7].keysSeen.clear();
    b.send("8 35 down");         /* 0xEF SAT down on channel 8 */
    b.settle();

    /* Reference bug: every timed-out key and every retry copy stayed in the
     * shared 2048-entry FIFO, and requestEvent() popped one per read, so
     * channel 8's read could consume channel 1's obsolete 0x83. */
    CHECK(b.master[7].keysSeen.size() >= 1u);
    for (size_t i = 0; i < b.master[7].keysSeen.size(); ++i) {
      CHECK(b.master[7].keysSeen[i] != 0x83);
    }
    CHECK_EQ(b.master[7].keyAt(0), 0xEF);
    CHECK(!b.core.isHeld(8, 0x83));
    CHECK(b.core.isHeld(8, 0xEF));
  }

  TEST("retries never enqueue extra copies of a key");
  {
    DartBus b; b.begin();
    b.master[0].doRead = false;
    b.send("1 4 down");
    b.settle();
    /* Let an idle poller drain anything that might have been left queued. */
    b.master[0].doRead = true;
    b.master[0].pollWhenIdle = true; b.master[0].pollPeriodMs = 5;
    b.master[0].keysSeen.clear();
    b.advance(400);
    for (size_t i = 0; i < b.master[0].keysSeen.size(); ++i)
      CHECK_EQ(b.master[0].keysSeen[i], DART_I2C_KEY_NONE);
  }

  TEST("only one transaction is outstanding at a time across 16 channels");
  {
    DartBus b; b.begin();
    for (uint8_t r = 1; r <= 16; ++r) {
      char cmd[32]; snprintf(cmd, sizeof(cmd), "%u 4 down", (unsigned)r);
      b.send(cmd);
    }
    /* The command queue holds 11; the rest are rejected explicitly. */
    uint32_t concurrentLowMax = 0;
    for (uint32_t i = 0; i < 3000; ++i) {
      b.advance(1);
      uint32_t low = 0;
      for (uint8_t c = 0; c < DART_REMOTE_COUNT; ++c) if (!b.hal.channelHigh[c]) low++;
      if (low > concurrentLowMax) concurrentLowMax = low;
    }
    CHECK(concurrentLowMax <= 1u);     /* never two channels triggered at once */
    CHECK(b.core.stats().cmdDropped >= 1u);
    CHECK(b.hal.sawOut("[BUSY] queue full"));
  }
}

/* ====================================================================== */
/* 7. Duplicate DOWN / UP                                                 */
/* ====================================================================== */
static void test_duplicates() {
  TEST("duplicate DOWN refreshes the hold instead of double-pressing");
  {
    DartBus b; b.begin();
    b.send("1 4 down"); b.settle();
    CHECK_EQ(b.hal.fallingEdges(0), 1);
    b.send("1 4 down"); b.settle();
    CHECK_EQ(b.hal.fallingEdges(0), 1);      /* no second physical press */
    CHECK_EQ(b.core.heldCount(1), 1);
  }

  TEST("duplicate UP transmits a release but invents no held-key record");
  {
    DartBus b; b.begin();
    b.send("1 4 down"); b.settle();
    b.send("1 4 up");   b.settle();
    CHECK_EQ(b.core.heldCount(1), 0);
    b.hal.clearOut();
    b.master[0].keysSeen.clear();
    b.send("1 4 up");   b.settle();
    bool saw03 = false;
    for (size_t i = 0; i < b.master[0].keysSeen.size(); ++i)
      if (b.master[0].keysSeen[i] == 0x03) saw03 = true;
    CHECK(saw03);                             /* idempotent release attempt */
    CHECK_EQ(b.core.heldCount(1), 0);
  }

  TEST("UP for a key that was never pressed still only releases that key");
  {
    DartBus b; b.begin();
    b.send("1 35 up"); b.settle();
    CHECK_EQ(b.core.heldCount(1), 0);
    CHECK_EQ(b.master[0].keysSeen.size(), 1u);
    CHECK_EQ(b.master[0].keyAt(0), 0x6F);
  }
}

/* ====================================================================== */
/* 8. Shared reset                                                        */
/* ====================================================================== */
static void test_shared_reset() {
  TEST("shared reset reconciles ALL channels, not just the named one (REGR #4)");
  {
    DartBus b; b.begin();
    b.send("1 4 down"); b.send("5 3 down"); b.send("9 35 down");
    b.settle();
    CHECK_EQ(b.core.heldCount(1), 1);
    CHECK_EQ(b.core.heldCount(5), 1);
    CHECK_EQ(b.core.heldCount(9), 1);
    /* queue some more work so we can prove it is flushed too */
    b.send("12 4 down"); b.send("13 4 down");

    b.hal.clearOut();
    b.send("2 99 reset");            /* nominally "remote 2" */
    b.settle();

    CHECK_EQ(b.hal.resetPulses(), 1);
    CHECK(b.hal.sawOut("SHARED reset line: ALL 16 remotes"));
    /* Reference bug: resetRemote() pulsed the shared line but cleared only
     * sm[remoteNum-1], leaving 15 stale held-key records and queued work. */
    for (uint8_t r = 1; r <= DART_REMOTE_COUNT; ++r) CHECK_EQ(b.core.heldCount(r), 0);
    CHECK_EQ(b.core.queueDepth(), 0);
    CHECK(!b.core.initSeen());       /* remotes will re-run their own init */
  }

  TEST("reset pulse width is bounded and does not block serial intake");
  {
    DartBus b; b.begin();
    b.send("1 reset 2000");
    b.advance(5);
    CHECK(!b.hal.resetHigh);                 /* asserted */
    /* Feed a command mid-pulse: it must be accepted, not lost. */
    b.send("7 4 down");
    b.advance(2100);
    CHECK(b.hal.resetHigh);                  /* deasserted after <= 2000 ms */
    b.settle();
    /* The queue was flushed by the reset, so the mid-pulse command is gone
     * rather than silently executed against just-rebooted remotes. */
    CHECK_EQ(b.core.heldCount(7), 0);
    CHECK_EQ(b.hal.resetPulses(), 1);
  }

  TEST("invalid reset arguments never pulse the reset line (REGRESSION #4b)");
  {
    const char* bad[] = {
      "0 99 reset", "17 99 reset", "-3 99 reset", "xx 99 reset",
      "0 reset", "17 reset", "1 reset 0", "1 reset 10", "1 reset 99999",
      "1 reset abc", "1 reset 500 junk"
    };
    DartBus b; b.begin();
    for (size_t i = 0; i < sizeof(bad)/sizeof(bad[0]); ++i) {
      b.send(bad[i]); b.settle(80);
    }
    /* The reference sketch passed the string literal "80" as the int delayMs
     * (compiling only under -fpermissive), so its Format-B reset asserted the
     * shared line for a pointer-derived duration; and it never validated
     * remoteNum before touching the line at all. */
    CHECK_EQ(b.hal.resetPulses(), 0);
    CHECK(b.hal.resetHigh);
  }

  TEST("reset while a transaction is mid-flight abandons it cleanly");
  {
    DartBus b; b.begin();
    b.master[0].doRead = false; b.master[0].doAck = false;
    b.send("1 4 down");
    b.advance(10);
    CHECK(b.core.txnStage() != DART_TXN_IDLE);
    b.send("reset");
    b.advance(700);
    b.settle();
    CHECK_EQ(b.core.txnStage(), DART_TXN_IDLE);
    CHECK_EQ(b.core.heldCount(1), 0);
    /* And no stale key is presented afterwards. */
    b.master[0].doRead = true;
    b.master[0].keysSeen.clear();
    b.master[0].doOneRead();
    CHECK_EQ(b.master[0].keyAt(0), DART_I2C_KEY_NONE);
  }
}

/* ====================================================================== */
/* 9. Saturation, bursts, logging load                                    */
/* ====================================================================== */
static void test_saturation() {
  TEST("queue saturation is rejected explicitly with no corruption");
  {
    DartBus b; b.begin();
    b.master[0].doRead = false; b.master[0].doAck = false;   /* stall the engine */
    for (int i = 0; i < 60; ++i) b.send("1 4 down");
    CHECK(b.core.stats().cmdDropped > 0u);
    CHECK(b.hal.sawOut("[BUSY] queue full"));
    CHECK(b.core.queueDepth() <= DART_CMD_QUEUE_LEN - 1);
    b.master[0].doRead = true; b.master[0].doAck = true;
    b.settle();
    /* Still coherent afterwards. */
    b.send("1 4 up"); b.settle();
    CHECK_EQ(b.core.heldCount(1), 0);
    CHECK(!b.hal.irqMisuse);
  }

  TEST("serial bursts without newlines, and split lines, parse correctly");
  {
    DartBus b; b.begin();
    b.sendRaw("1 4 ");            /* partial */
    b.advance(50);
    CHECK_EQ(b.core.heldCount(1), 0);
    b.sendRaw("down\n");          /* completes */
    b.settle();
    CHECK_EQ(b.core.heldCount(1), 1);

    /* Many commands arriving back-to-back in one burst. */
    DartBus c; c.begin();
    c.sendRaw("2 4 down\n2 4 up\n3 4 down\n3 4 up\n4 4 down\n4 4 up\n");
    c.settle();
    CHECK_EQ(c.core.heldCount(2), 0);
    CHECK_EQ(c.core.heldCount(3), 0);
    CHECK_EQ(c.core.heldCount(4), 0);
    CHECK_EQ(c.hal.fallingEdges(1), 2);
    CHECK_EQ(c.hal.fallingEdges(2), 2);
    CHECK_EQ(c.hal.fallingEdges(3), 2);
  }

  TEST("release deadlines are met while serial input and debug logging flood");
  {
    DartBus b; b.begin();
    b.send("debug on");
    b.send("1 8B 0B 300");          /* 300 ms hold on channel 1 */
    /* Hammer the parser with invalid lines for the whole hold window. */
    for (int i = 0; i < 300; ++i) {
      b.send("bogus line that must be rejected");
      b.advance(1);
    }
    b.settle();
    CHECK_EQ(b.core.heldCount(1), 0);
    CHECK(b.hal.sawOut("key=0x0B act=up result=confirmed"));
    /* The release must not have been dragged far past its deadline. */
    int upIdx = -1;
    for (size_t i = 0; i < b.hal.channelEdges.size(); ++i)
      if (b.hal.channelEdges[i].channel == 0 && !b.hal.channelEdges[i].high) upIdx = (int)i;
    CHECK(upIdx >= 0);
    CHECK(b.hal.channelEdgeTime(upIdx) <= 420u);
    CHECK(b.core.debugEnabled());
    b.send("debug off");
    CHECK(!b.core.debugEnabled());
  }

  TEST("debug disabled still reports outcomes and never changes behaviour");
  {
    DartBus on; on.begin(); on.send("debug on");
    on.send("1 83 03 100"); on.settle();
    DartBus off; off.begin(); off.send("debug off");
    off.send("1 83 03 100"); off.settle();
    CHECK_EQ(on.hal.fallingEdges(0), off.hal.fallingEdges(0));
    CHECK_EQ(on.core.heldCount(1), off.core.heldCount(1));
    CHECK(on.hal.sawOut("[SER]"));
    CHECK(!off.hal.sawOut("[SER]"));
    CHECK(off.hal.sawOut("act=up result=confirmed"));
  }
}

/* ====================================================================== */
/* 10. Client disappearance, idle, wraparound, recovery                   */
/* ====================================================================== */
static void test_idle_and_recovery() {
  TEST("a hold abandoned by a disconnected client is released by the watchdog");
  {
    DartBus b; b.begin();
    b.send("6 3 down");             /* host-managed hold, then the host vanishes */
    b.advance(100);
    CHECK_EQ(b.core.heldCount(6), 1);
    b.advance(DART_HOST_HOLD_WATCHDOG_MS - 500);
    CHECK_EQ(b.core.heldCount(6), 1);        /* not early */
    b.advance(1500);
    b.settle();
    CHECK_EQ(b.core.heldCount(6), 0);
    CHECK_EQ(b.core.stats().watchdogReleases, 1u);
    CHECK(b.hal.sawOut("[WDOG]"));
    CHECK(b.hal.sawOut("key=0x0B act=up result=confirmed"));
  }

  TEST("45 s and 120 s idle cause no reset and no spurious pin activity");
  {
    DartBus b; b.begin();
    b.advance(45000);
    CHECK_EQ(b.hal.resetPulses(), 0);
    CHECK_EQ(b.hal.channelEdges.size(), 0u);
    b.advance(75000);                        /* 120 s total */
    CHECK_EQ(b.hal.resetPulses(), 0);
    CHECK_EQ(b.hal.channelEdges.size(), 0u);
    CHECK(b.hal.resetHigh);
    for (uint8_t c = 0; c < DART_REMOTE_COUNT; ++c) CHECK(b.hal.channelHigh[c]);
    /* Still fully functional after a long quiet period. */
    b.send("1 83 03 100"); b.settle();
    CHECK(b.hal.sawOut("act=up result=confirmed"));
  }

  TEST("millisecond counter wraparound does not break holds or timeouts");
  {
    /* Board uptime ~49.7 days: millis() is about to wrap.  The clock is set
     * before begin() so every firmware-recorded timestamp is pre-wrap too. */
    DartBus b;
    b.hal.now = 0xFFFFF000u;
    b.begin();
    b.send("1 8B 0B 1000");
    b.advance(100);
    CHECK_EQ(b.core.heldCount(1), 1);
    b.advance(4000);                          /* crosses 0xFFFFFFFF -> 0 */
    CHECK(b.hal.now < 0x00001000u);           /* wrapped */
    b.settle();
    CHECK_EQ(b.core.heldCount(1), 0);
    CHECK(b.hal.sawOut("key=0x0B act=up result=confirmed"));
  }
  {
    /* A hold whose deadline lands on the far side of the wrap. */
    DartBus b;
    b.hal.now = 0xFFFFFF00u;                  /* 256 ms before wrap */
    b.begin();
    b.send("1 8B 0B 1000");
    b.advance(150);
    CHECK_EQ(b.core.heldCount(1), 1);
    b.advance(500);
    CHECK(b.hal.now < 0x00001000u);           /* wrapped mid-hold */
    CHECK_EQ(b.core.heldCount(1), 1);         /* not released early */
    b.advance(600);
    b.settle();
    CHECK_EQ(b.core.heldCount(1), 0);
    CHECK(b.hal.sawOut("key=0x0B act=up result=confirmed"));
  }
  {
    /* Wraparound during an ACK wait must still time out, not hang. */
    DartBus b;
    b.hal.now = 0xFFFFFFF0u;
    b.begin();
    b.master[0].doRead = false; b.master[0].doAck = false;
    b.send("1 4 down");
    b.advance(3000);
    b.settle();
    CHECK(b.hal.sawOut("act=down result=failed"));
    CHECK_EQ(b.core.txnStage(), DART_TXN_IDLE);
  }

  TEST("recovery after a simulated MCU-only reset (no remote reboot)");
  {
    /* The legacy firmware gated its ACK handler behind initFlag, so after an
     * MCU-only reset (for example a host DTR toggle) it ignored every ACK
     * forever: the remotes only send the init exchange at their OWN power-up. */
    DartBus b;
    b.core.begin();                 /* fresh MCU, NO init exchange from remotes */
    b.hal.clearOut();
    CHECK(!b.core.initSeen());
    b.send("1 4 down");
    b.settle();
    CHECK(b.hal.sawOut("act=down result=confirmed"));   /* not a deadlock */
    CHECK_EQ(b.hal.resetPulses(), 0);                   /* and no auto-reset */
  }

  TEST("recovery after a remote-side reset and re-initialisation");
  {
    DartBus b; b.begin();
    b.send("1 4 down"); b.settle();
    CHECK_EQ(b.core.heldCount(1), 1);
    /* The remote circuit reboots by itself and re-registers. */
    b.master[0].remoteReset();
    b.master[0].sendInit(0xA5);
    CHECK(b.core.initSeen());
    b.advance(10);
    /* Its held key is now stale in hardware; the watchdog still bounds it. */
    b.advance(DART_HOST_HOLD_WATCHDOG_MS + 500);
    b.settle();
    CHECK_EQ(b.core.heldCount(1), 0);
    b.hal.clearOut();
    b.send("1 4 down"); b.settle();
    CHECK(b.hal.sawOut("act=down result=confirmed"));
  }

  TEST("interrupt state is always restored, never blindly enabled");
  {
    DartBus b; b.begin();
    b.send("1 4 down"); b.settle();
    b.send("qstat"); b.core.tick();
    b.send("status"); b.core.tick();
    b.send("1 86 allup"); b.settle();
    b.send("reset"); b.advance(700); b.settle();
    CHECK(!b.hal.irqMisuse);
    CHECK_EQ(b.hal.irqDepth, 0);
    CHECK(b.hal.maxIrqDepth >= 1);
  }

  TEST("correlation tag is echoed when supplied and defaults to 0");
  {
    DartBus b; b.begin();
    b.send("1 4 down #4242"); b.settle();
    CHECK(b.hal.sawOut("id=4242"));
    b.hal.clearOut();
    b.send("1 4 up"); b.settle();
    CHECK(b.hal.sawOut("id=0"));
  }
}

/* ====================================================================== */
/* 11. Adversarial timing: reset-pulse bounds, stray ACKs, load deadlines */
/*                                                                        */
/* These scenarios exist because the corresponding reference-sketch       */
/* behaviour was confirmed broken in firmware/test/reference (R7, R2, R9) */
/* and because the I2C contract itself cannot attribute an ACK to a       */
/* channel.  They assert the firmware's *bounded* behaviour, never that   */
/* a receiver acted.                                                      */
/* ====================================================================== */

/* Measure every completed LOW->HIGH assertion of the shared reset line. */
static void resetPulseWidths(const FakeHal& hal, std::vector<uint32_t>& widths) {
  bool open = false;
  uint32_t t0 = 0;
  for (size_t i = 0; i < hal.resetEdges.size(); ++i) {
    if (!hal.resetEdges[i].high) { open = true; t0 = hal.resetEdges[i].t; }
    else if (open) { widths.push_back(hal.resetEdges[i].t - t0); open = false; }
  }
}

static void test_adversarial_timing() {
  /* ---------------------------------------------------------------- R7 --
   * DIRECT regression for the "pointer-as-delay" defect.
   * The reference sketch's Format-B reset path called
   *     resetRemote(remoteNum, "80");
   * passing the ADDRESS of the string literal "80" as `int delayMs`.
   * Built for arduino:megaavr:nona4809 that address is 0x7E1D, so the
   * real hardware executed delay(32285) - 32.285 s of blocking delay with
   * the shared reset line held LOW.  This test pins the pulse width of
   * that exact command form to the documented default.
   * ------------------------------------------------------------------ */
  TEST("Format-B '<r> 99 reset' pulse is the documented default, not pointer-derived (REGR #7)");
  {
    DartBus b; b.begin();
    b.send("3 99 reset");
    b.settle();
    std::vector<uint32_t> widths;
    resetPulseWidths(b.hal, widths);
    CHECK_EQ(widths.size(), (size_t)1);
    if (widths.size() == 1) {
      /* Exactly the documented default, allowing only tick granularity. */
      CHECK(widths[0] >= DART_RESET_MS_DEFAULT);
      CHECK(widths[0] <= DART_RESET_MS_DEFAULT + 5u);
      /* And categorically nowhere near the reference's 32285 ms. */
      CHECK(widths[0] < 1000u);
      CHECK(widths[0] != 32285u);
    }
    /* The pulse must also be bounded by the configured maximum, whatever
     * the entry form, so no code path can ever exceed it. */
    CHECK(DART_RESET_MS_DEFAULT <= DART_RESET_MS_MAX);
    CHECK(b.hal.resetHigh);
  }

  TEST("every reset entry form produces the same bounded pulse width");
  {
    struct Form { const char* line; uint32_t expect; };
    const Form forms[] = {
      { "reset",          DART_RESET_MS_DEFAULT },
      { "7 reset",        DART_RESET_MS_DEFAULT },
      { "7 99 reset",     DART_RESET_MS_DEFAULT },
      { "7 reset 250",    250u },
      { "7 reset 2000",   2000u },
      { "16 99 reset",    DART_RESET_MS_DEFAULT }
    };
    for (size_t i = 0; i < sizeof(forms)/sizeof(forms[0]); ++i) {
      DartBus b; b.begin();
      b.send(forms[i].line);
      b.settle();
      std::vector<uint32_t> widths;
      resetPulseWidths(b.hal, widths);
      CHECK_EQ(widths.size(), (size_t)1);
      if (widths.size() == 1) {
        CHECK(widths[0] >= forms[i].expect);
        CHECK(widths[0] <= forms[i].expect + 5u);
        CHECK(widths[0] <= (uint32_t)DART_RESET_MS_MAX + 5u);
      }
      CHECK(b.hal.resetHigh);
    }
  }

  /* ---------------------------------------------------------------- R2 --
   * ACK attribution limits.  The contract has no transaction id and the
   * slave API exposes no master address, so an ACK that arrives with
   * nothing outstanding is UNATTRIBUTABLE.  It must be counted and
   * discarded, never credited to anything.
   * ------------------------------------------------------------------ */
  TEST("an ACK with no transaction outstanding is counted stray and changes nothing");
  {
    DartBus b; b.begin();
    b.settle();
    CHECK_EQ(b.core.txnStage(), DART_TXN_IDLE);
    const size_t edgesBefore = b.hal.channelEdges.size();
    const uint32_t confirmedBefore = b.core.stats().txnConfirmed;

    b.master[3].sendAck();        /* channel 4 ACKs out of nowhere */
    b.master[9].sendAck();        /* and so does channel 10 */
    b.advance(5);                 /* lets tick() publish the ISR counters */

    CHECK(b.core.stats().ackStray >= 2u);
    CHECK_EQ(b.core.stats().txnConfirmed, confirmedBefore);
    CHECK_EQ(b.hal.channelEdges.size(), edgesBefore);   /* no pin activity */
    for (uint8_t r = 1; r <= DART_REMOTE_COUNT; ++r) CHECK_EQ(b.core.heldCount(r), 0);
    CHECK_EQ(b.core.txnStage(), DART_TXN_IDLE);
    CHECK(!b.hal.irqMisuse);
  }

  TEST("a stray ACK during the shared-reset pulse neither confirms nor shortens it");
  {
    DartBus b; b.begin();
    b.send("1 reset 500");
    b.advance(20);
    CHECK(!b.hal.resetHigh);                 /* asserted */
    b.master[0].sendAck();                   /* adversarial ACK mid-pulse */
    b.master[5].sendAck();
    b.advance(100);
    CHECK(!b.hal.resetHigh);                 /* still asserted: not shortened */
    b.settle();
    std::vector<uint32_t> widths;
    resetPulseWidths(b.hal, widths);
    CHECK_EQ(widths.size(), (size_t)1);
    if (widths.size() == 1) {
      CHECK(widths[0] >= 500u);
      CHECK(widths[0] <= 505u);
    }
    CHECK(b.core.stats().ackStray >= 2u);
    CHECK_EQ(b.core.stats().txnConfirmed, 0u);
  }

  /* SCOPE: as above, the late ACK lands while channel 2's DOWN is still only
   * PRESENTED.  The firmware rejects it because nothing had been read yet -
   * NOT because it can attribute the ACK to channel 1.  The slave Wire API
   * exposes no master address, so cross-channel ACK attribution is impossible
   * in this protocol and is never claimed.  See docs/DART-FIRMWARE.md. */
  TEST("a cross-channel late ACK before the next read is counted early, not credited");
  {
    DartBus b; b.begin();
    /* Channel 1 reads but never ACKs: its DOWN ends UNCERTAIN. */
    b.master[0].doAck = false;
    b.send("1 35 down");                     /* SAT 0xEF on channel 1 */
    b.settle();
    CHECK(b.hal.sawOut("key=0xEF act=down result=uncertain"));

    /* Channel 2 now presses; nobody on channel 2 reads it. */
    b.master[1].doRead = false; b.master[1].doAck = false;
    b.hal.clearOut();
    b.send("2 4 down");                      /* GUIDE 0x83 on channel 2 */
    b.advance(6);
    /* Channel 1's delayed ACK finally arrives, aimed at nothing. */
    b.master[0].sendAck();
    b.advance(10);
    CHECK(b.core.txnStage() != DART_TXN_ACKED);
    b.settle();
    /* It must NOT have been credited to channel 2's transaction. */
    CHECK(!b.hal.sawOut("r=2 key=0x83 act=down result=confirmed"));
    CHECK(b.hal.sawOut("r=2 key=0x83 act=down result=failed"));
    CHECK(b.core.stats().ackEarly >= 1u);
    CHECK(!b.core.isHeld(2, 0x83));          /* never read => nothing held */
  }

  /* ---------------------------------------------------------------- R9 --
   * The reference sketch blocked the whole main loop inside its ACK/retry
   * loop, so a release deadline on one channel was missed while another
   * channel was transmitting.  Prove deadlines still land under combined
   * multi-channel, queue and serial pressure.
   * ------------------------------------------------------------------ */
  TEST("release deadlines are met on many channels under combined load");
  {
    DartBus b; b.begin();
    b.send("debug on");
    /* Four firmware-owned holds of 200 ms, interleaved across channels. */
    b.send("1 8B 0B 200");
    b.send("4 83 03 200");
    b.send("8 EF 6F 200");
    b.send("12 8C 0C 200");
    /* Flood the parser with rejects for the whole hold window. */
    for (int i = 0; i < 400; ++i) {
      b.send("garbage that must be rejected");
      b.advance(1);
    }
    b.settle();
    for (uint8_t r = 1; r <= DART_REMOTE_COUNT; ++r) CHECK_EQ(b.core.heldCount(r), 0);
    /* All four releases actually transmitted and were acknowledged. */
    CHECK_EQ(b.hal.countOut("act=up result=confirmed"), 4);
    CHECK(b.core.stats().cmdRejected >= 400u);
    /* Every channel saw exactly one DOWN edge and one UP edge. */
    CHECK_EQ(b.hal.fallingEdges(0), 2);
    CHECK_EQ(b.hal.fallingEdges(3), 2);
    CHECK_EQ(b.hal.fallingEdges(7), 2);
    CHECK_EQ(b.hal.fallingEdges(11), 2);
    CHECK(!b.hal.irqMisuse);
  }

  TEST("a storm of stray ACKs cannot fabricate a confirmation under load");
  {
    DartBus b; b.begin();
    b.master[0].doRead = false; b.master[0].doAck = false;
    b.send("1 4 down");
    /* 200 unattributable ACKs from every other channel while channel 1's
     * key is merely PRESENTED and never read. */
    for (int i = 0; i < 200; ++i) {
      b.master[(uint8_t)(1 + (i % 15))].sendAck();
      b.advance(1);
    }
    b.settle();
    /* Never read => must be reported FAILED, never confirmed or uncertain. */
    CHECK(b.hal.sawOut("act=down result=failed"));
    CHECK(!b.hal.sawOut("result=confirmed"));
    CHECK_EQ(b.core.stats().txnConfirmed, 0u);
    CHECK_EQ(b.core.heldCount(1), 0);
    CHECK((b.core.stats().ackEarly + b.core.stats().ackStray) >= 200u);
    CHECK(!b.hal.irqMisuse);
  }
}

/* ====================================================================== */
int main() {
  printf("=== DART firmware native regression suite ===\n");
  printf("board=%s remotes=%u slots=%u queue=%u watchdog=%ums\n\n",
         DART_BOARD_NAME, (unsigned)DART_REMOTE_COUNT, (unsigned)DART_HELD_SLOTS,
         (unsigned)DART_CMD_QUEUE_LEN, (unsigned)DART_HOST_HOLD_WATCHDOG_MS);

  test_command_forms();
  test_hold_durations();
  test_combinations();
  test_release_all();
  test_ack_faults();
  test_no_stale_cross_channel();
  test_duplicates();
  test_shared_reset();
  test_saturation();
  test_idle_and_recovery();
  test_adversarial_timing();

  /* The harness must itself be sound for the run above to mean anything: no
   * accessor may have read out of range and no observation log may have hit
   * its cap.  A deliberately defective candidate trips these and reports a
   * bounded HARNESS-FAULT instead of crashing the process. */
  TEST("harness integrity: no out-of-range observation and no log cap reached");
  {
    printf("    observation high-water: channelEdges=%zu/%zu resetEdges=%zu/%zu "
           "keysSeen=%zu/%zu out=%zu/%zu bytes\n",
           g_hwChannelEdges, DART_MAX_PIN_EDGES,
           g_hwResetEdges, DART_MAX_PIN_EDGES,
           g_hwKeysSeen, DART_MAX_KEYS_SEEN,
           g_hwOut, DART_MAX_OUT_BYTES);
    CHECK_EQ(g_harnessFaults, 0);
  }

  printf("\n=== %d checks passed, %d failed ===\n", g_pass, g_fail);
  if (g_fail) {
    printf("\nFailures:\n");
    for (size_t i = 0; i < g_failures.size(); ++i)
      printf("  %s\n", g_failures[i].c_str());
  }
  return g_fail ? 1 : 0;
}
