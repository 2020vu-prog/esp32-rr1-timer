#include "mbedtls/base64.h"
#include "esp_heap_caps.h"

#include <string.h>

#define ABA_RAW_SIZE 4096
#define ABA_RAW_SIZE64 ((ABA_RAW_SIZE + (ABA_RAW_SIZE / 2)))
struct AbaHandle
{
	char raw[ABA_RAW_SIZE];
	char b64[ABA_RAW_SIZE64];
	char json[ABA_RAW_SIZE64];
};

typedef struct AbaHandle AbaHandle;
AbaHandle *initAbaHandle(void)
{
	AbaHandle *h = heap_caps_malloc(sizeof(AbaHandle), MALLOC_CAP_SPIRAM);

	memset(h, 0, sizeof(AbaHandle));
	return h;
}