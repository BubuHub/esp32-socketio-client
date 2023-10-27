#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"
#include <map>
#include <set>
#include <string>
#include <esp_log.h>
#include <string>
#include "websocketclient.h"
#include "socketioclient.h"
#include "esp_system.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "protocol_examples_common.h"


static char tag[] = "WSM";
#if 1
#define msg_info(fmt, args...) ESP_LOGI(tag, fmt, ## args);
#define msg_init(fmt, args...)  ESP_LOGI(tag, fmt, ## args);
#define msg_debug(fmt, args...)  ESP_LOGI(tag, fmt, ## args);
#define msg_error(fmt, args...)  ESP_LOGE(tag, fmt, ## args);
#else
#define msg_info(fmt, args...)
#define msg_init(fmt, args...)
#define msg_debug(fmt, args...)
#define msg_error(fmt, args...)  ESP_LOGE(TAG, fmt, ## args);
#endif


/* Change to correct socketio link ! */
SocketIoClient ws(CONFIG_EXAMPLE_SIO_URL);


/*!
 * \brief MAIN.
 */
extern "C" void app_main(void)
{
    esp_chip_info_t chip_info;	

    /* Print chip information */
    esp_chip_info(&chip_info);
    ESP_LOGI(tag,"This is ESP32 chip with %d CPU cores, WiFi%s%s, ",chip_info.cores,(chip_info.features & CHIP_FEATURE_BT) ? "/BT" : "",(chip_info.features & CHIP_FEATURE_BLE) ? "/BLE" : "");
    ESP_LOGI(tag,"silicon revision %d, ", chip_info.revision);
    // ESP_LOGI(tag,"CPU frequency: %d, Maximum priority level: %d, IDLE priority level: %d",getCpuFrequencyMhz(), configMAX_PRIORITIES - 1, tskIDLE_PRIORITY);
	ESP_LOGI(tag,"main_task: active on core %d", xPortGetCoreID());

	ESP_LOGI(tag, "Initialising WiFi Connection...");

    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    /* This helper function configures Wi-Fi or Ethernet, as selected in menuconfig.
     * Read "Establishing Wi-Fi or Ethernet Connection" section in
     * examples/protocols/README.md for more information about this function.
     */
    ESP_ERROR_CHECK(example_connect());

	ws.setConnectCB([](SocketIoClient *ws, bool b) {
		msg_debug("Connected <%d>", b?1:0);
	});

	ws.setCB([](SocketIoClient *c, const char *msg, int len, int type) {
		char buf[1024];
		strncpy(buf, msg, len);
		buf[len] = '\0';
		msg_debug("Message <%s>", buf);
	});

	ws.on("seq-num", [](SocketIoClient *c, char *msg) {
		msg_debug("Got seq num <%s>", msg);
		c->send("ble", msg);
	});

	ws.start();

	while (true) {
		vTaskDelay(100);
	}
}
/* ========================================================================================== */