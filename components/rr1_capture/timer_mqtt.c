/* MQTT (over TCP) Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include "esp_event.h"
#include "esp_netif.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
// #include "protocol_examples_common.h"

#include "esp_log.h"
#include "esp_sntp.h"

#include "esp_system.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "get_mqtt_creds.h"
#include "mqtt_client.h"
#include "rr1_blink.h"
#include "rr1_wifi.h"
#include "time.h"
#include "timer_hist.h"
#include "timer_marshal.h"
#include "timer_mqtt.h"
static const char *TAG = "timer_mqtt";
static char mq_topic[30] = "";
static char mqtt_client_id[12] = "";
static esp_mqtt_client_handle_t mqttClient = NULL;
static TaskHandle_t mqttReconnectTaskHandle = NULL;
#define MQ_PUBLISH_CREDITS_MAX 100
#define MQTT_RECONNECT_BACKOFF_MS 5000
#ifndef MQTT_TEST_DROP_PUBACK_7
#define MQTT_TEST_DROP_PUBACK_7 0
#endif
#ifndef MQTT_TEST_DROP_FIRST_PUBACK
#define MQTT_TEST_DROP_FIRST_PUBACK 1
#endif
#define RECENT_WIFI_PS_MAX 9
#define WIFI_PS_INVALID -999
static int mq_publish_credits = MQ_PUBLISH_CREDITS_MAX;
static int recentWifiPsMinModemPercents[RECENT_WIFI_PS_MAX] = {
    WIFI_PS_INVALID, WIFI_PS_INVALID, WIFI_PS_INVALID,
    WIFI_PS_INVALID, WIFI_PS_INVALID, WIFI_PS_INVALID,
    WIFI_PS_INVALID, WIFI_PS_INVALID, WIFI_PS_INVALID};
static int recentWifiPsIndex = 0;
static wifi_ps_type_t currentWifiPsMode = WIFI_PS_NONE;
static int64_t wifiPsLastChangeUs = 0;
static int64_t wifiPsMinModemTotalUs = 0;
static int64_t wifiPsLastSampleUs = 0;
static int64_t wifiPsLastSampleMinModemUs = 0;
#if MQTT_TEST_DROP_FIRST_PUBACK
static bool mqttTestDroppedFirstPubAck = false;
#endif
const char *AWS_ROOT_CA_1 = "\
-----BEGIN CERTIFICATE-----\n\
MIIDQTCCAimgAwIBAgITBmyfz5m/jAo54vB4ikPmljZbyjANBgkqhkiG9w0BAQsF\
ADA5MQswCQYDVQQGEwJVUzEPMA0GA1UEChMGQW1hem9uMRkwFwYDVQQDExBBbWF6\
b24gUm9vdCBDQSAxMB4XDTE1MDUyNjAwMDAwMFoXDTM4MDExNzAwMDAwMFowOTEL\
MAkGA1UEBhMCVVMxDzANBgNVBAoTBkFtYXpvbjEZMBcGA1UEAxMQQW1hem9uIFJv\
b3QgQ0EgMTCCASIwDQYJKoZIhvcNAQEBBQADggEPADCCAQoCggEBALJ4gHHKeNXj\
ca9HgFB0fW7Y14h29Jlo91ghYPl0hAEvrAIthtOgQ3pOsqTQNroBvo3bSMgHFzZM\
9O6II8c+6zf1tRn4SWiw3te5djgdYZ6k/oI2peVKVuRF4fn9tBb6dNqcmzU5L/qw\
IFAGbHrQgLKm+a/sRxmPUDgH3KKHOVj4utWp+UhnMJbulHheb4mjUcAwhmahRWa6\
VOujw5H5SNz/0egwLX0tdHA114gk957EWW67c4cX8jJGKLhD+rcdqsq08p8kDi1L\
93FcXmn/6pUCyziKrlA4b9v7LWIbxcceVOF34GfID5yHI9Y/QCB/IIDEgEw+OyQm\
jgSubJrIqg0CAwEAAaNCMEAwDwYDVR0TAQH/BAUwAwEB/zAOBgNVHQ8BAf8EBAMC\
AYYwHQYDVR0OBBYEFIQYzIU07LwMlJQuCFmcx7IQTgoIMA0GCSqGSIb3DQEBCwUA\
A4IBAQCY8jdaQZChGsV2USggNiMOruYou6r4lK5IpDB/G/wkjUu0yKGX9rbxenDI\
U5PMCCjjmCXPI6T53iHTfIUJrU6adTrCC2qJeHZERxhlbI1Bjjt/msv0tadQ1wUs\
N+gDS63pYaACbvXy8MWy7Vu33PqUXHeeE6V/Uq2V8viTO96LXFvKWlJbYK8U90vv\
o/ufQJVtMVT8QtPHRh8jrdkPSHCa2XV4cdFyQzR1bldZwgJcJmApzyMZFo6IQ6XU\
5MsI+yMRQ+hDKXJioaldXgjUkK642M4UwtBV8ob2xJNDd2ZhwLnoQdeXeGADbkpy\
rqXRfboQnoZsG4q5WTP468SQvvG5\n\
-----END CERTIFICATE-----";

typedef struct _rr1MqHandle {
  esp_mqtt_client_handle_t p_client;
  int connCount;
  int disconnCount;
  /*
   * Per-backend in-flight state. Each MQTT backend handle tracks the publish
   * currently waiting for ESP-MQTT to report MQTT_EVENT_PUBLISHED.
   */
  int inFlightMsgId;
  int64_t inFlightXmitUs;
  struct {
    int laneTransitionCount;
    uint64_t healthMarshalledUs;
  } inFlightRecap;
  int32_t recent_msg_latency_ms;
  int32_t max_msg_latency_ms;
  char *tag;
} _rr1MqHandle;
static _rr1MqHandle _aws_mqttHandle = {.p_client = NULL,
                                       .connCount = 0,
                                       .disconnCount = 0,
                                       .inFlightMsgId = 0,
                                       .inFlightXmitUs = 0,
                                       .inFlightRecap = {},
                                       .tag = "rr1_aws"};
