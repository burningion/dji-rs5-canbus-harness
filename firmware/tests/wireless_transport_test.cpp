// Actual body sketch with simulated radio and TWAI, including motion lifecycle.
#include <assert.h>
#include <Arduino.h>
#include <driver/twai.h>
uint32_t fakeNow=0, fakeAlerts=0;
unsigned fakePendingPolls=0, fakeStops=0, fakeUninstalls=0;
bool fakeStalled=false;
FakeSerial Serial, Serial1;
twai_status_info_t fakeStatus;
std::vector<twai_message_t> fakeFrames;
#define CONFIG_IDF_TARGET_ESP32S3 1
#include "../rs5_wireless_control/rs5_wireless_control.ino"
static unsigned heartbeats=0;
static uint8_t rangeSequence=0;
static void cmd(const std::string &body) {
  char line[128]; snprintf(line,sizeof(line),"%s",body.c_str()); handleCommand(line,fakeNow);
}
static std::string session(const char *name) {
  return std::string(name)+" "+std::to_string(sessionToken)+" "+std::to_string(fakeNow);
}
static void tick(unsigned ms=50) {
  fakeNow+=ms; jointMs=fakeNow;
  if (following) cmd(session("follow")+" "+std::to_string(++heartbeats));
  loop();
}
static void receive(uint32_t boot=77,uint32_t age=0,bool valid=true,bool wrongPeer=false) {
  uwb::Measurement m; m.tag=0x1234; m.sequence=++rangeSequence;
  m.xCm=200; m.yCm=400; m.distanceCm=450;
  uint8_t p[wireless::ResponseSize];
  assert(wireless::response(radioSent.data(),radioSent.size(),boot,m,age,valid,p));
  uint8_t mac[6]={}; if (wrongPeer) mac[0]=1;
  esp_now_recv_info_t info={mac}; espnowLink::receive(&info,p,sizeof(p));
}
static void reset() {
  fakeNow=1000; fakeAlerts=0; fakePendingPolls=0; fakeStalled=false; fakeStatus={}; fakeFrames.clear();
  Serial=FakeSerial(); Serial1=FakeSerial(); console=ConsoleQueue<4096>();
  canReady=probing=haveJoint=true; queryPending=false; consecutiveReplies=3; joint={};
  sessionToken=42; motion=ManualControl(); following=false; uwbConfigured=false;
  outputPan=outputTilt=0; tracker=tracking::Tracker(); radioReceiver.begin(123);
  haveObservedTag=false; observedMs=uwbReports=uwbBad=0;
  lastLoopMs=jointMs=fakeNow; lastPollMs=lastControlMs=0; commandLength=0; badCommand=false;
  espnowLink::waiting=espnowLink::overflow=false; espnowLink::ready=true; radioSendResult=ESP_OK;
  heartbeats=rangeSequence=0; cmd(session("uwb")+" 4660 1 0 50");
}
static void acquire() {
  for (int i=0;i<4;++i) { tick(); receive(); tick(5); }
  assert(tracker.fresh(fakeNow));
}
static void start() {
  reset(); acquire(); cmd(session("track")); tick();
  assert(following && motion.active() && outputPan>0);
}
int main() {
  // The body is usable on its own, including before radio pairing/setup.
  for (const bool radioInitialized : {false,true}) {
    reset(); espnowLink::ready=radioInitialized;
    cmd(session("arm"));
    for (unsigned seq=1;seq<=10;++seq) {
      cmd(session("drive")+" "+std::to_string(seq)+" 100 -50"); tick();
      assert(motion.active() && !following && outputPan==100 && outputTilt==-50);
      assert(!haveObservedTag && !tracker.fresh(fakeNow));
    }
    if (!radioInitialized) continue;
    // A paired camera appearing mid-gesture adds a fix, never control ownership.
    acquire();
    assert(haveObservedTag && motion.active() && !following && outputPan==100);
    for (unsigned seq=11;seq<=20;++seq) {
      cmd(session("drive")+" "+std::to_string(seq)+" 100 -50"); tick();
      assert(motion.active() && !following && outputPan==100);
    }
    assert(!tracker.fresh(fakeNow)); // Camera disappears; manual control continues.
    cmd("stop"); acquire();
    assert(!motion.active() && !following); // Detection alone never starts follow.
    consecutiveReplies=3; queryPending=false; // Simulate fresh CAN replies before re-arming.
    cmd(session("track")); tick();
    assert(following && motion.active() && outputPan>0);
  }
  reset(); cmd(session("track")); assert(!motion.active());
  start(); for (int i=0;i<7;++i) tick();
  assert(!following && !motion.active() && outputPan==0); // Radio loss.
  acquire(); assert(!motion.active()); // Never auto-resume.
  start(); receive(78); tick(5); assert(!motion.active()); // Camera reboot/fault epoch.
  start(); receive(77,151); tick(5); assert(!motion.active()); // Stale source.
  start(); receive(77,0,false); tick(5); assert(!motion.active()); // Invalid source.
  start(); receive(); receive(); tick(5); assert(!motion.active()); // Mailbox overflow.
  start(); receive(); tick(101); assert(!motion.active()); // Main loop stall.
  start(); receive(77,0,true,true); tick(5); assert(motion.active()); // Wrong peer ignored.
  for (int i=0;i<7;++i) tick(); assert(!motion.active());
  start(); radioSendResult=ESP_ERR_TIMEOUT; tick(); assert(!motion.active());
  start(); Serial.connected=false; tick(); assert(!motion.active());
  reset(); cmd(session("arm")); cmd(session("drive")+" 1 600 -600");
  radioSendResult=ESP_ERR_TIMEOUT; tick();
  assert(motion.active() && outputPan==600 && outputTilt==-600); // Manual fallback.
  status(fakeNow); while(console.size()) drainConsole();
  assert(Serial.written.find("\"transport\":\"esp-now\"")!=std::string::npos);
  puts("Wireless body sketch: master-only manual control, optional camera arrival/loss, explicit follow, faults and stops passed.");
}
