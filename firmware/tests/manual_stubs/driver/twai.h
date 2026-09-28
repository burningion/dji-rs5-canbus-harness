#pragma once
#include <stdint.h>
#include <vector>
#include <esp_err.h>
constexpr uint32_t TWAI_ALERT_BUS_ERROR=1, TWAI_ALERT_BUS_OFF=2, TWAI_ALERT_RX_QUEUE_FULL=4,
  TWAI_ALERT_RX_FIFO_OVERRUN=8, TWAI_ALERT_TX_FAILED=16, TWAI_ALERT_ARB_LOST=32;
constexpr int TWAI_STATE_RUNNING=1, TWAI_MODE_NORMAL=1;
struct twai_message_t { uint32_t identifier=0; unsigned ss=0,extd=0,rtr=0; uint8_t data_length_code=0,data[8]={}; };
struct twai_status_info_t {
  int state=TWAI_STATE_RUNNING;
  uint32_t msgs_to_tx=0,bus_error_count=0,rx_missed_count=0,rx_overrun_count=0,
    rx_error_counter=0,tx_error_counter=0,tx_failed_count=0,arb_lost_count=0;
};
struct twai_general_config_t { unsigned tx_queue_len=0,rx_queue_len=0; uint32_t alerts_enabled=0; };
struct twai_timing_config_t {};
struct twai_filter_config_t {};
inline twai_general_config_t TWAI_GENERAL_CONFIG_DEFAULT(int,int,int) { return {}; }
inline twai_timing_config_t TWAI_TIMING_CONFIG_1MBITS() { return {}; }
inline twai_filter_config_t TWAI_FILTER_CONFIG_ACCEPT_ALL() { return {}; }
extern twai_status_info_t fakeStatus;
extern uint32_t fakeAlerts;
extern unsigned fakePendingPolls,fakeStops,fakeUninstalls;
extern bool fakeStalled;
extern std::vector<twai_message_t> fakeFrames;
inline int twai_get_status_info(twai_status_info_t *out) {
  *out=fakeStatus;
  if (fakeStalled) out->msgs_to_tx=4;
  else if (fakePendingPolls) { --fakePendingPolls; out->msgs_to_tx=4; }
  return ESP_OK;
}
inline int twai_stop() { ++fakeStops; fakeStatus.msgs_to_tx=0; return ESP_OK; }
inline int twai_driver_uninstall() { ++fakeUninstalls; return ESP_OK; }
inline int twai_driver_install(const twai_general_config_t*,const twai_timing_config_t*,const twai_filter_config_t*) { return ESP_OK; }
inline int twai_start() { return ESP_OK; }
inline int twai_read_alerts(uint32_t *out,int) { *out=fakeAlerts; fakeAlerts=0; return ESP_OK; }
inline int twai_transmit(const twai_message_t *frame,int) { fakeFrames.push_back(*frame); return ESP_OK; }
inline int twai_receive(twai_message_t*,int) { return ESP_ERR_TIMEOUT; }