static rr1MqHandle aws_mqttHandle = &_aws_mqttHandle;
void get_device_hostname(char *host_name, size_t max) {
  uint8_t eth_mac[6];
  const char *host_prefix = "RR1-";
  esp_wifi_get_mac(WIFI_IF_STA, eth_mac);
  snprintf(host_name, max, "%s%02X%02X%02X", host_prefix, eth_mac[3],
           eth_mac[4], eth_mac[5]);
}
void init_mq_topic() {
  char host_name[12];
  get_device_hostname(host_name, sizeof(host_name));
  snprintf(mq_topic, sizeof(mq_topic), "rr2Timer/%s", host_name);
  snprintf(mqtt_client_id, sizeof(mqtt_client_id), "%s", host_name);
  ESP_LOGI(TAG, "MQTT topic set to: %s", mq_topic);
}
// esp_mqtt_client_handle_t p_client = NULL;
void time_sync_notification_cb(struct timeval *tv) {
  ESP_LOGI(TAG, "Notification of a time synchronization event");
}
static void initialize_sntp(void) {
  ESP_LOGI(TAG, "Initializing SNTP");
  esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
  esp_sntp_set_sync_mode(SNTP_SYNC_MODE_IMMED);
  esp_sntp_setservername(0, "1.us.pool.ntp.org");
  esp_sntp_set_time_sync_notification_cb(time_sync_notification_cb);
  esp_sntp_init();
  ESP_LOGI(TAG, "SNTP initialization done.");
}

static void log_error_if_nonzero(const char *message, int error_code) {
  if (error_code != 0) {
    ESP_LOGE(TAG, "Last error %s: 0x%x", message, error_code);
  }
}

