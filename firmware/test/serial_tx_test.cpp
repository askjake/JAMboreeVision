#include "dart_serial_tx.h"
#include "dart_core.h"
#include <string>
#include <cstdio>
#include <cstring>
struct Uart {
 int room=63, calls=0; bool oversize=false; size_t limit=63; std::string output;
 int availableForWrite(){++calls;return room;}
 size_t write(const uint8_t*p,size_t n){if(n>(size_t)room)oversize=true;size_t k=n<limit?n:limit;output.append((const char*)p,k);return k;}
};
struct Hal:DartHal {
 DartSerialTx tx; int dropped=0;
 uint32_t millis()override{return 0;}void writeChannel(uint8_t,bool)override{}void writeSharedReset(bool)override{}
 void print(const char*s)override{if(!tx.enqueue(s))++dropped;}
 uint8_t irqSave()override{return 128;}void irqRestore(uint8_t)override{}
};
int main(){int fail=0,checks=0;
#define CHECK(x) do{++checks;if(!(x)){++fail;printf("FAIL line %d: %s\n",__LINE__,#x);}}while(0)
 Hal h;DartCore c(h);Uart u;c.begin();
 for(int i=0;i<100;i++)h.tx.pump(u);
 CHECK(u.output.find("DART-BRIDGE")!=std::string::npos);CHECK(u.output.find("serial-proto=")!=std::string::npos);CHECK(h.dropped==0);
 u.output.clear();for(char x:std::string("help\n"))c.feedSerial(x);
 for(int i=0;i<100;i++)h.tx.pump(u);
 CHECK(h.dropped==0);CHECK(u.output.find("NOT receiver/STB action\r\n")!=std::string::npos);
 u.output.clear();for(char x:std::string("qstat\n"))c.feedSerial(x);
 for(int i=0;i<100;i++)h.tx.pump(u);
 size_t n=0,pos=0;while((pos=u.output.find("[Q]",pos))!=std::string::npos){n++;pos+=3;}CHECK(n==3);CHECK(!u.oversize);
 DartSerialTx q;Uart stalled;stalled.room=0;std::string full(1024,'A');CHECK(q.enqueue(full.c_str()));CHECK(!q.enqueue("rejected"));q.pump(stalled);CHECK(q.pending()==1024);CHECK(stalled.output.empty());
 stalled.room=1;stalled.limit=1;for(int i=0;i<1100;i++)q.pump(stalled);CHECK(stalled.output==full);CHECK(q.pending()==0);
 for(int pass=0;pass<4;pass++){std::string msg(700,'a'+pass);CHECK(q.enqueue(msg.c_str()));for(int i=0;i<800;i++)q.pump(stalled);}
 CHECK(stalled.output==full+std::string(700,'a')+std::string(700,'b')+std::string(700,'c')+std::string(700,'d'));
 Uart partial;partial.limit=3;CHECK(q.enqueue("partial-write-safe\r\n"));for(int i=0;i<20;i++)q.pump(partial);CHECK(partial.output=="partial-write-safe\r\n");
 printf("UART queue: %d/%d checks passed\n",checks-fail,checks);return fail?1:0;
}
