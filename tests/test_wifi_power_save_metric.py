import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def read_source(relative_path):
    return (ROOT / relative_path).read_text()


class WifiPowerSaveMetricTests(unittest.TestCase):
    def test_publish_power_save_transitions_are_tracked(self):
        mqtt_source = read_source("components/rr1_capture/timer_mqtt.c")
        power_source = read_source("components/rr1_capture/wifi_power.c")

        self.assertIn("atomic_bool mqttPublishHold", power_source)
        self.assertIn("atomic_bool otaHold", power_source)
        self.assertIn("atomic_bool mqttConnectHold", power_source)
        self.assertIn(
            "wifiPowerHold(WIFI_POWER_HOLD_MQTT_PUBLISH, \"publish\")",
            mqtt_source,
        )
        self.assertIn(
            "wifiPowerRelease(WIFI_POWER_HOLD_MQTT_PUBLISH",
            mqtt_source,
        )
        self.assertIn("WIFI_POWER_HOLD_MQTT_CONNECT", mqtt_source)
        self.assertIn(
            "wifiPowerRelease(WIFI_POWER_HOLD_MQTT_CONNECT, \"mqtt connected\")",
            mqtt_source,
        )
        self.assertIn("applyTrackedWifiPowerSaveMode", power_source)
        self.assertIn("getRecentWifiPsMinModemPercentAverage", power_source)

    def test_ota_holds_wifi_awake_during_https_attempt(self):
        source = read_source("main/simple_ota_example.c")

        self.assertIn("wifiPowerHold(WIFI_POWER_HOLD_OTA", source)
        self.assertIn("wifiPowerRelease(WIFI_POWER_HOLD_OTA", source)

    def test_health_logs_recent_min_modem_percentage(self):
        source = read_source("components/rr1_capture/timer_hist.c")

        self.assertIn("wifi_ps_min_modem percent", source)
        self.assertIn("getRecentWifiPsMinModemPercentAverage()", source)


if __name__ == "__main__":
    unittest.main()