/*
 * @brief Event handler registered to receive MQTT events
 *
 *  This function is called by the MQTT client event loop.
 *
 * @param handler_args user data registered to the event.
 * @param base Event base for the handler(always MQTT Base in this example).
 * @param event_id The id for the received event.
 * @param event_data The data for the event, esp_mqtt_event_handle_t.
 */

static int pubAckPending = 0;
#define MQ_PENDING_ACK_TIMEOUT_US (30 * 1000 * 1000)

static int64_t getWifiPsMinModemTotalUs(int64_t nowUs) {
  int64_t totalUs = wifiPsMinModemTotalUs;
  if (wifiPsLastChangeUs > 0 && currentWifiPsMode == WIFI_PS_MIN_MODEM) {
    totalUs += nowUs - wifiPsLastChangeUs;
  }
  return totalUs;
}

static void noteWifiPowerSaveMode(wifi_ps_type_t mode) {
  int64_t nowUs = esp_timer_get_time();
  if (wifiPsLastChangeUs == 0) {
    wifiPsLastChangeUs = nowUs;
    wifiPsLastSampleUs = nowUs;
    wifiPsLastSampleMinModemUs = wifiPsMinModemTotalUs;
  } else if (currentWifiPsMode == WIFI_PS_MIN_MODEM) {
    wifiPsMinModemTotalUs += nowUs - wifiPsLastChangeUs;
    wifiPsLastChangeUs = nowUs;
  } else {
    wifiPsLastChangeUs = nowUs;
  }
  currentWifiPsMode = mode;
}

static void setTrackedWifiPowerSaveMode(wifi_ps_type_t mode,
                                        const char *reason) {
  esp_err_t err = esp_wifi_set_ps(mode);
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "esp_wifi_set_ps(%d) failed for %s: %s", mode, reason,
             esp_err_to_name(err));
    return;
  }
  noteWifiPowerSaveMode(mode);
}

int getRecentWifiPsMinModemPercentAverage(void) {
  int64_t nowUs = esp_timer_get_time();
  if (wifiPsLastSampleUs == 0) {
    wifiPsLastSampleUs = nowUs;
    wifiPsLastSampleMinModemUs = getWifiPsMinModemTotalUs(nowUs);
    return 0;
  }

  int64_t totalMinModemUs = getWifiPsMinModemTotalUs(nowUs);
  int64_t elapsedUs = nowUs - wifiPsLastSampleUs;
  if (elapsedUs > 0) {
    int64_t minModemUs = totalMinModemUs - wifiPsLastSampleMinModemUs;
    int percent = (int)((minModemUs * 100) / elapsedUs);
    if (percent < 0) {
      percent = 0;
    } else if (percent > 100) {
      percent = 100;
    }
    recentWifiPsMinModemPercents[recentWifiPsIndex] = percent;
    recentWifiPsIndex = (recentWifiPsIndex + 1) % RECENT_WIFI_PS_MAX;
    wifiPsLastSampleUs = nowUs;
    wifiPsLastSampleMinModemUs = totalMinModemUs;
  }

  int sum = 0;
  int count = 0;
  for (int i = 0; i < RECENT_WIFI_PS_MAX; i++) {
    if (recentWifiPsMinModemPercents[i] != WIFI_PS_INVALID) {
      sum += recentWifiPsMinModemPercents[i];
      count++;
    }
  }
  return count > 0 ? sum / count : 0;
}

static void clear_in_flight_mq_msg(void) {
  aws_mqttHandle->inFlightMsgId = 0;
  aws_mqttHandle->inFlightXmitUs = 0;
  memset(&aws_mqttHandle->inFlightRecap, 0,
         sizeof(aws_mqttHandle->inFlightRecap));
}

static void clear_pending_mq_msg(const char *reason) {
  if (aws_mqttHandle->inFlightMsgId == 0) {
    return;
  }

  int msg_id = aws_mqttHandle->inFlightMsgId;
  ESP_LOGW(TAG, "Clearing pending MQTT msg id %d: %s", msg_id, reason);
  timerHistMqPubCleared(msg_id, reason);
  clear_in_flight_mq_msg();
}

