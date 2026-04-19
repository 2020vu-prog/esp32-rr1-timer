#pragma once
#include "driver/mcpwm_cap.h"

#define GPS_PPS_GPIO 1 // D0
#define LANE1_GPIO 0   // D1
#define LANE2_GPIO 25  // D2

struct _pindef_t;

typedef struct {
  uint64_t cap_value64;
  mcpwm_capture_edge_t cap_edge;
  struct _pindef_t *pin_user_data;
} esp_probe_recv_data_t;

bool isLaneClear(mcpwm_capture_edge_t cap_edge);
void capture_main(void);
void capture_main_xtask(void *pvParameters);

typedef void (*PinHandlerFunc)(esp_probe_recv_data_t *);
typedef struct _pindef_t {
  mcpwm_cap_channel_handle_t channel_h;
  PinHandlerFunc pinHandlerFunc;
  uint8_t gpio;
  uint8_t lane_index; // 0->lane1, 1->lane2
  char *pname;
  bool pull_down;

  bool pull_up;
  bool pos_edge;
  bool neg_edge;

} pindef_t;

int getGpsInitialAcquisitionSecondsAfterBoot();
int getGpsUptimeTotalSeconds();
int getGpsFlutter();
bool isGpsEmittingPps();
int getgpsUptimeContiguousSeconds();
void scheduleMqPubDataList(int delayMs);
