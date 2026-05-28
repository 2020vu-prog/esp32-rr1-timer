#pragma once

#include <stdbool.h>
#include <stdint.h>

#define HIST_MAX 0x7fff
#define MEG (1000 * 1000)

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

typedef struct {
  uint32_t clearMs;
  uint32_t maxCarMs;
  uint32_t minCarMs;
  uint32_t maxPerfs;
} timer_config_t;

typedef struct {
  lane_transition_t *nose;
  lane_transition_t *tail;
  int transition_count;
  char errs[9];
} lane_finish_t;

#define FE_MISSING_NOSE 'a'
#define FE_MISSING_TAIL 'b'
#define FE_MAXCARLEN 'c'
#define FE_MINCARLEN 'd'
#define FE_PERFCOUNT 'e'
#define FE_NONE '0'

int timer_hist_dec(int h);
int timer_hist_inc(int h);
uint64_t timer_hist_msecs_to_ticks(uint64_t ms);
int timer_hist_backlog_count(int next_hist, int next_xmit_hist);
int timer_hist_capped_backlog_count(int backlog, int cap);
bool timer_hist_health_due(uint64_t now_us, uint64_t last_health_us,
                           int transition_count);
void timer_hist_process_lane_transition(lane_transition_t *transition,
                                        lane_finish_t *finish);
void timer_hist_audit_finish(lane_finish_t *finish,
                             const timer_config_t *config);