bool isMqttPublishPending() { return aws_mqttHandle->inFlightMsgId != 0; }

int getMqttInFlightMsgId(void) { return aws_mqttHandle->inFlightMsgId; }

int64_t getMqttInFlightAgeMs(void) {
  if (aws_mqttHandle->inFlightMsgId == 0) {
    return 0;
  }

  return (esp_timer_get_time() - aws_mqttHandle->inFlightXmitUs) / 1000;
}

bool clearExpiredMqttPublish(void) {
  if (aws_mqttHandle->inFlightMsgId == 0) {
    return false;
  }

  int64_t pending_us = esp_timer_get_time() - aws_mqttHandle->inFlightXmitUs;
  if (pending_us <= MQ_PENDING_ACK_TIMEOUT_US) {
    return false;
  }

  clear_pending_mq_msg("publish ack timeout");
  return true;
}

bool getMqttInFlightRecap(int *laneTransitionCount,
                          uint64_t *healthMarshalledUs) {
  if (!laneTransitionCount || !healthMarshalledUs ||
      aws_mqttHandle->inFlightMsgId == 0) {
    return false;
  }

  *laneTransitionCount = aws_mqttHandle->inFlightRecap.laneTransitionCount;
  *healthMarshalledUs = aws_mqttHandle->inFlightRecap.healthMarshalledUs;
  return true;
}

static void scheduleMqttReconnect(void) {
  if (mqttReconnectTaskHandle) {
    xTaskNotifyGive(mqttReconnectTaskHandle);
  }
}

void timerMqttWifiDisconnected(const char *reason) {
  ESP_LOGW(TAG, "timerMqttWifiDisconnected: %s", reason);
  aws_mqttHandle->p_client = NULL;
  clear_pending_mq_msg(reason);
}

void timerMqttWifiIpReady(void) {
  if (!mqttClient) {
    ESP_LOGI(TAG, "timerMqttWifiIpReady: MQTT client not initialized yet");
    return;
  }
  if (aws_mqttHandle->p_client) {
    ESP_LOGI(TAG, "timerMqttWifiIpReady: MQTT already connected");
    return;
  }

  ESP_LOGI(TAG,
           "timerMqttWifiIpReady: scheduling MQTT reconnect after IP ready");
  scheduleMqttReconnect();
}

static void mqttReconnectTask(void *pvParameters) {
  (void)pvParameters;

  while (1) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

    while (mqttClient && !aws_mqttHandle->p_client && wifi_ip[0]) {
      /*
       * Auto reconnect is disabled so MQTT does not reconnect while Wi-Fi is
       * down. After ESP-MQTT reports DISCONNECTED it moves into
       * MQTT_STATE_WAIT_RECONNECT; esp_mqtt_client_reconnect() returning
       * ESP_OK only means the request was accepted. DNS/TLS/MQTT can still
       * fail asynchronously and emit another DISCONNECTED event, so every
       * request gets the same backoff to keep reconnect notifications from
       * spinning this task.
       */
      ESP_LOGI(TAG, "mqttReconnectTask: reconnecting MQTT");
      esp_err_t err = esp_mqtt_client_reconnect(mqttClient);
      if (err != ESP_OK) {
        ESP_LOGW(TAG, "mqttReconnectTask: reconnect failed err=0x%x", err);
      }
      vTaskDelay(pdMS_TO_TICKS(MQTT_RECONNECT_BACKOFF_MS));
    }
  }
}

