#ifndef DART_MOCK_WIRE_H
#define DART_MOCK_WIRE_H
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include <string>
#include <deque>
#include <vector>
#define NUM_DIGITAL_PINS 22
#define PIN_A0 14
#define PIN_A1 15
#define PIN_A2 16
#define PIN_A3 17
#define PIN_A7 21
#define PIN_WIRE_SDA 22
#define PIN_WIRE_SCL 23
#define HIGH 1
#define LOW 0
#define OUTPUT 1
extern uint32_t mockNow;
extern bool mockPins[26];
extern uint8_t SREG;
inline void cli(){SREG=0;}
inline uint32_t millis(){return mockNow;}
inline void digitalWrite(uint8_t p,int h){mockPins[p]=(h==HIGH);}
inline void pinMode(uint8_t,int){}
struct MockSerial {
 int room=63;bool blockedWrite=false;std::string output;std::deque<char> input;
 void begin(int){}int available(){return input.size();}int read(){char c=input.front();input.pop_front();return c;}
 int availableForWrite(){return room;}
 size_t write(const uint8_t*p,size_t n){if(n>(size_t)room){blockedWrite=true;return 0;}room-=n;output.append((const char*)p,n);return n;}
 void feed(const char*s){for(;*s;s++)input.push_back(*s);}
};
struct MockWire {
 void(*request)()=0;void(*receive)(int)=0;std::deque<uint8_t> input;std::vector<uint8_t> output;
 void begin(int){}void onRequest(void(*f)()){request=f;}void onReceive(void(*f)(int)){receive=f;}
 int available(){return input.size();}int read(){int v=input.front();input.pop_front();return v;}
 size_t write(const uint8_t*p,size_t n){output.assign(p,p+n);return n;}
};
extern MockSerial Serial;extern MockWire Wire;
#endif
