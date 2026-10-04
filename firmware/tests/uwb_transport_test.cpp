// Actual combined firmware: framed UART input, mode control, leases and CAN output.
#include <assert.h>
#include <stdio.h>
#include <Arduino.h>
#include <driver/twai.h>
uint32_t fakeNow=0, fakeAlerts=0;
unsigned fakePendingPolls=0, fakeStops=0, fakeUninstalls=0;
bool fakeStalled=false;
FakeSerial Serial, Serial1;
twai_status_info_t fakeStatus;
std::vector<twai_message_t> fakeFrames;
#define CONFIG_IDF_TARGET_ESP32S3 1
#include "../rs5_manual_control/rs5_manual_control.ino"

static void cmd(const std::string &body) {
  char line[128];
  snprintf(line, sizeof(line), "%s", body.c_str());
  handleCommand(line, fakeNow);
}
static std::string session(const char *name) {
  return std::string(name)+" "+std::to_string(sessionToken)+" "+std::to_string(fakeNow);
}
static void report(int seq, int tag=0x1234, int x=200, int y=400) {
  char json[180], wire[200];
  snprintf(json,sizeof(json),"{\"TWR\":{\"a16\":\"%04X\",\"R\":%d,\"D\":450,\"Xcm\":%d,\"Ycm\":%d}}",tag,seq,x,y);
  snprintf(wire,sizeof(wire),"JS%04X%s\r\n",static_cast<unsigned>(strlen(json)),json);
  Serial1.incoming += wire;
}
static void tick(unsigned ms=50) {
  fakeNow += ms; jointMs=fakeNow;
  loop();
}
static void reset() {
  fakeNow=1000; fakeAlerts=0; fakePendingPolls=0; fakeStalled=false; fakeStatus={}; fakeFrames.clear();
  Serial=FakeSerial(); Serial1=FakeSerial(); console=ConsoleQueue<4096>();
  canReady=probing=haveJoint=true; queryPending=false; consecutiveReplies=3; joint={};
  sessionToken=42; motion=ManualControl(); following=false; uwbConfigured=false;
  outputPan=outputTilt=0; tracker=tracking::Tracker(); uwbParser.reset(); uartError=false;
  lastLoopMs=jointMs=fakeNow; lastControlMs=0; commandLength=0; badCommand=false;
  cmd(session("uwb")+" 4660 1 0 50");
  assert(!motion.active() && fakeFrames.empty());
}
static void acquire(int x=200) {
  for(int i=1;i<=3;++i) { report(i,0x1234,x); tick(); }
  assert(tracker.fresh(fakeNow));
}
static void start() {
  acquire(); cmd(session("track")); assert(motion.active() && following);
  tick(); assert(outputPan>0 && outputPan<=50 && outputTilt==0);
}
int main() {
  reset(); cmd(session("track")); assert(!motion.active()); // No tag yet.
  report(1,0x9999); tick(); assert(!tracker.fresh(fakeNow));
  start();
  for(int i=4;i<15;++i) {
    report(i); cmd(session("follow")+" "+std::to_string(i)); tick();
    assert(motion.active() && outputPan<=50 && outputTilt==0);
  }
  cmd("stop"); assert(!following && !motion.active() && outputPan==0);

  reset(); start();
  // Expired lease is checked before a delayed heartbeat can revive it.
  fakeNow+=301; cmd(session("follow")+" 1"); assert(!motion.active() && !following);

  reset(); start();
  cmd(session("follow")+" 1"); cmd(session("follow")+" 1");
  assert(!motion.active() && !following); // Replay.

  reset(); start();
  // Tag timeout with fresh host/joints, and no auto-resume on a new report.
  for(int i=1;i<=7;++i) { cmd(session("follow")+" "+std::to_string(i)); tick(); }
  assert(!motion.active() && outputPan==0);
  for(int i=4;i<=6;++i) { report(i); tick(); }
  assert(tracker.fresh(fakeNow) && !motion.active());

  reset(); start();
  Serial1.incoming="JS0002{}JS0002xx"; tick();
  assert(!motion.active() && !following && outputPan==0);

  reset(); start(); uartError=true; report(4); tick();
  assert(!motion.active() && Serial1.incoming.empty());

  reset(); start(); report(4); tick(101);
  assert(!motion.active() && Serial1.incoming.empty()); // No stale UART backlog.

  reset(); start(); Serial.connected=false; tick(); assert(!motion.active());

  reset(); start(); haveJoint=false; tick(); assert(!motion.active());

  reset(); start(); cmd(session("drive")+" 1 600 600");
  assert(!motion.active()); // No mixing follow and manual speed packets.

  reset(); cmd(session("arm")); cmd(session("drive")+" 1 600 -600");
  Serial1.incoming="JS0002xx"; tick();
  assert(motion.active() && outputPan==600 && outputTilt==-600); // Absent/bad UWB doesn't break manual.

  for (const int sign : {-1,1}) {
    reset(); cmd(session("uwb")+" 4660 "+std::to_string(sign)+" 0 300");
    assert(uwbMaxSpeed==300);
    acquire(400); cmd(session("track"));
    for(int i=4;i<24;++i) {
      const int previous=outputPan;
      report(i,0x1234,400); cmd(session("follow")+" "+std::to_string(i)); tick();
      assert(motion.active() && outputPan*sign>0 && outputPan*sign<=300 && outputTilt==0);
      assert(abs(outputPan-previous)<=23); // Existing 45deg/s^2 ramp, in tenths.
    }
    assert(outputPan==sign*300); // The configurable tracker really reaches 30deg/s.
    cmd(session("uwb")+" 4660 1 0 301");
    assert(!motion.active() && !following && outputPan==0 && uwbMaxSpeed==300);
    assert(!strcmp(motion.reason(),"invalid_command")); // 30.1deg/s is rejected.
  }
  status(fakeNow); while(console.size()) drainConsole();
  assert(Serial.written.find("\"uwb\":{")!=std::string::npos);
  puts("Combined firmware UART, 30deg/s follow in both directions, limits, leases and manual fallback passed.");
}