static void mqtt_event_handler(void *handler_args, esp_event_base_t base,
                               int32_t event_id, void *event_data) {
  ESP_LOGD(TAG,
           "Event dispatched from event loop base=%s, event_id=%" PRIi32 "",
           base, event_id);
  esp_mqtt_event_handle_t event = event_data;
  esp_mqtt_client_handle_t client = event->client;
  int msg_id;
  switch ((esp_mqtt_event_id_t)event_id) {
  case MQTT_EVENT_CONNECTED:
    ESP_LOGI(TAG, "MQTT_EVENT_CONNECTED");

#ifdef DO__SUBSCRIBE
    msg_id = esp_mqtt_client_subscribe(client, "/topic/cqos0", 0);
    ESP_LOGI(TAG, "sent subscribe successful, msg_id=%d", msg_id);
#endif
    // msg_id = esp_mqtt_client_subscribe(client, mq_topic, 1);
    // ESP_LOGI(TAG, "sent subscribe successful, msg_id=%d", msg_id);

    //        msg_id = esp_mqtt_client_unsubscribe(client, mq_topic);
    //        ESP_LOGI(TAG, "sent unsubscribe successful, msg_id=%d", msg_id);
    // msg_id = esp_mqtt_client_publish(client, mq_topic, "data_3", 0, 1, 0);
    // ESP_LOGI(TAG, "sent publish successful, msg_id=%d", msg_id);
    aws_mqttHandle->p_client = client;

    set_error_priority(ERROR_PRI_MQTT, false);
    aws_mqttHandle->connCount++;
    mq_pub("MQTT_EVENT_CONNECTED");
    scheduleMqPubDataList(1000);

    break;
  case MQTT_EVENT_DISCONNECTED:
    ESP_LOGI(TAG, "MQTT_EVENT_DISCONNECTED");
    set_error_priority(ERROR_PRI_MQTT, true);
    // ignore spurious disconnects that can happen when esp_mqtt_client_stop is
    // called while a connection is still being established?
    if (aws_mqttHandle->connCount > aws_mqttHandle->disconnCount) {
      aws_mqttHandle->disconnCount++;
    }

    aws_mqttHandle->p_client = NULL;
    clear_pending_mq_msg("disconnected");
    if (wifi_ip[0]) {
      scheduleMqttReconnect();
    }
    break;

  case MQTT_EVENT_SUBSCRIBED:
    ESP_LOGI(TAG, "MQTT_EVENT_SUBSCRIBED, msg_id=%d", event->msg_id);
    msg_id =
        esp_mqtt_client_enqueue(client, "/topic/qos0", "data", 0, 0, 0, true);
    ESP_LOGI(TAG, "sent publish successful, msg_id=%d", msg_id);
    break;
  case MQTT_EVENT_UNSUBSCRIBED:
    ESP_LOGI(TAG, "MQTT_EVENT_UNSUBSCRIBED, msg_id=%d", event->msg_id);
    break;
  case MQTT_EVENT_PUBLISHED:
    ESP_LOGI(TAG, "MQTT_EVENT_PUBLISHED, msg_id=%d", event->msg_id);
#if MQTT_TEST_DROP_FIRST_PUBACK
    if (!mqttTestDroppedFirstPubAck) {
      mqttTestDroppedFirstPubAck = true;
      ESP_LOGW(TAG, "TEST: dropping first MQTT_EVENT_PUBLISHED msg_id=%d",
               event->msg_id);
      break;
    }
#endif
#if MQTT_TEST_DROP_PUBACK_7
    if ((event->msg_id % 10) == 7) {
      ESP_LOGW(TAG, "TEST: dropping MQTT_EVENT_PUBLISHED msg_id=%d",
               event->msg_id);
      break;
    }
#endif
    pubAckPending--;
    if (aws_mqttHandle->inFlightMsgId == event->msg_id) {
      int64_t latency_ms =
          (esp_timer_get_time() - aws_mqttHandle->inFlightXmitUs) / 1000;
      aws_mqttHandle->recent_msg_latency_ms = latency_ms;
      ESP_LOGI(TAG,
               "Publish ack received for pending msg id %d latency %" PRIi64
               " ms",
               event->msg_id, latency_ms);
      if (latency_ms > aws_mqttHandle->max_msg_latency_ms) {
        aws_mqttHandle->max_msg_latency_ms = latency_ms;
      }
      timerHistMqPubAcked(event->msg_id);
      setTrackedWifiPowerSaveMode(WIFI_PS_MIN_MODEM, "publish ack");
      clear_in_flight_mq_msg();
      scheduleMqPubDataList(100); // make sure backlog is caught up
    }
    break;
  case MQTT_EVENT_DATA:
    ESP_LOGI(TAG, "MQTT_EVENT_DATA");
    printf("MQTT_EVENT_DATA TOPIC=%.*s\r\n", event->topic_len, event->topic);
    printf("MQTT_EVENT_DATA DATA=%.*s\r\n", event->data_len, event->data);
    break;
  case MQTT_EVENT_ERROR:
    ESP_LOGI(TAG, "MQTT_EVENT_ERROR");
    clear_pending_mq_msg("mqtt error");
    if (wifi_ip[0]) {
      scheduleMqttReconnect();
    }
    if (event->error_handle->error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT) {
      log_error_if_nonzero("reported from esp-tls",
                           event->error_handle->esp_tls_last_esp_err);
      log_error_if_nonzero("reported from tls stack",
                           event->error_handle->esp_tls_stack_err);
      log_error_if_nonzero("captured as transport's socket errno",
                           event->error_handle->esp_transport_sock_errno);
      ESP_LOGI(TAG, "Last errno string (%s)",
               strerror(event->error_handle->esp_transport_sock_errno));
    }
    break;
  default:
    ESP_LOGI(TAG, "Other event id:%d", event->event_id);
    break;
  }
}

