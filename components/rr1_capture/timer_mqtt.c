/* MQTT (over TCP) Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <inttypes.h>
#include "esp_system.h"
#include "nvs_flash.h"
#include "esp_event.h"
#include "esp_netif.h"
// #include "protocol_examples_common.h"

#include "esp_log.h"
#include "esp_sntp.h"

#include "mqtt_client.h"
#include "time.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "timer_mqtt.h"

static const char *TAG = "mqtt_example";
esp_mqtt_client_handle_t p_client = NULL;
void time_sync_notification_cb(struct timeval *tv)
{
    ESP_LOGI(TAG, "Notification of a time synchronization event");
}
static void initialize_sntp(void)
{
    ESP_LOGI(TAG, "Initializing SNTP");
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "pool.ntp.org");
    esp_sntp_set_time_sync_notification_cb(time_sync_notification_cb);
    esp_sntp_init();
}

static void log_error_if_nonzero(const char *message, int error_code)
{
    if (error_code != 0)
    {
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
static int connCount = 0;
static int disconnCount = 0;
static int pubAckPending=0;
static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    ESP_LOGD(TAG, "Event dispatched from event loop base=%s, event_id=%" PRIi32 "", base, event_id);
    esp_mqtt_event_handle_t event = event_data;
    esp_mqtt_client_handle_t client = event->client;
    int msg_id;
    switch ((esp_mqtt_event_id_t)event_id)
    {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "MQTT_EVENT_CONNECTED");

        msg_id = esp_mqtt_client_subscribe(client, "/topic/cqos0", 0);
        ESP_LOGI(TAG, "sent subscribe successful, msg_id=%d", msg_id);

        msg_id = esp_mqtt_client_subscribe(client, "/topic/cqos1", 1);
        ESP_LOGI(TAG, "sent subscribe successful, msg_id=%d", msg_id);

        //        msg_id = esp_mqtt_client_unsubscribe(client, "/topic/cqos1");
        //        ESP_LOGI(TAG, "sent unsubscribe successful, msg_id=%d", msg_id);
        // msg_id = esp_mqtt_client_publish(client, "/topic/cqos1", "data_3", 0, 1, 0);
        // ESP_LOGI(TAG, "sent publish successful, msg_id=%d", msg_id);
        p_client = client;

        connCount++;
        mq_pub("MQTT_EVENT_CONNECTED");
        break;
    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGI(TAG, "MQTT_EVENT_DISCONNECTED");
        disconnCount++;

        p_client = NULL;
        break;

    case MQTT_EVENT_SUBSCRIBED:
        ESP_LOGI(TAG, "MQTT_EVENT_SUBSCRIBED, msg_id=%d", event->msg_id);
        msg_id = esp_mqtt_client_enqueue(client, "/topic/qos0", "data", 0, 0, 0, true);
        ESP_LOGI(TAG, "sent publish successful, msg_id=%d", msg_id);
        break;
    case MQTT_EVENT_UNSUBSCRIBED:
        ESP_LOGI(TAG, "MQTT_EVENT_UNSUBSCRIBED, msg_id=%d", event->msg_id);
        break;
    case MQTT_EVENT_PUBLISHED:
        ESP_LOGI(TAG, "MQTT_EVENT_PUBLISHED, msg_id=%d", event->msg_id);
	pubAckPending--;
        break;
    case MQTT_EVENT_DATA:
        ESP_LOGI(TAG, "MQTT_EVENT_DATA");
        printf("MQTT_EVENT_DATA TOPIC=%.*s\r\n", event->topic_len, event->topic);
        printf("MQTT_EVENT_DATA DATA=%.*s\r\n", event->data_len, event->data);
        break;
    case MQTT_EVENT_ERROR:
        ESP_LOGI(TAG, "MQTT_EVENT_ERROR");
        if (event->error_handle->error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT)
        {
            log_error_if_nonzero("reported from esp-tls", event->error_handle->esp_tls_last_esp_err);
            log_error_if_nonzero("reported from tls stack", event->error_handle->esp_tls_stack_err);
            log_error_if_nonzero("captured as transport's socket errno", event->error_handle->esp_transport_sock_errno);
            ESP_LOGI(TAG, "Last errno string (%s)", strerror(event->error_handle->esp_transport_sock_errno));
        }
        break;
    default:
        ESP_LOGI(TAG, "Other event id:%d", event->event_id);
        break;
    }
}

void mqtt_app_start(void)
{
    static bool init = false;
    if (init)
    {
        ESP_LOGI(TAG, "[APP]  skipping duplicate init..");
        return;
    }
    init = true;
    initialize_sntp();
    esp_mqtt_client_config_t mqtt_cfg = {
        //.broker.address.uri = "mqtt://test.mosquitto.org",
        .broker.address.uri = "mqtt://broker.hivemq.com",

    };
#if CONFIG_BROKER_URL_FROM_STDIN
    char line[128];

    if (strcmp(mqtt_cfg.broker.address.uri, "FROM_STDIN") == 0)
    {
        int count = 0;
        printf("Please enter url of mqtt broker\n");
        while (count < 128)
        {
            int c = fgetc(stdin);
            if (c == '\n')
            {
                line[count] = '\0';
                break;
            }
            else if (c > 0 && c < 127)
            {
                line[count] = c;
                ++count;
            }
            vTaskDelay(10 / portTICK_PERIOD_MS);
        }
        mqtt_cfg.broker.address.uri = line;
        printf("Broker url: %s\n", line);
    }
    else
    {
        ESP_LOGE(TAG, "Configuration mismatch: wrong broker url");
        abort();
    }
#endif /* CONFIG_BROKER_URL_FROM_STDIN */

    esp_mqtt_client_handle_t client = esp_mqtt_client_init(&mqtt_cfg);
    /* The last argument may be used to pass data to the event handler, in this example mqtt_event_handler */
    esp_mqtt_client_register_event(client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    esp_mqtt_client_start(client);
}

