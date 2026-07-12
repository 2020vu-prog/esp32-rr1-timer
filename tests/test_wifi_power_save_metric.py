import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def read_source(relative_path):
    return (ROOT / relative_path).read_text()


class WifiPowerSaveMetricTests(unittest.TestCase):
    def test_publish_power_save_transitions_are_tracked(self):
        source = read_source("components/rr1_capture/timer_mqtt.c")

        self.assertIn("setTrackedWifiPowerSaveMode(WIFI_PS_NONE, \"publish\")", source)
        self.assertIn(
            "setTrackedWifiPowerSaveMode(WIFI_PS_MIN_MODEM, \"publish ack\")",
            source,
        )
        self.assertIn("getRecentWifiPsMinModemPercentAverage", source)

    def test_health_logs_recent_min_modem_percentage(self):
        source = read_source("components/rr1_capture/timer_hist.c")

        self.assertIn("wifi_ps_min_modem percent", source)
        self.assertIn("getRecentWifiPsMinModemPercentAverage()", source)


if __name__ == "__main__":
    unittest.main()
