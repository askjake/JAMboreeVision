/* arduino_shim.h - minimal Arduino/Wire/Serial emulation so the UNMODIFIED
 * reference sketch
 *   JAMboreeLite/DART_16Remotes_Buffered_Handshake_TxAck_Timestamps/
 *     DART_16Remotes_Buffered_Handshake_TxAck_Timestamps.ino
 * can be compiled and executed natively.  This exists only to INDEPENDENTLY
 * CONFIRM the defects before they were fixed; it is not used by the shipping
 * firmware or by dart_tests.cpp.
 *
 * Concurrency model: the reference sketch blocks inside delay() and inside a
 * millis() busy-wait.  Therefore millis(), micros() and delay() all advance a
 * virtual clock and pump the simulated DART masters, which is what lets the
 * blocking handshake complete (or time out) exactly as it would on hardware.
 */
#ifndef ARDUINO_SHIM_H
#define ARDUINO_SHIM_H

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cctype>
#include <cstdlib>
#include <string>
#include <vector>

typedef uint8_t byte;
typedef bool boolean;

#define HIGH 1
#define LOW 0
#define OUTPUT 1
#define INPUT 0

/* Nano Every digital indexes (see variants/nona4809/pins_arduino.h). */
enum { A0 = 14, A1 = 15, A2 = 16, A3 = 17, A4 = 18, A5 = 19, A6 = 20, A7 = 21 };

/* ------------------------------ clock ---------------------------------- */
extern uint32_t shim_ms;
extern uint32_t shim_us;
void shim_pump();            /* advance 1 ms and step the simulated masters */

inline unsigned long millis() { shim_pump(); return shim_ms; }
inline unsigned long micros() { shim_pump(); return shim_us; }

/* Diagnostic-only cap so a confirmed pointer-derived garbage delay (defect R7)
 * cannot hang the TEST PROCESS itself. Real hardware would genuinely sit
 * unresponsive for that same implementation-defined duration; this cap only
 * protects the harness's wall-clock budget. Every call's requested value is
 * recorded regardless of whether it was capped, so the defect is still
 * provable from shim_lastDelayRequested / shim_delayWasCapped. */
extern unsigned long shim_lastDelayRequested;
extern bool shim_delayWasCapped;
extern unsigned long shim_delayPumpCapMs;

inline void delay(unsigned long ms) {
  shim_lastDelayRequested = ms;
  if (ms > shim_delayPumpCapMs) {
    shim_delayWasCapped = true;
    for (unsigned long i = 0; i < shim_delayPumpCapMs; ++i) shim_pump();
    shim_ms += (uint32_t)(ms - shim_delayPumpCapMs);   /* clock still advances */
    shim_us += (uint32_t)(ms - shim_delayPumpCapMs) * 1000u;
    return;
  }
  for (unsigned long i = 0; i < ms; ++i) shim_pump();
}
inline void delayMicroseconds(unsigned int) { }

/* ------------------------------- GPIO ---------------------------------- */
extern int shim_pinState[32];
extern std::vector<std::pair<uint32_t, int> > shim_fallingEdges;  /* (t, pin) */
void pinMode(int pin, int mode);
void digitalWrite(int pin, int value);
inline int digitalRead(int pin) { return shim_pinState[pin]; }

/* ---------------------------- interrupts -------------------------------- */
extern int shim_irqDepth;
inline void noInterrupts() { shim_irqDepth++; }
inline void interrupts()   { shim_irqDepth--; }

/* ------------------------------ String ---------------------------------- */
class String {
 public:
  std::string s;
  String() {}
  String(const char* p) : s(p ? p : "") {}
  String(const std::string& p) : s(p) {}
  String(int v) { char b[24]; snprintf(b, sizeof(b), "%d", v); s = b; }
  String(unsigned long v) { char b[24]; snprintf(b, sizeof(b), "%lu", v); s = b; }
  String(char c) { s = std::string(1, c); }
  int length() const { return (int)s.size(); }
  const char* c_str() const { return s.c_str(); }
  char operator[](int i) const { return s[(size_t)i]; }
  String& operator+=(char c) { s += c; return *this; }
  String& operator+=(const String& o) { s += o.s; return *this; }
  String& operator+=(const char* o) { s += o; return *this; }
  String operator+(const String& o) const { return String(s + o.s); }
  String operator+(const char* o) const { return String(s + o); }
  bool operator==(const char* o) const { return s == std::string(o); }
  bool operator!=(const char* o) const { return s != std::string(o); }
  void trim() {
    size_t a = s.find_first_not_of(" \t\r\n");
    size_t b = s.find_last_not_of(" \t\r\n");
    s = (a == std::string::npos) ? "" : s.substr(a, b - a + 1);
  }
  void toUpperCase() { for (size_t i = 0; i < s.size(); ++i) s[i] = (char)toupper(s[i]); }
  void toLowerCase() { for (size_t i = 0; i < s.size(); ++i) s[i] = (char)tolower(s[i]); }
  bool startsWith(const char* p) const { return s.rfind(p, 0) == 0; }
  bool equalsIgnoreCase(const char* p) const {
    std::string a = s, b = p;
    for (size_t i = 0; i < a.size(); ++i) a[i] = (char)tolower(a[i]);
    for (size_t i = 0; i < b.size(); ++i) b[i] = (char)tolower(b[i]);
    return a == b;
  }
  String substring(int from) const {
    if (from >= (int)s.size()) return String();
    return String(s.substr((size_t)from));
  }
  String substring(int from, int to) const {
    if (from >= (int)s.size()) return String();
    return String(s.substr((size_t)from, (size_t)(to - from)));
  }
  long toInt() const { return strtol(s.c_str(), 0, 10); }
};
inline String operator+(const char* a, const String& b) { return String(std::string(a) + b.s); }

