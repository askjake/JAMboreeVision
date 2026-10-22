#include "arduino_mock/Wire.h"
uint32_t mockNow=0;bool mockPins[26]={};uint8_t SREG=128;MockSerial Serial;MockWire Wire;
#include "../dart_remote_bridge/dart_remote_bridge.ino"
int main(){int checks=0,fail=0;
#define CHECK(x) do{++checks;if(!(x)){++fail;printf("FAIL line %d: %s\n",__LINE__,#x);}}while(0)
 setup();
 for(int i=0;i<100;i++){Serial.room=63;loop();mockNow++;}
 CHECK(Serial.output.find("DART-BRIDGE")!=std::string::npos);CHECK(Serial.output.find("serial-proto=")!=std::string::npos);
 Serial.output.clear();Serial.feed("help\n");for(int i=0;i<100;i++){Serial.room=63;loop();mockNow++;}
 CHECK(Serial.output.find("NOT receiver/STB action\r\n")!=std::string::npos);
 Serial.output.clear();Serial.feed("qstat\n");for(int i=0;i<100;i++){Serial.room=63;loop();mockNow++;}
 size_t count=0,pos=0;while((pos=Serial.output.find("[Q]",pos))!=std::string::npos){count++;pos+=3;}CHECK(count==3);
 Serial.output.clear();Serial.feed("16 83 03 0\n");Serial.room=0;loop();CHECK(!mockPins[PIN_A3]);
 Wire.request();CHECK(Wire.output.size()==15);CHECK(Wire.output[3]==0x83);
 Wire.input.push_back(2);Wire.receive(1);
 mockNow++;Serial.room=0;loop();CHECK(!mockPins[PIN_A3]);
 mockNow++;Serial.room=0;loop();CHECK(mockPins[PIN_A3]);
 for(int i=0;i<6000;i++){Serial.room=63;loop();mockNow++;}
 Serial.feed("status\n");for(int i=0;i<100;i++){Serial.room=63;loop();mockNow++;}
 CHECK(Serial.output.find("[STATUS] stage=0")!=std::string::npos);CHECK(!Serial.blockedWrite);
 CHECK(SREG==128);
 printf("Actual sketch adapter: %d/%d checks passed\n",checks-fail,checks);return fail?1:0;
}