void mqtt_app_start(void) {
  static bool init = false;
  if (init) {
    ESP_LOGI(TAG, "[APP]  skipping duplicate init..");
    return;
  }
  init = true;
  init_mq_topic();
  initialize_sntp();
#ifdef HIVEMQTT
  esp_mqtt_client_config_t mqtt_cfg = {
      //.broker.address.uri = "mqtt://test.mosquitto.org",
      .session.disable_clean_session = true,
      .broker.address.uri = "mqtt://broker.hivemq.com",

  };
#endif /* HIVEMQTT */

  ESP_LOGI(TAG, "MQTT client config begin");
  if (get_mqtt_host() && get_mqtt_cert() && get_mqtt_key()) {
    ESP_LOGI(TAG, "MQTT client config with credentials");
  } else {
    ESP_LOGW(TAG, "MQTT client config missing credentials");
    return;
  }
  const esp_mqtt_client_config_t mqtt_cfg = {
      .session =
          {
              .disable_clean_session = false,
              .last_will =
                  {
                      .topic = mq_topic,
                      .msg = "offline",
                      .qos = 1,
                      .retain = false,
                  },
          },
      .broker =
          {
              .address =
                  {
                      .hostname = get_mqtt_host(), // AWS IoT Endpoint
                      .transport = MQTT_TRANSPORT_OVER_SSL,
                      .port = 8883,
                  },
              .verification =
                  {
                      .certificate = AWS_ROOT_CA_1, // Amazon Root CA 1
                  },
          },
      .credentials =
          {
              .authentication =
                  {
                      .certificate = get_mqtt_cert(), // Device Certificate
                      .key = get_mqtt_key(),          // Device Private Key
                  },
              .client_id = mqtt_client_id, // Must match AWS IoT Thing Name
          },
      .network =
          {
              .timeout_ms = 20000,
              .disable_auto_reconnect = true,
          },
      .task =
          {
              .stack_size = 10240,
              .priority = 5,
          },
  };
  ESP_LOGI(TAG, "MQTT client configured with host %s, client_id %s",
           mqtt_cfg.broker.address.hostname, mqtt_cfg.credentials.client_id);
  ESP_LOGI(TAG, "MQTT client configured with cert %s, key %s",
           mqtt_cfg.credentials.authentication.certificate,
           mqtt_cfg.credentials.authentication.key);
  ESP_LOGI(TAG, "MQTT client configured with root CA cert %s",
           mqtt_cfg.broker.verification.certificate);
#if CONFIG_BROKER_URL_FROM_STDIN
  char line[128];

  if (strcmp(mqtt_cfg.broker.address.uri, "FROM_STDIN") == 0) {
    int count = 0;
    printf("Please enter url of mqtt broker\n");
    while (count < 128) {
      int c = fgetc(stdin);
      if (c == '\n') {
        line[count] = '\0';
        break;
      } else if (c > 0 && c < 127) {
        line[count] = c;
        ++count;
      }
      vTaskDelay(10 / portTICK_PERIOD_MS);
    }
    mqtt_cfg.broker.address.uri = line;
    printf("Broker url: %s\n", line);
  } else {
    ESP_LOGE(TAG, "Configuration mismatch: wrong broker url");
    abort();
  }
#endif /* CONFIG_BROKER_URL_FROM_STDIN */

  esp_mqtt_client_handle_t client = esp_mqtt_client_init(&mqtt_cfg);
  mqttClient = client;
  BaseType_t reconnectTaskCreated =
      xTaskCreate(&mqttReconnectTask, "mqtt_reconnect", 4096, NULL, 4,
                  &mqttReconnectTaskHandle);
  if (reconnectTaskCreated != pdPASS) {
    ESP_LOGE(TAG, "failed to create mqtt reconnect task");
    mqttReconnectTaskHandle = NULL;
  }
  /* The last argument may be used to pass data to the event handler, in this
   * example mqtt_event_handler */
  esp_mqtt_client_register_event(client, ESP_EVENT_ANY_ID, mqtt_event_handler,
                                 NULL);

  vTaskDelay(1 / portTICK_PERIOD_MS);
  ESP_LOGI(TAG, "0412Starting MQTT client");
  esp_mqtt_client_start(client);
  ESP_LOGI(TAG, "0412MQTT client started");
}

