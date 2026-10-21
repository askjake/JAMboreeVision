/* pty_sim.cpp - exposes the REAL dart_core.cpp firmware logic on a virtual
 * serial port (a PTY) so JAMboreeVision can be driven end-to-end without any
 * physical board.
 *
 * WHAT THIS IS / IS NOT
 *   - It links the SAME dart_core.cpp translation unit the Arduino build
 *     compiles, and the SAME edge-driven RemoteMaster bus model used by the
 *     native regression suite.  The remote model reacts only to interrupt-line
 *     edges and to the 15-byte register read; it never inspects firmware state.
 *   - It is NOT an MCU emulator: this is host-native x86-64 code.  AVR
 *     instruction-level execution is covered separately by emu/avr_emu_tests.
 *   - Virtual time is slaved to the host monotonic clock so that the
 *     application's real time.sleep() based holds behave realistically.
 *
 * Fault injection (so honest failure reporting can be proven):
 *   DART_SIM_NOREAD="3,7"   channels (1-based) whose master never reads  -> failed
 *   DART_SIM_NOACK="5"      channels whose master reads but never ACKs   -> uncertain
 *   DART_SIM_LATEACK="9"    channels whose ACK arrives far too late
 */
#include "dart_test_harness.h"

#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <signal.h>
#include <time.h>
#include <termios.h>
#include <set>
#include <string>

int g_pass = 0, g_fail = 0;
std::vector<std::string> g_failures;
std::string g_currentTest;

static volatile sig_atomic_t g_stop = 0;
static void onSig(int) { g_stop = 1; }

static uint64_t nowMs() {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (uint64_t)ts.tv_sec * 1000ull + (uint64_t)(ts.tv_nsec / 1000000ull);
}

static std::set<int> parseChannels(const char* env) {
  std::set<int> out;
  const char* v = getenv(env);
  if (!v) return out;
  const char* p = v;
  while (*p) {
    char* end = 0;
    long n = strtol(p, &end, 10);
    if (end == p) break;
    if (n >= 1 && n <= DART_REMOTE_COUNT) out.insert((int)n);
    p = (*end == ',') ? end + 1 : end;
  }
  return out;
}

int main(void) {
  signal(SIGINT, onSig);
  signal(SIGTERM, onSig);

  int mfd = posix_openpt(O_RDWR | O_NOCTTY);
  if (mfd < 0) { perror("posix_openpt"); return 2; }
  if (grantpt(mfd) || unlockpt(mfd)) { perror("grantpt/unlockpt"); return 2; }
  const char* slave = ptsname(mfd);
  if (!slave) { perror("ptsname"); return 2; }

  /* Put the PTY into raw mode BEFORE anything is written.  A PTY defaults to
   * ECHO on, which would echo everything the firmware prints back into the
   * firmware's own input and have it parsed as host commands.  A real UART
   * does not do this, so echo and all line processing are disabled. */
  struct termios tio;
  if (tcgetattr(mfd, &tio) == 0) {
    cfmakeraw(&tio);
    tio.c_cc[VMIN] = 0;
    tio.c_cc[VTIME] = 0;
    tcsetattr(mfd, TCSANOW, &tio);
  }
  fcntl(mfd, F_SETFL, O_NONBLOCK);

  printf("PTY %s\n", slave);
  fflush(stdout);

  DartBus bus;
  /* NOTE: deliberately NOT DartBus::begin(), which clears the captured output.
   * A real board prints its identity banner at start-up and the host must be
   * able to see it, so the banner is retained and flushed to the PTY. */
  bus.core.begin();
  bus.master[0].sendInit();
  bus.allMastersIdeal();

  const std::set<int> noRead  = parseChannels("DART_SIM_NOREAD");
  const std::set<int> noAck   = parseChannels("DART_SIM_NOACK");
  const std::set<int> lateAck = parseChannels("DART_SIM_LATEACK");
  for (int c = 1; c <= DART_REMOTE_COUNT; ++c) {
    if (noRead.count(c))  bus.master[c - 1].doRead = false;
    if (noAck.count(c))   bus.master[c - 1].doAck  = false;
    if (lateAck.count(c)) bus.master[c - 1].ackDelayMs = 5000;
  }
  fprintf(stderr, "sim: noread=%zu noack=%zu lateack=%zu\n",
          noRead.size(), noAck.size(), lateAck.size());

  /* Re-emit the banner now that the clock baseline is set. */
  const uint64_t t0 = nowMs();
  std::string hostLine;

  while (!g_stop) {
    /* 1. advance firmware virtual time to track the wall clock */
    const uint32_t target = (uint32_t)(nowMs() - t0);
    uint32_t guard = 0;
    while (bus.hal.now < target && guard++ < 5000) bus.advance(1);

    /* 2. ingest host bytes exactly as the UART would */
    char rb[256];
    ssize_t n = read(mfd, rb, sizeof(rb));
    if (n > 0) {
      for (ssize_t i = 0; i < n; ++i) {
        bus.core.feedSerial(rb[i]);
        if (rb[i] == '\n' || rb[i] == '\r') {
          if (!hostLine.empty()) {
            fprintf(stderr, "HOSTLINE t=%u %s\n", (unsigned)bus.hal.now, hostLine.c_str());
            fflush(stderr);
            hostLine.clear();
          }
        } else {
          hostLine += rb[i];
        }
      }
    }

    /* 3. flush firmware output back to the host */
    if (!bus.hal.out.empty()) {
      const std::string chunk = bus.hal.out;
      bus.hal.out.clear();
      size_t off = 0;
      while (off < chunk.size()) {
        ssize_t w = write(mfd, chunk.data() + off, chunk.size() - off);
        if (w <= 0) break;
        off += (size_t)w;
      }
      fprintf(stderr, "FWOUT %s", chunk.c_str());
      fflush(stderr);
    }

    usleep(500);
  }
  fprintf(stderr, "sim: stopping; confirmed=%lu uncertain=%lu failed=%lu resets=%lu stray=%lu\n",
          (unsigned long)bus.core.stats().txnConfirmed,
          (unsigned long)bus.core.stats().txnUncertain,
          (unsigned long)bus.core.stats().txnFailed,
          (unsigned long)bus.core.stats().sharedResets,
          (unsigned long)bus.core.stats().ackStray);
  return 0;
}
