#pragma once

#include "mqtt_client.h"

void mqttCliHandleData(esp_mqtt_event_handle_t event, const char *cli_topic);