void NOTapp_main(void) {
  ESP_LOGI(TAG, "[APP] Startup..");
  ESP_LOGI(TAG, "[APP] Free memory: %" PRIu32 " bytes",
           esp_get_free_heap_size());
  ESP_LOGI(TAG, "[APP] IDF version: %s", esp_get_idf_version());

  esp_log_level_set("*", ESP_LOG_INFO);
  esp_log_level_set("mqtt_client", ESP_LOG_VERBOSE);
  esp_log_level_set("mqtt_example", ESP_LOG_VERBOSE);
  esp_log_level_set("transport_base", ESP_LOG_VERBOSE);
  esp_log_level_set("esp-tls", ESP_LOG_VERBOSE);
  esp_log_level_set("transport", ESP_LOG_VERBOSE);
  esp_log_level_set("outbox", ESP_LOG_VERBOSE);

  ESP_ERROR_CHECK(nvs_flash_init());
  ESP_ERROR_CHECK(esp_netif_init());
  ESP_ERROR_CHECK(esp_event_loop_create_default());

  /* This helper function configures Wi-Fi or Ethernet, as selected in
   * menuconfig. Read "Establishing Wi-Fi or Ethernet Connection" section in
   * examples/protocols/README.md for more information about this function.
   */
  // ESP_ERROR_CHECK(example_connect());

  mqtt_app_start();
}
double epoch_double() {
  struct timespec tv;
  if (clock_gettime(CLOCK_REALTIME, &tv)) {
    perror("error clock_gettime\n");
    return 1;
  }

  char time_str[32];
  sprintf(time_str, "%lld.%.9ld", tv.tv_sec,
          tv.tv_nsec);   // Combine seconds and nanoseconds
  return atof(time_str); // Convert to a double
}

