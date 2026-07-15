#include "esp_heap_caps.h"
#include "mbedtls/base64.h"
// #include <cjson.h>
#include <cJSON.h>

#include <string.h>

static int seq = 0;
char *aba_b64_json(char *buffer) {
  char *string = NULL;

  cJSON *monitor = cJSON_CreateObject();
  if (monitor == NULL) {
    goto end;
  }

  if (cJSON_AddNumberToObject(monitor, "seq", seq) == NULL) {
    goto end;
  }
  if (cJSON_AddStringToObject(monitor, "b64", buffer) == NULL) {
    goto end;
  }
  string = cJSON_PrintUnformatted(monitor);
  if (string == NULL) {
    fprintf(stderr, "Failed to print monitor.\n");
  }

end:
  cJSON_Delete(monitor);
  return string;
}

void aba_b64_json_mark_sent(void) { seq++; }
