#pragma once
#include "timer_capture.h"
#include <time.h>
#include <inttypes.h>
struct _gps_xref;
typedef struct _gps_xref gps_xref;
typedef struct _gps_xref
{
    uint64_t cap_value64;
    time_t epoch;
} gps_xref;

typedef struct _gps_pair
{
    gps_xref t0;
    gps_xref t1;
    long double ratio;
} gps_xlate_handle_t;

void log_gps_pps(esp_probe_recv_data_t *rd);
// int64_t xlateCap64(gps_xref *t0, gps_xref *t1, uint64_t *sample64);
int64_t xlateCap64(gps_xlate_handle_t *pair, uint64_t *sample64);
void getGpsHandle(gps_xlate_handle_t *gp);
