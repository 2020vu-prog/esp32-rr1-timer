#pragma once
#include "timer_capture.h"
#include <time.h>
#include <inttypes.h>
struct _gps_xref;
typedef struct _gps_xref gps_xref;
typedef struct _gps_xref
{
	int64_t cap_value64;
	int64_t epoch;
} gps_xref;

typedef struct _gps_pair
{
	gps_xref t0;
	gps_xref t1;
	// float ratioF;
	// long double ratioLdSlow;
	int64_t igDelta;
	int64_t itDelta;
} gps_xlate_handle_t;

void log_gps_pps(esp_probe_recv_data_t *rd);
// int64_t xlateCap64(gps_xref *t0, gps_xref *t1, uint64_t *sample64);

int64_t xlateCap64(gps_xlate_handle_t *pair, uint64_t *sample64, struct timespec *resulTs);
void getGpsHandle(gps_xlate_handle_t *gp);

void test_ghandle(void);
