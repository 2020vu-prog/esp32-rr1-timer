#include "quad_uint32.h"
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "esp_log.h"

const static char *TAG = "rr1_quad";
typedef struct
{
	/* data */
	uint32_t orBits;
	bool armed;
} qcItem;

struct _qcontrol
{
	qcItem qcItems[4];
};
qcontrol_handle gqh;
qcontrol_handle *init_qcontrol()
{

	memset(&gqh, 0, sizeof(qcontrol_handle));
	gqh.qcItems[2].armed = true;
	gqh.qcItems[3].armed = true;
	return &gqh;
}
uint64_t qc_get64(qcontrol_handle *qh, uint32_t p32)
{
	int idx = (p32 >> 30); // hi 2 bits (int 0-3)

	if (qh->qcItems[idx].armed)
	{
		int polarIdx = (idx + 2) & 0x03;
		qh->qcItems[idx].armed = false;
		qh->qcItems[polarIdx].armed = true;
		qh->qcItems[polarIdx].orBits++;
	}
	uint64_t rc = qh->qcItems[idx].orBits;
	rc = rc << 32;
	return rc | p32;
}

void testem()
{
	int pass = 0;
	int fail = 0;
	uint64_t prev = 0;
	qcontrol_handle *h = init_qcontrol();
	for (uint64_t x = 0; x < 10000; x++)
	{
		uint64_t cut = x * 1000 * 1000;
		qc_get64(h, cut) == cut ? pass++ : fail++;
		qc_get64(h, prev) == prev ? pass++ : fail++;
		prev = cut;
	}
	ESP_LOGI(TAG, "testem p:%d f:%d", pass, fail);
}