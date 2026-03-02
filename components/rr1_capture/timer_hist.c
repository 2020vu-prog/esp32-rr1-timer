#include "timer_hist.h"
#include "stddef.h"
#include "gps_xlate.h"

#include "esp_heap_caps.h"

const static char *TAG = "rr1_capture";
timer_config_t timerConfig = {
    clearMs : 10 * 1000,
    maxCarMs : 500,
    minCarMs : 50,
    maxPerfs : 2,

};
candidate_block_t candidateBlock = {
    expiryTicks64 : 0,
    birthTicks64 : 0,
    priorState : {},
};
lane_transition_t *hist;
lane_transition_t recentState[2] = {};
int recentHist = 0;

inline int dec_hist(int h)
{
    return (h - 1) & HIST_MAX;
}
inline int inc_hist(int h)
{

    return (h + 1) & HIST_MAX;
}

void timer_hist_init()
{
    ESP_LOGI(TAG, "timer_capture_init: BEGIN");

    heap_caps_print_heap_info(MALLOC_CAP_SPIRAM);
    size_t size = sizeof(lane_transition_t) * (HIST_MAX + 1);
    hist = heap_caps_malloc(size, MALLOC_CAP_SPIRAM);
    ESP_LOGI(TAG, "timer_capture_init: %d :: %p ", size, hist);
    memset(hist, 0, size);
}
uint64_t msecsToTicks(uint64_t ms)
{
    return ms * 80000;
}
void potentialRollCandidate(lane_transition_t *hp)
{
    // lane_transition_t *hp = &hist[recentHist];
    if (candidateBlock.expiryTicks64 < hp->cap_value64)
    {
        memset(&candidateBlock, 0, sizeof(candidateBlock));

        candidateBlock.birthIndex = recentHist;
        candidateBlock.birthTicks64 = hp->cap_value64;
        candidateBlock.expiryTicks64 = candidateBlock.birthTicks64 + msecsToTicks(timerConfig.clearMs);
        candidateBlock.priorState[0] = recentState[0];
        candidateBlock.priorState[1] = recentState[1];
        getGpsHandle(&candidateBlock.ghandle);
        candidateBlock.lastAudit = NULL;
        candidateBlock.auditPending = true;
    }
    hp->gps_micros = xlateCap64(&candidateBlock.ghandle, &hp->cap_value64);
    recentState[hp->lane_index] = *hp;
}
void process_lane_transition(lane_transition_t *hp, lane_finish_t *lf)
{
    if (!lf->nose || hp->cap_value64 < lf->nose->cap_value64)
    {
        lf->nose = hp;
    }
    if (!lf->tail || hp->cap_value64 > lf->tail->cap_value64)
    {
        lf->tail = hp;
    }
    lf->transition_count += 1;
}

void lfError(lane_finish_t *lf, char errCode)
{
    lf->errs[0] = errCode;
}
void auditFinish(lane_finish_t *lf)
{
    if (!lf->nose)
    {
        lfError(lf, FE_MISSING_NOSE);
        return;
    }
    if (!lf->tail)
    {
        lfError(lf, FE_MISSING_TAIL);
        return;
    }
    int carLenTicks = lf->tail->cap_value64 - lf->nose->cap_value64;
    if (carLenTicks > msecsToTicks(timerConfig.maxCarMs))
    {
        lfError(lf, FE_MAXCARLEN);
        return;
    }
    if (carLenTicks < msecsToTicks(timerConfig.minCarMs))
    {
        lfError(lf, FE_MINCARLEN);
        return;
    }
    // no perfs s/b 2 transitions (nose, tail)
    // one perfs s/b 4 transitions (nose, beginPerf,endPerf, tail)
    if (lf->transition_count > (timerConfig.maxPerfs + 1) * 2)
    {
        lfError(lf, FE_PERFCOUNT);
        return;
    }
    lfError(lf, FE_NONE);
    return;
}
void auditCandidateHist()
{
    lane_finish_t l12[2] = {};
    //    lane_finish_t l2 = {};

    for (int x = recentHist; hist[x].cap_value64 >= candidateBlock.birthTicks64; x = dec_hist(x))
    {
        ESP_LOGI(TAG, "auditCandidateHist %d %d", x, HIST_MAX);
        lane_transition_t *hp = &hist[x];
        process_lane_transition(hp, &l12[hp->lane_index]);
    }
    for (int j = 0; j < 2; j++)
    {
        auditFinish(&l12[j]);
    }
    // TODO:  do something with finish
    candidateBlock.auditPending = false;
}
void th_append(esp_probe_recv_data_t *recv_dataP)
{
    if (!hist)
    {
        ESP_LOGI(TAG, "th_append: SKIPPED no mem");
        return;
    }
    int _nextHist = inc_hist(recentHist);
    lane_transition_t *hpNext = &hist[_nextHist];

    hpNext->gps_micros = 0; //  defer until candidate block is assigned
    hpNext->lane_result_state = getResultState(recv_dataP->cap_edge);
    hpNext->cap_value64 = recv_dataP->cap_value64;
    hpNext->lane_gpio = recv_dataP->pin_user_data->gpio;
    hpNext->lane_index = recv_dataP->pin_user_data->lane_index;

    recentHist = _nextHist;
    potentialRollCandidate(hpNext);
    auditCandidateHist();
    candidateBlock.auditPending = true;
}
lane_state_enum getResultState(mcpwm_capture_edge_t cap_edge)
{
    if (isLaneClear(cap_edge))
    {
        return LANE_CLEAR;
    }
    else
    {
        return LANE_BLOCKED;
    }
}
bool isLaneClear(mcpwm_capture_edge_t cap_edge)
{
    return cap_edge == MCPWM_CAP_EDGE_POS;
}
