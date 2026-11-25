/*
 * Internal EMAC example (ESP32 + RMII PHY, e.g. WT32-ETH01, Olimex ESP32-POE, LAN8720 modules)
 *
 * Typical LAN8720 board: MDC = GPIO23, MDIO = GPIO18, 50 MHz clock input on GPIO0,
 * PHY power/reset pin differs per board (WT32-ETH01: GPIO16, Olimex ESP32-POE: GPIO12).
 */

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "Ethernet.h"

static const char *TAG = "example";

extern "C" void app_main(void)
{
	internal_eth_config_t cfg = INTERNAL_ETH_DEFAULT_CONFIG();
	cfg.phy = PHY_LAN87XX;
	cfg.phy_addr = 1;            // -1 = auto detect
	cfg.phy_reset_gpio = 16;     // WT32-ETH01 PHY power pin
	cfg.mdc_gpio = 23;
	cfg.mdio_gpio = 18;
	cfg.clock_mode = EMAC_CLK_EXT_IN;
	cfg.clock_gpio = 0;

	Ethernet.onGotIp([](esp_netif_ip_info_t* info) {
		ESP_LOGI(TAG, "got IP: " IPSTR, IP2STR(&info->ip));
	});
	Ethernet.onLostIp([]() {
		ESP_LOGW(TAG, "lost IP");
	});

	if (!Ethernet.init(&cfg)) {
		ESP_LOGE(TAG, "Ethernet init failed");
		return;
	}
	Ethernet.setHostname("esp32-emac");
	Ethernet.start();

	while (true) {
		ESP_LOGI(TAG, "link=%d ip=%s", Ethernet.isConnected(), EthernetClass::ipToString(Ethernet.localIP()).c_str());
		vTaskDelay(pdMS_TO_TICKS(5000));
	}
}
