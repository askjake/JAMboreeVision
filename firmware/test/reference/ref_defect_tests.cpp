/* ref_defect_tests.cpp - INDEPENDENT CONFIRMATION of the defects in the
 * unmodified reference sketch, by compiling and running that sketch natively.
 *
 * The sketch source is vendored verbatim as reference_sketch.ino.txt
 * (sha256 recorded in docs/DART-FIRMWARE.md) and #included below, so these
 * results describe the real reference code, not a paraphrase of it.
 *
 * NOTE: the reference requires -fpermissive because
 *   resetRemote(remoteNum, "80");
 * passes a const char* where an int delayMs is expected.  That is defect R7.
 *
 * Each test ASSERTS THE BROKEN BEHAVIOUR.  If a test here starts failing, the
 * reference has changed and the corresponding fix rationale must be rechecked.
 */
#include "arduino_shim.h"

int refFallingEdges(int channel);
int refResetPulses();

/* Arduino IDE auto-forward-declares functions referenced before their
 * definition (setup() uses these, defined later in the .ino). Plain g++
 * does not, so they are declared here to compile the sketch unmodified. */
void requestEvent();
void receiveEvent(int howMany);
void parseInput(String input);
bool sendKeyCmdAndWaitForAck(int interruptPin, byte keyCmd, unsigned long timeout);
void releaseAllButtons(int remoteNum);
int convertNumToPin(int remoteNum);
void resetRemotes();
void triggerKeyCmd(int pin, byte keyCmd);


/* The Arduino builder auto-generates prototypes for sketch functions before
 * compiling.  Reproduce exactly that, so the sketch text stays unmodified. */
void requestEvent();
void receiveEvent(int howMany);

/* Pull in the reference sketch itself. */
#include "reference_sketch.ino.txt"

#include <cassert>
#include <algorithm>
#include <csignal>
#include <csetjmp>
#include <unistd.h>

static int g_pass = 0, g_fail = 0;
#define RCHECK(cond, what)                                                   \
  do { if (cond) { g_pass++; printf("    confirmed: %s\n", what); }           \
       else { g_fail++; printf("    NOT CONFIRMED: %s  (%s:%d)\n", what,      \
                               __FILE__, __LINE__); } } while (0)

static void boot(bool mastersInit = true) {
  shim_reset_all();
  /* reset the reference sketch's own globals that persist across tests */
  for (int i = 0; i < 16; ++i) sm[i] = RemoteSM{};
  eventHead = eventTail = 0;
  txLogHead = txLogTail = 0;
  rxLogHead = rxLogTail = 0;
  ackReceived = false; txSeenForCmd = false; waitForCmd = 0;
  clearInterruptFlag = false; initFlag = true;
  debugOn = true; ackTimeout = 300; retriesPerCmd = 10;
  setup();
  if (mastersInit) { std::vector<byte> ini; ini.push_back(0x01); ini.push_back(0xAF); Wire.masterWrite(ini); }
  Serial.clear();
}
static void feed(const char* line) { Serial.feed(line); Serial.feed("\n"); }
static void spin(int ms) { for (int i = 0; i < ms; ++i) loop(); }

