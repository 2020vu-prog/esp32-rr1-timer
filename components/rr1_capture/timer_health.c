
#include "driver/temperature_sensor.h"
#include "esp_wifi.h"
#include "esp_event.h"

#include "timer_health.h"

temperature_sensor_handle_t temp_handle = NULL;
static bool init = false;
void health_init()
{
	temperature_sensor_config_t temp_sensor_config = TEMPERATURE_SENSOR_CONFIG_DEFAULT(20, 80);
	ESP_ERROR_CHECK(temperature_sensor_install(&temp_sensor_config, &temp_handle));
}
float health_cpu_temp()
{
	if (!init)
	{
		health_init();
		init = true;
	}

	// Enable temperature sensor
	ESP_ERROR_CHECK(temperature_sensor_enable(temp_handle));
	// Get converted sensor data
	float tsens_out;
	ESP_ERROR_CHECK(temperature_sensor_get_celsius(temp_handle, &tsens_out));

	tsens_out = (float)((int)(tsens_out * 10.)) / 10.;

	printf("Temperature in %f °C\n", tsens_out);
	// Disable the temperature sensor if it is not needed and save the power
	ESP_ERROR_CHECK(temperature_sensor_disable(temp_handle));
	return tsens_out;
}
int getWifiRssi(){
	wifi_ap_record_t ap_info;
	if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
		return ap_info.rssi;
	}
	return 0;
}
void get_device_mac(char *mac, size_t max)
{
    uint8_t eth_mac[6];
    esp_wifi_get_mac(WIFI_IF_STA, eth_mac);
    snprintf(mac, max, "%02X:%02X:%02X:%02X:%02X:%02X",
             eth_mac[0], eth_mac[1], eth_mac[2], eth_mac[3], eth_mac[4], eth_mac[5]);

}