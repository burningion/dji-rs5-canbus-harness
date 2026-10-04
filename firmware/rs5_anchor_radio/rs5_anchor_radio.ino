// Camera-side Feather ESP32-S3. No CAN or motion commands; stock STM32 unchanged.
#include <Arduino.h>
#include <esp_random.h>
#include "../shared/EspNowLink.h"

#if !defined(CONFIG_IDF_TARGET_ESP32S3)
#error "Select Adafruit Feather ESP32-S3 No PSRAM."
#endif
static uwb::StreamParser parser;
static uwb::Measurement latest;
static volatile bool uartError=false;
static bool valid=false, haveMeasurement=false;
static uint32_t sampleMs=0, lastLoopMs=0, bootId=0, lastStatusMs=0;
static void invalidate() { valid=false; ++bootId; }

static void readAnchor(uint32_t now) {
  if (uartError || now-lastLoopMs>100 || Serial1.available()>512) {
    uartError=false; invalidate(); parser.reset();
    for (unsigned i=0;i<2048 && Serial1.available();++i) Serial1.read();
    return;
  }
  if (parser.expire(now)) invalidate();
  for (unsigned i=0;i<512 && Serial1.available();++i) {
    uwb::Measurement m;
    const auto result=parser.feed(Serial1.read(),now,m);
    if (result==uwb::ParseResult::Invalid) invalidate();
    if (result==uwb::ParseResult::Measurement) {
      // Repeated anchor reports never acquire a new source timestamp.
      if (m.tag!=latest.tag || m.sequence!=latest.sequence || !haveMeasurement) {
        latest=m; sampleMs=now; valid=true; haveMeasurement=true;
      }
    }
  }
}

void setup() {
  Serial.begin(115200);
#if ARDUINO_USB_CDC_ON_BOOT
  Serial.setTxTimeoutMs(0);
#endif
  Serial1.setRxBufferSize(1024);
  Serial1.begin(115200,SERIAL_8N1,38,39); // Only RX/GPIO38 and GND connect to STM32.
  Serial1.onReceiveError([](hardwareSerial_error_t e) { if (e!=UART_NO_ERROR) uartError=true; });
  bootId=esp_random();
  espnowLink::begin(true);
  lastLoopMs=millis();
}
void loop() {
  const uint32_t now=millis();
  readAnchor(now); lastLoopMs=now;
  espnowLink::Message request; bool lost=false;
  if (espnowLink::take(request,lost)) {
    uint8_t reply[wireless::ResponseSize];
    if (wireless::response(request.bytes,request.size,bootId,latest,now-sampleMs,
                           valid && !lost && now-sampleMs<=wireless::MaxAgeMs,reply))
      espnowLink::send(reply,sizeof(reply));
  }
  // No Serial connection or UI heartbeat is required on this battery-powered node.
  if (Serial && now-lastStatusMs>=1000) {
    lastStatusMs=now;
    Serial.printf("camera mac=%s radio_ready=%d sample_valid=%d age_ms=%lu\n",
        WiFi.macAddress().c_str(),espnowLink::ready,valid,
        static_cast<unsigned long>(now-sampleMs));
  }
  delay(1);
}
