#pragma once
#include <stdint.h>
// struct _qcontrol;

typedef struct _qcontrol qcontrol_handle;

void testem();
qcontrol_handle *init_qcontrol();
uint64_t qc_get64(qcontrol_handle *qh, uint32_t p32);