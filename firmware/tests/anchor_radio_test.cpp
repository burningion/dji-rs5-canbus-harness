#include <assert.h>
#include <Arduino.h>
uint32_t fakeNow=1000;
FakeSerial Serial, Serial1;
#define CONFIG_IDF_TARGET_ESP32S3 1
#include "../rs5_anchor_radio/rs5_anchor_radio.ino"
static void report(unsigned seq) {
  char json[160], wire[180];
  snprintf(json,sizeof(json),"{\"TWR\":{\"a16\":\"1234\",\"R\":%u,\"D\":450,\"Xcm\":200,\"Ycm\":400}}",seq);
  snprintf(wire,sizeof(wire),"JS%04X%s",static_cast<unsigned>(strlen(json)),json);
  Serial1.incoming+=wire;
}
int main() {
  Serial.connected=false; lastLoopMs=fakeNow; bootId=77; espnowLink::ready=true;
  report(1); loop(); assert(valid && sampleMs==1000); // Runs with no USB host.
  report(1); fakeNow+=50; loop(); assert(sampleMs==1000); // Duplicate can't refresh age.
  report(2); fakeNow+=50; loop(); assert(valid && latest.sequence==2);
  Serial1.incoming="JS0002xx"; loop(); assert(!valid && bootId==78);
  report(3); loop(); assert(valid && bootId==78); // Error epoch survives later good reports.
  report(4); fakeNow+=101; loop(); assert(!valid && Serial1.incoming.empty() && bootId==79);
  report(5); loop(); uartError=true; loop(); assert(!valid && bootId==80);
  report(6); loop();
  wireless::Receiver receiver; receiver.begin(1);
  uint8_t request[wireless::RequestSize]; receiver.request(fakeNow,request);
  uint8_t mac[6]={}; esp_now_recv_info_t info={mac};
  espnowLink::receive(&info,request,sizeof(request)); loop();
  assert(radioSent.size()==wireless::ResponseSize && radioSent[23]==1);
  for (int i=0;i<4;++i) { fakeNow+=50; loop(); }
  receiver.request(fakeNow,request); espnowLink::receive(&info,request,sizeof(request)); loop();
  assert(radioSent[23]==0); // Running radio doesn't make a stale anchor live.
  puts("Camera sketch: battery-only operation, duplicate age, UART faults and stale reports passed.");
}
