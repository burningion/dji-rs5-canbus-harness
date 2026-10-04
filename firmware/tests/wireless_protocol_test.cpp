#include <assert.h>
#include <stdio.h>
#include "../shared/WirelessProtocol.h"
using wireless::Result;
int main() {
  wireless::Receiver r; r.begin(123);
  uint8_t q[wireless::RequestSize], p[wireless::ResponseSize];
  uwb::Measurement in, out; in.tag=0x1234; in.sequence=19;
  in.xCm=-200; in.yCm=400; in.distanceCm=450;
  uint32_t stamp=0;
  auto reply=[&](uint32_t now,uint32_t age=10,uint32_t boot=77,bool valid=true) {
    r.request(now,q); assert(wireless::response(q,sizeof(q),boot,in,age,valid,p));
  };
  reply(1000); assert(r.accept(p,sizeof(p),1010,out,stamp)==Result::Restart);
  reply(1050); assert(r.accept(p,sizeof(p),1060,out,stamp)==Result::Measurement);
  assert(stamp==1040 && out.tag==in.tag && out.xCm==-200 && out.sequence==19);
  assert(r.accept(p,sizeof(p),1061,out,stamp)==Result::Ignore); // Duplicate response.
  reply(1100); r.request(1150,q);
  assert(r.accept(p,sizeof(p),1160,out,stamp)==Result::Ignore); // Old challenge.
  reply(1200); assert(r.accept(p,sizeof(p),1281,out,stamp)==Result::Invalid);
  reply(1300,145); assert(r.accept(p,sizeof(p),1310,out,stamp)==Result::Invalid);
  reply(1400,0,77,false); assert(r.accept(p,sizeof(p),1410,out,stamp)==Result::Invalid);
  reply(1500,0,78); assert(r.accept(p,sizeof(p),1510,out,stamp)==Result::Restart);
  reply(1550); p[4]^=1; assert(r.accept(p,sizeof(p),1560,out,stamp)==Result::Ignore);
  reply(1600); assert(r.accept(p,sizeof(p)-1,1610,out,stamp)==Result::Ignore);
  p[2]=2; assert(r.accept(p,sizeof(p),1610,out,stamp)==Result::Ignore);
  r.begin(999); reply(UINT32_MAX-20,0);
  assert(r.accept(p,sizeof(p),5,out,stamp)==Result::Restart);
  reply(40,0); assert(r.accept(p,sizeof(p),50,out,stamp)==Result::Measurement && stamp==40);
  reply(60,30); assert(r.accept(p,sizeof(p),70,out,stamp)==Result::Ignore); // Age regresses.
  puts("Wireless framing, challenges, age bounds, restarts, replay and clock wrap passed.");
}
