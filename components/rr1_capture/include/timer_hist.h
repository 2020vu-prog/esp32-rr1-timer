
#pragma once

#include "esp_log.h"
#include "gps_xlate.h"
#include "timer_capture.h"
#include "timer_hist_logic.h"
#include <inttypes.h>

void timer_hist_init();
void th_append(esp_probe_recv_data_t *recv_dataP);
extern lane_transition_t *hist;
extern int nextHist;
int dec_hist(int h);
int inc_hist(int h);

typedef struct {
  int birthIndex;
  uint64_t expiryTicks64;
  uint64_t birthTicks64;
  lane_transition_t priorState[2];
  gps_xlate_handle_t ghandle;
  lane_transition_t *lastAudit;
  bool auditPending;
} candidate_block_t;

typedef struct {
  int laneTransitionCount;
  uint64_t healthMarshalledUs;
} marshal_recap_t;

lane_state_enum getResultState(mcpwm_capture_edge_t cap_edge);

int mqPubDataList();

int aba_xmit_b64_json(uint8_t *buffer, size_t packed_size);
