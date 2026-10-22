#ifndef DART_SERIAL_TX_H
#define DART_SERIAL_TX_H
#include <stdint.h>
#include <stddef.h>
#include <string.h>

/* Main-loop-only queue. Admit whole messages, then drain in bounded chunks.
 * UART capacity is a per-write limit, not a maximum diagnostic-line length.
 * Overflow rejects a whole message; caller counts it. No heap, no waiting. */
class DartSerialTx {
 public:
  static const uint16_t capacity = 1024;
  DartSerialTx() : head_(0), tail_(0), used_(0) {}
  bool enqueue(const char* text) {
    const size_t n = strlen(text);
    if (n > (size_t)(capacity - used_)) return false;
    for (size_t i=0; i<n; ++i) {
      bytes_[head_] = (uint8_t)text[i];
      head_ = (uint16_t)((head_ + 1) % capacity);
    }
    used_ = (uint16_t)(used_ + n);
    return true;
  }
  template<class Uart> void pump(Uart& uart, uint16_t budget=32) {
    const int available = uart.availableForWrite();
    if (available <= 0 || !used_) return;
    uint16_t n = used_;
    if (n > budget) n = budget;
    if (n > available) n = (uint16_t)available;
    if (n > capacity-tail_) n = (uint16_t)(capacity-tail_);
    const size_t written = uart.write(bytes_+tail_, n);
    const uint16_t accepted = written > n ? n : (uint16_t)written;
    tail_ = (uint16_t)((tail_ + accepted) % capacity);
    used_ = (uint16_t)(used_ - accepted);
  }
  uint16_t pending() const { return used_; }
 private:
  uint8_t bytes_[capacity];
  uint16_t head_,tail_,used_;
};
#endif
