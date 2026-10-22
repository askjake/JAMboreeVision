/* dart_hal.h - the only seam between portable DART logic and real hardware.
 *
 * The identical dart_core.cpp translation unit is compiled three ways:
 *   1. avr-g++ for arduino:megaavr:nona4809  (ArduinoDartHal in the .ino)
 *   2. host g++ for the native regression harness (FakeHal, virtual time)
 *   3. host g++ for the PTY simulator that JAMboreeVision talks to
 * so the tests exercise the real firmware logic, not a reimplementation.
 */
#ifndef DART_HAL_H
#define DART_HAL_H

#include <stdint.h>

class DartHal {
 public:
  virtual ~DartHal() {}
  /* Free-running millisecond counter.  Must be allowed to wrap at 2^32. */
  virtual uint32_t millis() = 0;
  /* Drive one remote interrupt line. channel is 0..DART_REMOTE_COUNT-1. */
  virtual void writeChannel(uint8_t channel, bool high) = 0;
  /* Drive the single physical reset line shared by every remote circuit. */
  virtual void writeSharedReset(bool high) = 0;
  /* Emit NUL-terminated text to the host serial link. Must not block long. */
  virtual void print(const char* text) = 0;
  /* Interrupt-state save/restore. Must restore the previous state, not
   * unconditionally re-enable interrupts (the reference sketch did the latter). */
  virtual uint8_t irqSave() = 0;
  virtual void irqRestore(uint8_t state) = 0;
};

/* RAII critical section: restores the previous interrupt state on scope exit. */
class DartCritical {
 public:
  explicit DartCritical(DartHal& hal) : hal_(hal), state_(hal.irqSave()) {}
  ~DartCritical() { hal_.irqRestore(state_); }
 private:
  DartHal& hal_;
  uint8_t state_;
  DartCritical(const DartCritical&);
  DartCritical& operator=(const DartCritical&);
};

#endif /* DART_HAL_H */
