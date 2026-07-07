
#pragma once

#include "esp_log.h"
#include "gps_xlate.h"
#include "timer_capture.h"
#include <inttypes.h>
typedef enum _lane_state_enum {
  LANE_BLOCKED = 1,
  LANE_CLEAR = 2,
} lane_state_enum;
typedef struct {
  uint64_t cap_value64;
  uint64_t gps_micros;
  uint8_t mqtt_pending;
  uint8_t lane_result_state; // result of transition
  uint8_t lane_gpio;
  uint8_t lane_index; // 0->lane1, 1->lane2

} lane_transition_t;

void timer_hist_init();
void th_append(esp_probe_recv_data_t *recv_dataP);
extern lane_transition_t *hist;
extern int nextHist;
#define HIST_MAX 0x7fff
// #define HIST_MAX 0x000f
#define MEG (1000 * 1000)
int dec_hist(int h);
int inc_hist(int h);

typedef struct {
  uint32_t clearMs;
  uint32_t maxCarMs;
  uint32_t minCarMs;
  uint32_t maxPerfs;
} timer_config_t;

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
  lane_transition_t *nose;
  lane_transition_t *tail;
  int transition_count;
  char errs[9];
} lane_finish_t;

typedef struct {
  int laneTransitionCount;
  uint64_t healthMarshalledUs;
} marshal_recap_t;

lane_state_enum getResultState(mcpwm_capture_edge_t cap_edge);

#define FE_MISSING_NOSE 'a'
#define FE_MISSING_TAIL 'b'
#define FE_MAXCARLEN 'c'
#define FE_MINCARLEN 'd'
#define FE_PERFCOUNT 'e'
#define FE_NONE '0'

int mqPubDataList();
int getXmitHistBacklog();
void timerHistMqPubAcked(int msg_id);
void timerHistMqPubCleared(int msg_id, const char *reason);
void scheduleMqPubDataList(int delayMs);
void timerMarshalInit(void);
int timerHistGetHealthIntervalMs(int tlCount);
uint64_t timerHistNextHealthDueMs(int tlCount, uint64_t nowMs);
bool isHealthDue(int tlCount);

int aba_xmit_b64_json(uint8_t *buffer, size_t packed_size);
