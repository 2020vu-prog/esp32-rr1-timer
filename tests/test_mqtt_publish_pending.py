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


if __name__ == "__main__":
    unittest.main()