/* ------------------------------ Serial ---------------------------------- */
class SerialShim {
 public:
  std::string out;
  std::string in;
  void begin(unsigned long) {}
  void flush() {}
  int available() { return (int)in.size(); }
  int read() { if (in.empty()) return -1; char c = in[0]; in.erase(0, 1); return (unsigned char)c; }
  int availableForWrite() { return 1024; }
  void feed(const char* s) { in += s; }

  void print(const String& v) { out += v.s; }
  void print(const char* v) { out += v; }
  void print(char v) { out += v; }
  void print(int v) { char b[24]; snprintf(b, sizeof(b), "%d", v); out += b; }
  void print(unsigned int v) { char b[24]; snprintf(b, sizeof(b), "%u", v); out += b; }
  void print(long v) { char b[24]; snprintf(b, sizeof(b), "%ld", v); out += b; }
  void print(unsigned long v) { char b[24]; snprintf(b, sizeof(b), "%lu", v); out += b; }
  void print(bool v) { out += (v ? "1" : "0"); }
  void print(byte v) { char b[24]; snprintf(b, sizeof(b), "%u", (unsigned)v); out += b; }

  template <class T> void println(T v) { print(v); out += "\n"; }
  void println() { out += "\n"; }

  bool saw(const char* needle) const { return out.find(needle) != std::string::npos; }
  int count(const char* needle) const {
    int n = 0; size_t p = 0; const size_t L = strlen(needle);
    while ((p = out.find(needle, p)) != std::string::npos) { n++; p += L; }
    return n;
  }
  void clear() { out.clear(); }
};
extern SerialShim Serial;

/* -------------------------------- Wire ---------------------------------- */
class WireShim {
 public:
  void begin(int addr) { address = addr; }
  void onRequest(void (*cb)()) { onRequestCb = cb; }
  void onReceive(void (*cb)(int)) { onReceiveCb = cb; }

  /* slave->master */
  void write(byte v) { txBuf.push_back(v); }
  /* master->slave */
  int available() { return (int)(rxBuf.size() - rxPos); }
  int read() { return rxPos < rxBuf.size() ? (int)rxBuf[rxPos++] : -1; }

  /* driven by the simulated master */
  std::vector<byte> masterRead() {
    txBuf.clear();
    if (onRequestCb) onRequestCb();
    return txBuf;
  }
  void masterWrite(const std::vector<byte>& bytes) {
    rxBuf = bytes; rxPos = 0;
    if (onReceiveCb) onReceiveCb((int)bytes.size());
  }

  int address = 0;
  void (*onRequestCb)() = nullptr;
  void (*onReceiveCb)(int) = nullptr;
  std::vector<byte> txBuf, rxBuf;
  size_t rxPos = 0;
};
extern WireShim Wire;

/* ------------------- simulated DART masters (reference) ----------------- */
struct RefMaster {
  bool doRead = true, doAck = true;
  uint32_t readDelay = 3, ackDelay = 2;
  bool pollWhenIdle = false;
  int reads = 0, acks = 0;
  std::vector<byte> keysSeen;
  /* Extra ACKs injected at an exact virtual time, independent of any read.
   * Used only by test code, to demonstrate that the reference firmware's
   * ackReceived flag is a bare boolean uncorrelated with which transaction
   * requested it: ANY ack, from any source, satisfies ackReceived. */
  std::vector<uint32_t> strayAcksAt;
  /* internal */
  bool wasLow = false, pendRead = false, pendAck = false;
  uint32_t readAt = 0, ackAt = 0, lastPoll = 0;
};
extern RefMaster refMaster[16];
extern bool shim_mastersEnabled;
void shim_reset_all();

/* One-shot scheduled out-of-band ACK, used only to construct a deterministic
 * "ACK arrives before any master reads the current key" scenario regardless
 * of the real master models' own read/ack timing. This models a late ACK
 * from an EARLIER transaction landing during a NEW one - exactly the
 * ambiguity the shared ackReceived flag (defect R2) cannot distinguish. */
extern bool shim_scheduledAckArmed;
extern uint32_t shim_scheduledAckAtMs;
void shim_armScheduledAck(uint32_t atMs);

#endif /* ARDUINO_SHIM_H */
