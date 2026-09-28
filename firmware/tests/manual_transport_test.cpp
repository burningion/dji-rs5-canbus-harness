// Exercise the actual sketch's packet sending against a deterministic TWAI fake.
#include <assert.h>
#include <stdio.h>
#include <Arduino.h>
#include <driver/twai.h>
uint32_t fakeNow=0, fakeAlerts=0;
unsigned fakePendingPolls=0, fakeStops=0, fakeUninstalls=0;
bool fakeStalled=false;
FakeSerial Serial;
twai_status_info_t fakeStatus;
std::vector<twai_message_t> fakeFrames;
#define CONFIG_IDF_TARGET_ESP32S3 1
#include "../rs5_manual_control/rs5_manual_control.ino"
static void resetFake() {
  fakeNow=0; fakeAlerts=0; fakePendingPolls=0; fakeStops=fakeUninstalls=0;
  fakeStalled=false; fakeStatus={}; fakeFrames.clear(); canReady=true;
  faultAlerts=0; faultReason="none"; lastCanStatus={}; motion.stop("idle");
}
int main() {
  resetFake();
  fakeAlerts=TWAI_ALERT_ARB_LOST; fakeStatus.arb_lost_count=3; fakePendingPolls=2;
  assert(sendPacket(dji::axisSpeed(1,600,-600)));
  assert(canReady && fakeStops==0 && fakeFrames.size()==4);
  for (const auto &frame:fakeFrames) assert(!frame.ss && frame.identifier==0x223);
  assert(fakeNow<20); // Normal arbitration can finish inside the deadline.

  resetFake(); fakeStalled=true;
  assert(!sendPacket(dji::axisSpeed(1,600,0)));
  assert(fakeNow==20 && !canReady && fakeStops==1 && fakeUninstalls==1);
  assert(fakeFrames.size()==4);
  assert(!sendPacket(dji::axisSpeed(2,600,0)) && fakeFrames.size()==4); // No replay.

  resetFake(); fakeAlerts=TWAI_ALERT_BUS_ERROR;
  fakeStatus.bus_error_count=7; fakeStatus.arb_lost_count=12; fakeStatus.tx_failed_count=2;
  assert(!sendPacket(dji::jointQuery(1)));
  assert(!canReady && faultAlerts==TWAI_ALERT_BUS_ERROR);
  assert(lastCanStatus.bus_error_count==7 && lastCanStatus.tx_failed_count==2);
  stopMotion("operator_stop"); // Operator commands must not erase the CAN fault.
  status(fakeNow);
  while(console.size()) drainConsole();
  assert(Serial.written.find("\"reason\":\"can_bus_error\"")!=std::string::npos);
  assert(Serial.written.find("\"arbitration_lost\":12")!=std::string::npos);
  puts("Actual sketch: arbitration retries, 20ms cutoff/TX clearing and preserved fault diagnostics passed.");
}
