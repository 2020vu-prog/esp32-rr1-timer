#include "timer_hist_logic.h"

int timer_hist_dec(int h) { return (h - 1) & HIST_MAX; }

int timer_hist_inc(int h) { return (h + 1) & HIST_MAX; }

uint64_t timer_hist_msecs_to_ticks(uint64_t ms) { return ms * 1000; }

int timer_hist_backlog_count(int next_hist, int next_xmit_hist) {
  int backlog = next_hist - next_xmit_hist;
  return backlog & HIST_MAX;
}

int timer_hist_capped_backlog_count(int backlog, int cap) {
  return backlog > cap ? cap : backlog;
}

bool timer_hist_health_due(uint64_t now_us, uint64_t last_health_us,
                           int transition_count) {
  int health_interval_ms = transition_count > 0 ? 30000 : 55000;
  return last_health_us == 0 ||
         now_us > last_health_us + (health_interval_ms * 1000);
}

void timer_hist_process_lane_transition(lane_transition_t *transition,
                                        lane_finish_t *finish) {
  if (!finish->nose || transition->cap_value64 < finish->nose->cap_value64) {
    finish->nose = transition;
  }
  if (!finish->tail || transition->cap_value64 > finish->tail->cap_value64) {
    finish->tail = transition;
  }
  finish->transition_count += 1;
}

static void lf_error(lane_finish_t *finish, char err_code) {
  finish->errs[0] = err_code;
}

void timer_hist_audit_finish(lane_finish_t *finish,
                             const timer_config_t *config) {
  if (!finish->nose) {
    lf_error(finish, FE_MISSING_NOSE);
    return;
  }
  if (!finish->tail) {
    lf_error(finish, FE_MISSING_TAIL);
    return;
  }
  uint64_t car_len_ticks =
      finish->tail->cap_value64 - finish->nose->cap_value64;
  if (car_len_ticks > timer_hist_msecs_to_ticks(config->maxCarMs)) {
    lf_error(finish, FE_MAXCARLEN);
    return;
  }
  if (car_len_ticks < timer_hist_msecs_to_ticks(config->minCarMs)) {
    lf_error(finish, FE_MINCARLEN);
    return;
  }
  // no perfs s/b 2 transitions (nose, tail)
  // one perfs s/b 4 transitions (nose, beginPerf,endPerf, tail)
  if ((uint32_t)finish->transition_count > (config->maxPerfs + 1) * 2) {
    lf_error(finish, FE_PERFCOUNT);
    return;
  }
  lf_error(finish, FE_NONE);
}
