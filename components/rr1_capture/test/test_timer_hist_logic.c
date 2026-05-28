#include "timer_hist_logic.h"
#include "unity.h"

TEST_CASE("timer history indexes wrap around the ring", "[timer_hist]") {
  TEST_ASSERT_EQUAL_INT(1, timer_hist_inc(0));
  TEST_ASSERT_EQUAL_INT(0, timer_hist_inc(HIST_MAX));
  TEST_ASSERT_EQUAL_INT(HIST_MAX, timer_hist_dec(0));
  TEST_ASSERT_EQUAL_INT(HIST_MAX - 1, timer_hist_dec(HIST_MAX));
}

TEST_CASE("timer history backlog handles wraparound", "[timer_hist]") {
  TEST_ASSERT_EQUAL_INT(0, timer_hist_backlog_count(3, 3));
  TEST_ASSERT_EQUAL_INT(2, timer_hist_backlog_count(5, 3));
  TEST_ASSERT_EQUAL_INT(2, timer_hist_backlog_count(1, HIST_MAX));
  TEST_ASSERT_EQUAL_INT(20, timer_hist_capped_backlog_count(25, 20));
  TEST_ASSERT_EQUAL_INT(5, timer_hist_capped_backlog_count(5, 20));
}

TEST_CASE("timer health is due on first publish or after interval",
          "[timer_hist]") {
  TEST_ASSERT_TRUE(timer_hist_health_due(1000, 0, 0));
  TEST_ASSERT_FALSE(timer_hist_health_due(54000000, 1000, 0));
  TEST_ASSERT_TRUE(timer_hist_health_due(55002000, 1000, 0));
  TEST_ASSERT_FALSE(timer_hist_health_due(30000000, 1000, 1));
  TEST_ASSERT_TRUE(timer_hist_health_due(30002000, 1000, 1));
}

TEST_CASE("lane transition audit accepts valid finish", "[timer_hist]") {
  timer_config_t config = {
      .clearMs = 10000,
      .maxCarMs = 500,
      .minCarMs = 50,
      .maxPerfs = 2,
  };
  lane_transition_t transitions[] = {
      {.cap_value64 = 100000},
      {.cap_value64 = 180000},
  };
  lane_finish_t finish = {};

  timer_hist_process_lane_transition(&transitions[1], &finish);
  timer_hist_process_lane_transition(&transitions[0], &finish);
  timer_hist_audit_finish(&finish, &config);

  TEST_ASSERT_EQUAL_PTR(&transitions[0], finish.nose);
  TEST_ASSERT_EQUAL_PTR(&transitions[1], finish.tail);
  TEST_ASSERT_EQUAL_INT(2, finish.transition_count);
  TEST_ASSERT_EQUAL_CHAR(FE_NONE, finish.errs[0]);
}

TEST_CASE("lane transition audit reports finish rule failures",
          "[timer_hist]") {
  timer_config_t config = {
      .clearMs = 10000,
      .maxCarMs = 500,
      .minCarMs = 50,
      .maxPerfs = 0,
  };
  lane_transition_t short_finish[] = {
      {.cap_value64 = 100000},
      {.cap_value64 = 120000},
  };
  lane_transition_t long_finish[] = {
      {.cap_value64 = 100000},
      {.cap_value64 = 700000},
  };
  lane_transition_t valid_length_finish[] = {
      {.cap_value64 = 100000},
      {.cap_value64 = 180000},
  };
  lane_finish_t finish = {};

  timer_hist_audit_finish(&finish, &config);
  TEST_ASSERT_EQUAL_CHAR(FE_MISSING_NOSE, finish.errs[0]);

  finish = (lane_finish_t){.nose = &short_finish[0],
                           .tail = &short_finish[1],
                           .transition_count = 2};
  timer_hist_audit_finish(&finish, &config);
  TEST_ASSERT_EQUAL_CHAR(FE_MINCARLEN, finish.errs[0]);

  finish = (lane_finish_t){
      .nose = &long_finish[0], .tail = &long_finish[1], .transition_count = 2};
  timer_hist_audit_finish(&finish, &config);
  TEST_ASSERT_EQUAL_CHAR(FE_MAXCARLEN, finish.errs[0]);

  finish = (lane_finish_t){.nose = &valid_length_finish[0],
                           .tail = &valid_length_finish[1],
                           .transition_count = 3};
  timer_hist_audit_finish(&finish, &config);
  TEST_ASSERT_EQUAL_CHAR(FE_PERFCOUNT, finish.errs[0]);
}
