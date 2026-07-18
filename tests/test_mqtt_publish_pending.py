import re
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def read_source(relative_path):
    return (ROOT / relative_path).read_text()


def c_function_body(source, function_name):
    match = re.search(rf"\b{re.escape(function_name)}\s*\([^)]*\)\s*\{{", source)
    if not match:
        raise AssertionError(f"Could not find function {function_name}")

    start = match.end()
    depth = 1
    pos = start
    while pos < len(source) and depth:
        if source[pos] == "{":
            depth += 1
        elif source[pos] == "}":
            depth -= 1
        pos += 1

    if depth:
        raise AssertionError(f"Could not parse function {function_name}")
    return source[start : pos - 1]


class MqttPublishPendingTests(unittest.TestCase):
    def test_mq_pub_data_list_clears_expired_pending_before_early_return(self):
        source = read_source("components/rr1_capture/timer_hist.c")
        body = c_function_body(source, "mqPubDataList")

        clear_pos = body.find("clearExpiredMqttPublish();")
        pending_pos = body.find("isMqttPublishPending()")

        self.assertNotEqual(clear_pos, -1)
        self.assertNotEqual(pending_pos, -1)
        self.assertLess(clear_pos, pending_pos)

    def test_mqtt_timeout_clear_is_available_from_publish_path(self):
        source = read_source("components/rr1_capture/timer_mqtt.c")
        body = c_function_body(source, "mq_pub64")

        self.assertIn("clearExpiredMqttPublish();", body)

    def test_timeout_clear_preserves_history_for_resend(self):
        source = read_source("components/rr1_capture/timer_mqtt.c")
        body = c_function_body(source, "clearExpiredMqttPublish")

        self.assertIn("MQ_PENDING_ACK_TIMEOUT_US", body)
        self.assertIn('clear_pending_mq_msg("publish ack timeout")', body)

    def test_pending_log_includes_msg_id_and_age(self):
        source = read_source("components/rr1_capture/timer_hist.c")
        body = c_function_body(source, "mqPubDataList")

        self.assertIn("getMqttInFlightMsgId()", body)
        self.assertIn("getMqttInFlightAgeMs()", body)

    def test_first_puback_drop_runtime_probe_is_enabled(self):
        source = read_source("components/rr1_capture/timer_mqtt.c")

        self.assertIn("#define MQTT_TEST_DROP_FIRST_PUBACK 1", source)
        self.assertIn("mqttTestDroppedFirstPubAck", source)
        self.assertIn("TEST: dropping first MQTT_EVENT_PUBLISHED", source)

    def test_connect_subscribes_to_cli_topic(self):
        source = read_source("components/rr1_capture/timer_mqtt.c")

        self.assertIn('snprintf(mq_cli_topic, sizeof(mq_cli_topic), "%s/cli"', source)
        self.assertIn("esp_mqtt_client_subscribe(client, mq_cli_topic, 1)", source)
        self.assertNotIn('"/topic/qos0"', source)

    def test_last_will_message_is_json_object(self):
        source = read_source("components/rr1_capture/timer_mqtt.c")

        self.assertIn('.msg = "{\\"status\\":\\"offline\\"}"', source)
        self.assertNotIn('.msg = "offline"', source)

    def test_cli_topic_payload_accepts_json_object_or_array_of_objects(self):
        mqtt_source = read_source("components/rr1_capture/timer_mqtt.c")
        cli_source = read_source("components/rr1_capture/mqtt_cli.c")

        self.assertIn("mqttCliHandleData(event, mq_cli_topic)", mqtt_source)
        self.assertIn("eventTopicEquals(event, cli_topic)", cli_source)
        self.assertIn("cJSON_Parse(payload)", cli_source)
        self.assertIn("cJSON_IsObject(root)", cli_source)
        self.assertIn("cJSON_IsArray(root)", cli_source)
        self.assertIn("cJSON_ArrayForEach(command, root)", cli_source)
        self.assertIn("cJSON_IsObject(command)", cli_source)
        self.assertIn("CLI MQTT data topic=", cli_source)
        self.assertIn("CLI MQTT fragmented payload ignored", cli_source)

    def test_cli_tap_threshold_reinitializes_lis3dh(self):
        cli_source = read_source("components/rr1_capture/mqtt_cli.c")
        tap_source = read_source("components/rr1_capture/timer_i2c_tap.c")
        tap_header = read_source("components/rr1_capture/include/timer_i2c_tap.h")

        self.assertIn('"tap_threshold"', cli_source)
        self.assertIn("invoking rr1_i2c_tap_reinit", cli_source)
        self.assertIn("rr1_i2c_tap_reinit((uint8_t)value->valueint)", cli_source)
        self.assertIn("void rr1_i2c_tap_reinit(uint8_t tap_threshold)", tap_header)
        self.assertIn("lis3dh_tap_threshold = tap_threshold", tap_source)
        self.assertIn("LIS3DH_REG_CLICK_THS, lis3dh_tap_threshold", tap_source)

    def test_timer_hist_calloc_failures_are_logged_and_reboot(self):
        source = read_source("components/rr1_capture/timer_hist.c")
        wrapper = c_function_body(source, "checkedCalloc")

        self.assertIn("#define RR1_CALLOC(size, label)", source)
        self.assertIn("static void *checkedCalloc", source)
        self.assertIn("failed to allocate %s size=%zu", wrapper)
        self.assertIn("internal_free=%zu", wrapper)
        self.assertIn("esp_restart();", wrapper)
        self.assertNotIn("vTaskDelay", wrapper)
        self.assertNotIn("count=", wrapper)
        self.assertNotIn("= calloc(", source.replace("void *ptr = calloc(", ""))


if __name__ == "__main__":
    unittest.main()