unsigned long getTime() {
  time_t now;
  /*
  struct tm timeinfo;

  if (!getLocalTime(&timeinfo))
  {
      // Serial.println("Failed to obtain time");
      return (0);
  }
  */
  time(&now);
  return now;
}
const int bufs = 128;
void mq_pub_tags(jsonTagP tagsHead) {
  char buf[bufs] = {};
  fmtJson(buf, bufs, tagsHead);
}
int mq_pub64(char *msg, int laneTransitionCount, uint64_t healthMarshalledUs) {
  // return -8;
  int msg_id = -9;
  clearExpiredMqttPublish();

  if (aws_mqttHandle->p_client && aws_mqttHandle->inFlightMsgId == 0) {
    ESP_LOGI(TAG, "mq_pub64 sending publish pending msg  %s ", msg);
    setTrackedWifiPowerSaveMode(WIFI_PS_NONE, "publish");
    /*
     * Keep QoS 1 for broker PUBACKs, but do not store messages in ESP-MQTT's
     * outbox across reconnects. timer_hist owns resend by holding nextXmitHist
     * until timerHistMqPubAcked() observes the PUBACK.
     */
    msg_id = esp_mqtt_client_enqueue(aws_mqttHandle->p_client, mq_topic, msg, 0,
                                     1, 0, false);
  } else {
    ESP_LOGI(TAG, "mq_pub64 NOT sent publish pending msg id %d ",
             aws_mqttHandle->inFlightMsgId);
  }

  if (msg_id >= 0) {
    ESP_LOGI(TAG, "mq_pub64 sent publish successful, topic [%s] msg_id=%d",
             mq_topic, msg_id);
    aws_mqttHandle->inFlightMsgId = msg_id;
    aws_mqttHandle->inFlightXmitUs = esp_timer_get_time();
    aws_mqttHandle->inFlightRecap.laneTransitionCount = laneTransitionCount;
    aws_mqttHandle->inFlightRecap.healthMarshalledUs = healthMarshalledUs;
    pubAckPending++;
  } else {
    ESP_LOGI(TAG, "mq_pub64 NOT sent publish  ");
  }
  return msg_id;
}
void mq_pub(char *msg) {
  return;
  static int seq;
  seq++;
  uint64_t upUs = esp_timer_get_time();
  char buf[bufs] = {};
  int msg_id;
  jsonTag jfirst = {
    tag : "seq",
    val64 : seq,
  };

  fmtJson(buf, bufs, &jfirst);

  if (aws_mqttHandle->p_client) {
    double nowD = epoch_double();

    int fheap = esp_get_minimum_free_heap_size();
    snprintf(buf, bufs,
             "{\"seq\":%04d, \"msg\":\"%s\", \"t\":\"%.4f\", \"f\":\"%d\", "
             "\"up\":%" PRIu64
             ", \"conn\":\"%d:%d\",\"minutes\":%d, \"blog\":%d  }",
             seq, msg, nowD, fheap, upUs, aws_mqttHandle->connCount,
             aws_mqttHandle->disconnCount, (int)(upUs / 1000000) / 60,
             pubAckPending);

    msg_id = esp_mqtt_client_enqueue(aws_mqttHandle->p_client, mq_topic, buf, 0,
                                     2, 0, true);
    ESP_LOGI(TAG, "mq_pub sent publish successful, msg_id=%d", msg_id);
    pubAckPending++;

    // ESP_LOGI(TAG, "sent , now=%ld", getTime());
  } else {
    ESP_LOGI(TAG, "mq_pub NOT sent publish  ");
  }
}
void addTag(jsonTagP tagsHead, jsonTagP nt) {
  while (tagsHead) {
    if (!tagsHead->next) {
      tagsHead->next = nt;
      return;
    }
    tagsHead = tagsHead->next;
  }
}
void fmtJson(char *buf, size_t bufs, jsonTagP tagsHead) {
  while (tagsHead) {
    tagsHead = tagsHead->next;
  }
}
int getMqttConnectionCount() { return aws_mqttHandle->connCount; }
int getMqttRecentLatencyMs() { return aws_mqttHandle->recent_msg_latency_ms; }
int getMqttMaxLatencyMs() { return aws_mqttHandle->max_msg_latency_ms; }
int getMqttPublishCredits() { return mq_publish_credits; }
void incMqttPublishCredits() {
  if (mq_publish_credits < MQ_PUBLISH_CREDITS_MAX) {
    mq_publish_credits++;
  }
}
void decrementMqttPublishCredits() {
  if (mq_publish_credits > 0) {
    mq_publish_credits--;
  }
}
