
#include <stdio.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "nvs.h"

void nvs_dumprr1(){

	nvs_iterator_t it=NULL;
//	esp_err_t err;

	/* Initialize NVS partition */
	esp_err_t ret = nvs_flash_init();
	if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
	{
		/* NVS partition was truncated
		 * and needs to be erased */
		ESP_ERROR_CHECK(nvs_flash_erase());

		/* Retry nvs_flash_init */
		ESP_ERROR_CHECK(nvs_flash_init());
	}


// Start iteration: find the first entry in the default partition, all types, all namespaces
ESP_ERROR_CHECK(nvs_entry_find("nvs", NULL, NVS_TYPE_ANY,&it));

esp_err_t res=ESP_OK;
 while(res == ESP_OK) {
    nvs_entry_info_t info;
    nvs_entry_info(it, &info);
    
    // Process the namespace name (info.namespace_name)
    // You might add this to a list of unique namespace names
    printf("Found entry in namespace: %s, key: %s, type: %d\n", info.namespace_name, info.key, info.type);

    // Move to the next entry
         res = nvs_entry_next(&it);

}

// Release the iterator
nvs_release_iterator(it);

}