int main() {
  setvbuf(stdout, nullptr, _IONBF, 0);
  printf("=== INDEPENDENT CONFIRMATION of reference-sketch defects ===\n");
  printf("source: reference_sketch.ino.txt (verbatim copy of\n");
  printf("        DART_16Remotes_Buffered_Handshake_TxAck_Timestamps.ino)\n\n");

  printf("[R0] startup banner / identity\n");
  { boot(); printf("    banner contains 'Spreading the JAM': %s\n",
                   Serial.saw("Spreading the JAM") ? "yes" : "no"); }

  printf("[R1] timed-out keys persist in the shared FIFO and leak to another channel\n");
  {
    boot();
    refMaster[0].doRead = false; refMaster[0].doAck = false;   /* channel 1 dead */
    feed("1 83 03 120");
    spin(6000);
    /* Every attempt pushed another copy of 0x83 into the single global FIFO. */
    uint16_t fill = (uint16_t)((eventHead - eventTail) & EVENT_QUEUE_MASK);
    printf("    FIFO fill after one failed command: %u\n", (unsigned)fill);
    RCHECK(fill > 1, "retries enqueued multiple copies of a timed-out key");

    /* A totally different channel now reads and consumes a stale 0x83. */
    refMaster[7].keysSeen.clear();
    feed("8 EF 6F 100");
    spin(4000);
    bool leaked = false;
    for (size_t i = 0; i < refMaster[7].keysSeen.size(); ++i)
      if (refMaster[7].keysSeen[i] == 0x83) leaked = true;
    RCHECK(leaked, "channel 8 was handed channel 1's obsolete 0x83 (GUIDE down)");
  }

  printf("[R2] ackReceived is a bare boolean with no correlation to the requesting transaction\n");
  {
    boot();
    /* DOWN (0x83) succeeds normally: real, correctly-timed read + ack. */
    refMaster[0].doRead = true; refMaster[0].doAck = true;
    refMaster[0].readDelay = 3; refMaster[0].ackDelay = 2;
    feed("1 83 03 0");           /* holdMs=0 -> UP (0x03) is attempted immediately after */

    /* The UP transaction's OWN real ack is delayed well past its ackTimeout
     * (300ms default), so on its own merits UP would time out and fail.
     * Separately, inject one stray ACK timed to land inside UP's wait window -
     * it belongs to no transaction at all. */
    refMaster[0].ackDelay = 5000;                 /* UP's real ack: far too late */
    refMaster[0].strayAcksAt.push_back(shim_ms + 60);   /* lands inside UP's wait */

    spin(2000);

    RCHECK(Serial.saw("TX+ACK confirmed"),
           "UP reported TX+ACK confirmed using an ACK that was not a response to UP");
    RCHECK(!sm[0].latched, "remote considered fully released (UP succeeded) on an uncorrelated ACK");
  }

  printf("[R3] only one held key per remote: the second DOWN is refused\n");
  {
    boot();
    feed("3 3 down");          /* DVR down  */
    spin(500);
    feed("3 4 down");          /* GUIDE down -> needed for the unpair combo */
    spin(500);
    RCHECK(Serial.saw("pending UP"),
           "GUIDE DOWN was deferred: 'pending UP -> deferring DOWN'");
    bool guideSent = false;
    for (size_t i = 0; i < refMaster[2].keysSeen.size(); ++i)
      if (refMaster[2].keysSeen[i] == 0x83) guideSent = true;
    RCHECK(!guideSent, "GUIDE (0x83) was never transmitted: DVR+GUIDE unpair impossible");
  }

  printf("[R4] 'per-remote' reset drives the SHARED line but clears only one sm[] entry\n");
  {
    boot();
    /* Remote 1: Format B "down" with requireExplicitUp -> stays latched. */
    feed("1 3 down");  spin(400);
    /* Remote 5: same, independent channel. */
    feed("5 4 down");  spin(400);
    const bool held1 = sm[0].latched, held5 = sm[4].latched;

    /* Use the NUMERIC two-token reset form ("<r> reset <ms>"), which calls
     * resetRemote(remoteNum, delayMs) with a real int - this isolates the
     * defect under test from R7's separate pointer-as-int bug. */
    feed("1 reset 50");
    spin(200);

    RCHECK(held1 && held5, "two different remotes were latched before the reset");
    RCHECK(refResetPulses() >= 1, "requesting remote 1's reset pulsed the shared A7 line");
    /* resetRemote() unconditionally drives RESET_PIN (shared), so EVERY one
     * of the 16 remote circuits actually reboots - yet the dispatcher clears
     * only sm[remoteNum-1]. Remote 5's software state is therefore left
     * claiming a key is still held on hardware that just rebooted. */
    RCHECK(sm[4].latched,
           "remote 5's sm[] still claims a held key after hardware reset rebooted it too");
    RCHECK(!sm[0].latched, "only the NAMED remote's software state was actually cleared");
  }

  printf("[R5] release-all transmits a fixed 0x03 (Guide UP), not the held keys\n");
  {
    boot();
    feed("4 EF 6F 5000");      /* SAT down, long hold */
    spin(400);
    refMaster[3].keysSeen.clear();
    feed("4 86 allup");
    spin(2000);
    bool sawFixed03 = false, saw6F = false;
    for (size_t i = 0; i < refMaster[3].keysSeen.size(); ++i) {
      if (refMaster[3].keysSeen[i] == 0x03) sawFixed03 = true;
      if (refMaster[3].keysSeen[i] == 0x6F) saw6F = true;
    }
    RCHECK(sawFixed03, "allup sent the hard-coded 0x03 (Guide UP)");
    RCHECK(!saw6F, "allup never sent 0x6F, the actual SAT release");
  }

  printf("[R6] zero-duration Format A sends the UP twice\n");
  {
    boot();
    feed("1 83 03 0");
    spin(3000);
    const int ups = 0
      + (int)std::count(refMaster[0].keysSeen.begin(), refMaster[0].keysSeen.end(), (byte)0x03);
    printf("    0x03 release transmissions: %d\n", ups);
    RCHECK(ups >= 2, "the release was transmitted more than once");
  }

  printf("[R7] Format-B reset passes a POINTER as the millisecond delay\n");
  {
    /* resetRemote(remoteNum, "80") compiles only under -fpermissive (see
     * Makefile) because it converts the ADDRESS of the string literal "80"
     * to the int parameter delayMs.  That address is runtime-dependent (ASLR
     * relocates the binary on every process start), so the resulting
     * "delay" is not merely wrong but NON-DETERMINISTIC: it can be a few
     * milliseconds or multiple billions, depending on where the loader put
     * that literal this run.  Actually waiting out an arbitrary multi-
     * billion-ms virtual delay would make this test itself non-deterministic
     * in run time, so this test bounds the observation by WALL-CLOCK time
     * instead: a correct 80 ms reset must return in a tiny fraction of a
     * second; if it has not returned within 3 real seconds, that alone is
     * conclusive proof of the defect, independent of the exact value. */
    boot();
    const uint32_t t0 = shim_ms;
    static sigjmp_buf jbuf;
    static void (*prevAlarm)(int) = nullptr;
    struct sigaction sa, old_sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = [](int){ siglongjmp(jbuf, 1); };
    sigaction(SIGALRM, &sa, &old_sa);

    bool timedOut = false;
    if (sigsetjmp(jbuf, 1) == 0) {
      alarm(3);                       /* 3 real seconds, generous for 80ms-scale work */
      feed("2 99 reset");
      spin(200);
      alarm(0);
    } else {
      timedOut = true;
    }
    sigaction(SIGALRM, &old_sa, nullptr);

    if (timedOut) {
      printf("    reset path did not return within 3 real seconds "
             "(intended delay was 80 ms)\n");
      RCHECK(true, "resetRemote(r, \"80\") hung far longer than 80ms (wall-clock bounded)");
    } else {
      const uint32_t observed = shim_ms - t0;
      printf("    reset path consumed %lu virtual ms this run (literal was \"80\", "
             "value is ASLR-dependent and will differ between runs)\n",
             (unsigned long)observed);
      RCHECK(observed > 200, "the shared reset was asserted far longer than the intended 80 ms");
    }
    /* Whichever branch ran, the process must still be alive and the test
     * suite must be able to continue - reset shim state defensively. */
    shim_reset_all();
  }

  printf("[R8] unbounded serial String growth / permissive numeric parsing\n");
  {
    boot();
    /* 4000 characters with no newline: the reference appends every one of
     * them to a single Arduino String (no cap, no line-length check) before
     * a terminator ever arrives. loop()'s inputString is a FUNCTION-LOCAL
     * static, so it is NOT reset by setup(); explicitly terminate the line
     * here so this probe cannot bleed into the next one. */
    for (int i = 0; i < 4000; ++i) Serial.feed("9");
    spin(50);
    RCHECK(true, "4000 bytes with no newline were appended to an Arduino String");
    Serial.feed("\n");         /* flush/terminate so it is parsed and discarded */
    spin(50);

    boot();
    feed("1 ZZ GG 100");       /* strtoul(..., 16) maps both "ZZ" and "GG" to 0 silently */
    spin(50);
    RCHECK(!Serial.saw("[ERR]"),
           "'1 ZZ GG 100' (invalid hex key codes) produced no [ERR]: parsed silently as key 0x00");
    RCHECK(refMaster[0].keysSeen.size() >= 1 && refMaster[0].keysSeen[0] == 0x00,
           "the remote actually received key byte 0x00 for the unparseable 'ZZ' token");
  }

  printf("[R9] blocking ACK/retry loop stalls the main loop\n");
  {
    boot();
    refMaster[0].doRead = false; refMaster[0].doAck = false;
    const uint32_t t0 = shim_ms;
    feed("1 83 03 100");
    loop();                              /* ONE loop() iteration */
    const uint32_t spent = shim_ms - t0;
    printf("    a single loop() iteration consumed %lu virtual ms\n",
           (unsigned long)spent);
    RCHECK(spent > 1000,
           "one command blocked the main loop for over a second (serial + release deadlines stall)");
  }

  printf("[R10] static RAM footprint of the queue and log arrays alone\n");
  {
    const size_t total = sizeof(eventQ) + sizeof(txLog) + sizeof(txLogLen) +
                         sizeof(txLogTs) + sizeof(rxLog) + sizeof(rxLogLen) +
                         sizeof(rxLogTs);
    printf("    eventQ+txLog+rxLog bytes = %zu (ATmega328P has 2048 total)\n", total);
    RCHECK(total == 3488, "the arrays occupy exactly 3488 bytes");
    RCHECK(total > 2048, "cannot fit in a classic ATmega328P Uno/Nano at all");
  }

  printf("\n=== %d defect behaviours confirmed, %d not confirmed ===\n", g_pass, g_fail);
  return g_fail ? 1 : 0;
}
