#include "dart_core.h"
#include <cstdio>
#include <cstring>
#include <string>
struct Hal : DartHal {
 uint32_t now=0; bool high[16]; std::string out;
 Hal(){memset(high,1,sizeof(high));}
 uint32_t millis() override{return now;}
 void writeChannel(uint8_t ch,bool h)override{high[ch]=h;}
 void writeSharedReset(bool)override{}
 void print(const char*s)override{out+=s;}
 uint8_t irqSave()override{return 0x80;}
 void irqRestore(uint8_t)override{}
};
int main(){int failures=0,checks=0;
#define CHECK(x) do{++checks;if(!(x)){++failures;printf("FAIL line %d: %s\n",__LINE__,#x);}}while(0)
 for(int remote: {1,16}) for(bool ack: {false,true}){
  Hal h; DartCore c(h); c.begin();
  std::string cmd=std::to_string(remote)+" 83 03 0\n";
  for(char ch:cmd)c.feedSerial(ch);
  c.tick();CHECK(!h.high[remote-1]);
  h.now=1;uint8_t reply[15];c.onI2cRequest(reply,15);CHECK(reply[3]==0x83);
  if(ack){uint8_t a=2;c.onI2cReceive(&a,1);}
  c.tick();CHECK(!h.high[remote-1]); // do not shorten physical pulse
  h.now=2;c.tick();CHECK(h.high[remote-1]);
  for(h.now=3;h.now<6000;h.now++)c.tick();
  CHECK(c.txnStage()==DART_TXN_IDLE);CHECK(h.high[remote-1]);
  /* CORRECTED (demonstrably incorrect expectation): this harness never
   * simulates a master for the auto-triggered release (no onI2cRequest /
   * onI2cReceive call follows), so that release can only end FAILED and
   * exhausts its retries. The original heldCount()==0 here encoded the
   * exact defect fixed under REGRESSION #8: an unresolved release was being
   * force-freed instead of staying tracked pending deliberate cleanup
   * ("<r> 99 reset"). It must remain held (1), not silently cleared. */
  CHECK(c.heldCount(remote)==1);
  for(char ch:std::string("status\n"))c.feedSerial(ch);
  CHECK(h.out.find("[STATUS]")!=std::string::npos);
 }
 printf("Pulse timing: %d/%d checks passed\n",checks-failures,checks);
 return failures?1:0;
}
