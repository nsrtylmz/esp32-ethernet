/*
 * W5500 SPI Ethernet example
 *
 * Wiring (ESP32 <-> W5500), change the pins below for your board:
 *   GPIO12 -> SCLK, GPIO14 <- MISO, GPIO27 -> MOSI, GPIO13 -> CS, GPIO26 <- INT, GPIO25 -> RST
 */

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "Ethernet.h"

static const char *TAG = "example";

#define ETH_SPI_HOST     SPI2_HOST
#define ETH_SPI_SCLK     GPIO_NUM_12
#define ETH_SPI_MISO     GPIO_NUM_14
#define ETH_SPI_MOSI     GPIO_NUM_27
#define ETH_SPI_CS       GPIO_NUM_13
#define ETH_SPI_INT      GPIO_NUM_26    // -1 -> polling mode
#define ETH_PHY_RST      GPIO_NUM_25    // -1 -> not connected

// Set to 1 to use a static IP instead of DHCP
#define USE_STATIC_IP    0

extern "C" void app_main(void)
{
	// 1) SPI bus
	if (!EthernetClass::spiBusInit(ETH_SPI_HOST, ETH_SPI_MISO, ETH_SPI_MOSI, ETH_SPI_SCLK)) {
		ESP_LOGE(TAG, "SPI bus init failed");
		return;
	}

	// 2) Callbacks (run in the default event loop task - keep them short)
	Ethernet.onConnected([]() {
		ESP_LOGI(TAG, "link up");
	});
	Ethernet.onDisconnected([]() {
		ESP_LOGW(TAG, "link down");
	});
	Ethernet.onGotIp([](esp_netif_ip_info_t* info) {
		ESP_LOGI(TAG, "got IP: " IPSTR, IP2STR(&info->ip));
	});

	// 3) Driver. mac_addr = nullptr -> MAC is derived from the ESP32 eFuse.
	if (!Ethernet.init(ETH_SPI_HOST, nullptr, ETH_SPI_CS, ETH_SPI_INT, ETH_PHY_RST)) {
		ESP_LOGE(TAG, "Ethernet init failed");
		return;
	}

	Ethernet.setHostname("esp32-w5500");

#if USE_STATIC_IP
	// Either with strings ...
	Ethernet.config("192.168.1.50", "192.168.1.1", "255.255.255.0", "8.8.8.8");

	// ... or with ESP-IDF esp_ip4_addr_t values:
	// esp_ip4_addr_t ip   = { .addr = ESP_IP4TOADDR(192, 168, 1, 50) };
	// esp_ip4_addr_t gw   = { .addr = ESP_IP4TOADDR(192, 168, 1, 1) };
	// esp_ip4_addr_t mask = { .addr = ESP_IP4TOADDR(255, 255, 255, 0) };
	// Ethernet.config(ip, gw, mask);
#endif

	// 4) Start
	if (!Ethernet.start()) {
		ESP_LOGE(TAG, "Ethernet start failed");
		return;
	}

	ESP_LOGI(TAG, "MAC: %s", Ethernet.macAddress().c_str());

	while (true) {
		if (Ethernet.hasIP()) {
			esp_netif_ip_info_t info;
			Ethernet.getIpInfo(&info);
			esp_ip4_addr_t dns = Ethernet.dnsIP();
			ESP_LOGI(TAG, "IP " IPSTR "  GW " IPSTR "  MASK " IPSTR "  DNS " IPSTR "  %u Mbps %s",
			         IP2STR(&info.ip), IP2STR(&info.gw), IP2STR(&info.netmask), IP2STR(&dns),
			         Ethernet.linkSpeed(),
			         Ethernet.fullDuplex() ? "full-duplex" : "half-duplex");
		}
		else {
			ESP_LOGI(TAG, "waiting for %s...", Ethernet.isConnected() ? "IP" : "link");
		}
		vTaskDelay(pdMS_TO_TICKS(5000));
	}
}
