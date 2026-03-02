
#include "gps_xlate.h"
#include "timer_mqtt.h"
#include <time.h>
#include <math.h>
#include <string.h>

#define gps_list_max 8
gps_xref gps_list[gps_list_max];
static int recent_pps = 0;
void log_gps_pps(esp_probe_recv_data_t *rd)
{
    recent_pps++;

    if (recent_pps >= gps_list_max)
    {
        recent_pps = 0;
    }
    double now = epoch_double();
    gps_list[recent_pps].epoch = round(now);
    gps_list[recent_pps].cap_value64 = rd->cap_value64;
};
const int YEAR5 = 3600 * 24 * 360 * 5;
bool isValid(gps_xref *t)
{
    return t->epoch > YEAR5;
}
int64_t xlateCap64(gps_xlate_handle_t *ghandle, uint64_t *sample64)
{
    if (ghandle && isValid(&ghandle->t0) && isValid(&ghandle->t1))
    {
    }
    else
    {
        return 0;
    }

    int64_t sampleOffset = *sample64 - ghandle->t0.cap_value64;
    return (sampleOffset * ghandle->ratio) + ghandle->t0.epoch;
}
// poopulate caller's struct!
void getGpsHandle(gps_xlate_handle_t *ghandle)
{
    int oldPps = recent_pps + 1;
    if (oldPps >= gps_list_max)
    {
        oldPps = 0;
    }
    memset(ghandle, 0, sizeof(*ghandle));
    ghandle->t0 = gps_list[recent_pps];
    ghandle->t1 = gps_list[oldPps];

    int delta = ghandle->t0.epoch - ghandle->t1.epoch;

    if (delta != (gps_list_max - 1))
    {
        memset(ghandle, 0, sizeof(*ghandle));
        return;
    }
    long double gDelta = ghandle->t0.cap_value64 - ghandle->t1.cap_value64;
    long double tDelta = ghandle->t0.epoch - ghandle->t1.epoch;
    ghandle->ratio = tDelta / gDelta;
    return;
}