void NOTapp_main(void)
{
    ESP_LOGI(TAG, "[APP] Startup..");
    ESP_LOGI(TAG, "[APP] Free memory: %" PRIu32 " bytes", esp_get_free_heap_size());
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

    /* This helper function configures Wi-Fi or Ethernet, as selected in menuconfig.
     * Read "Establishing Wi-Fi or Ethernet Connection" section in
     * examples/protocols/README.md for more information about this function.
     */
    // ESP_ERROR_CHECK(example_connect());

    mqtt_app_start();
}
double epoch_double()
{
    struct timespec tv;
    if (clock_gettime(CLOCK_REALTIME, &tv))
    {
        perror("error clock_gettime\n");
        return 1;
    }

    char time_str[32];
    sprintf(time_str, "%lld.%.9ld", tv.tv_sec, tv.tv_nsec); // Combine seconds and nanoseconds
    return atof(time_str);                                  // Convert to a double
}

unsigned long getTime()
{
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
void mq_pub_tags(jsonTagP tagsHead)
{
    char buf[bufs] = {};
    fmtJson(buf, bufs, tagsHead);
}
int mq_pub64(char *msg){
    int msg_id=-9;
    if (p_client)
    {
        msg_id = esp_mqtt_client_enqueue(p_client, "/topic/cqos1", msg, 0, 2, 0, true);
        ESP_LOGI(TAG, "mq_pub64 sent publish successful, msg_id=%d", msg_id);
	pubAckPending++;
    }
    else
    {
        ESP_LOGI(TAG, "mq_pub64 NOT sent publish  ");
    }
    return	 msg_id;
}
void mq_pub(char *msg)
{
    static int seq;
    seq++;
    uint64_t upUs = esp_timer_get_time();
    char buf[bufs] = {};
    int msg_id;
    jsonTag jfirst =
        {
            tag : "seq",
            val64 : seq,
        };

    fmtJson(buf, bufs, &jfirst);

    if (p_client)
    {
        double nowD = epoch_double();

        int fheap = esp_get_minimum_free_heap_size();
        snprintf(buf, bufs, "{\"seq\":%04d, \"msg\":\"%s\", \"t\":\"%.4f\", \"f\":\"%d\", \"up\":%" PRIu64 ", \"conn\":\"%d:%d\",\"minutes\":%d, \"blog\":%d  }",
                 seq, msg, nowD, fheap, upUs, connCount, disconnCount,(int)(upUs/1000000)/60, pubAckPending);

        msg_id = esp_mqtt_client_enqueue(p_client, "/topic/cqos1", buf, 0, 2, 0, true);
        ESP_LOGI(TAG, "mq_pub sent publish successful, msg_id=%d", msg_id);
	pubAckPending++;

        // ESP_LOGI(TAG, "sent , now=%ld", getTime());
    }
    else
    {
        ESP_LOGI(TAG, "mq_pub NOT sent publish  ");
    }
}
void addTag(jsonTagP tagsHead, jsonTagP nt)
{
    while (tagsHead)
    {
        if (!tagsHead->next)
        {
            tagsHead->next = nt;
            return;
        }
        tagsHead = tagsHead->next;
    }
}
void fmtJson(char *buf, size_t bufs, jsonTagP tagsHead)
{
    while (tagsHead)
    {
        tagsHead = tagsHead->next;
    }
}