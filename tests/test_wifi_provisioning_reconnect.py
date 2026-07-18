import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def read_source(relative_path):
    return (ROOT / relative_path).read_text()


class WifiProvisioningReconnectTests(unittest.TestCase):
    def test_disconnect_handler_does_not_reapply_link_settings(self):
        source = read_source("components/rr1_wifi_prov/app_main.c")
        disconnect_case = source.split("case WIFI_EVENT_STA_DISCONNECTED:", 1)[1]
        disconnect_case = disconnect_case.split("#ifdef CONFIG_EXAMPLE_PROV_TRANSPORT_SOFTAP", 1)[0]

        self.assertIn("timerMqttWifiDisconnected", disconnect_case)
        self.assertIn("wifi_provisioning_reset_pending", disconnect_case)
        self.assertIn("Skipping Wi-Fi reconnect during provisioning reset", disconnect_case)
        self.assertIn("esp_wifi_connect();", disconnect_case)
        self.assertNotIn("apply_wifi_link_settings();", disconnect_case)

    def test_button_reset_marks_reconnect_as_intentional_teardown(self):
        source = read_source("components/rr1_wifi_prov/app_main.c")
        callback = source.split("button_single_click_cb", 1)[1]
        callback = callback.split("static void hookReset", 1)[0]

        flag_pos = callback.find("wifi_provisioning_reset_pending = true;")
        reset_pos = callback.find("wifi_prov_mgr_reset_provisioning();")

        self.assertNotEqual(flag_pos, -1)
        self.assertNotEqual(reset_pos, -1)
        self.assertLess(flag_pos, reset_pos)


if __name__ == "__main__":
    unittest.main()
