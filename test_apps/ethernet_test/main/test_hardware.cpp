/*
 * Hardware tests - need a W5500 module and a network with a DHCP server.
 * Pins are configured in menuconfig > "Ethernet Test Configuration".
 */

#include <string.h>
#include <atomic>
#include "unity.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "Ethernet.h"

#if CONFIG_ETH_USE_SPI_ETHERNET && CONFIG_ETH_SPI_ETHERNET_W5500

static const char *TAG = "eth_test";

#define TEST_HOST ((spi_host_device_t)CONFIG_TEST_ETH_SPI_HOST)

static bool wait_for(bool (*cond)(void), uint32_t timeout_ms)
{
	for (uint32_t t = 0; t < timeout_ms; t += 100) {
		if (cond()) {
			return true;
		}
		vTaskDelay(pdMS_TO_TICKS(100));
	}
	return cond();
}

static bool link_up(void) { return Ethernet.isConnected(); }
static bool has_ip(void)  { return Ethernet.hasIP(); }

static void hw_setup(uint8_t* mac = nullptr)
{
	// Clean up whatever a previously failed test left behind
	Ethernet.deinit();
	Ethernet.onConnected(nullptr);
	Ethernet.onDisconnected(nullptr);
	Ethernet.onGotIp(nullptr);
	Ethernet.onLostIp(nullptr);
	spi_bus_free(TEST_HOST);

	TEST_ASSERT_TRUE(EthernetClass::spiBusInit(TEST_HOST, CONFIG_TEST_ETH_SPI_MISO, CONFIG_TEST_ETH_SPI_MOSI, CONFIG_TEST_ETH_SPI_SCLK));
	TEST_ASSERT_TRUE(Ethernet.init(TEST_HOST, mac, CONFIG_TEST_ETH_SPI_CS, CONFIG_TEST_ETH_SPI_INT, CONFIG_TEST_ETH_PHY_RST));
	TEST_ASSERT_TRUE(Ethernet.isInitialized());
	TEST_ASSERT_NOT_NULL(Ethernet.netif());
}

static void hw_teardown(void)
{
	Ethernet.deinit();
	TEST_ASSERT_FALSE(Ethernet.isInitialized());
	spi_bus_free(TEST_HOST);
}

TEST_CASE("W5500 DHCP: link up and IP received", "[hw]")
{
	static std::atomic<int> connected_cb;
	static std::atomic<int> disconnected_cb;
	static std::atomic<int> got_ip_cb;
	connected_cb = 0;
	disconnected_cb = 0;
	got_ip_cb = 0;

	hw_setup();
	Ethernet.onConnected([]() { connected_cb++; });
	Ethernet.onDisconnected([]() { disconnected_cb++; });
	Ethernet.onGotIp([](esp_netif_ip_info_t*) { got_ip_cb++; });

	TEST_ASSERT_TRUE(Ethernet.setHostname("eth-test"));
	TEST_ASSERT_EQUAL_STRING("eth-test", Ethernet.getHostname());
	TEST_ASSERT_TRUE(Ethernet.config());
	TEST_ASSERT_TRUE(Ethernet.start());

	TEST_ASSERT_TRUE_MESSAGE(wait_for(link_up, 10000), "no link - is the cable connected?");
	TEST_ASSERT_TRUE_MESSAGE(wait_for(has_ip, 20000), "no DHCP lease");

	esp_netif_ip_info_t info;
	TEST_ASSERT_TRUE(Ethernet.getIpInfo(&info));
	ESP_LOGI(TAG, "IP " IPSTR " GW " IPSTR " MASK " IPSTR " MAC %s %u Mbps",
	         IP2STR(&info.ip), IP2STR(&info.gw), IP2STR(&info.netmask),
	         Ethernet.macAddress().c_str(), Ethernet.linkSpeed());

	TEST_ASSERT_NOT_EQUAL(0, Ethernet.localIP().addr);
	TEST_ASSERT_NOT_EQUAL(0, Ethernet.subnetMask().addr);
	TEST_ASSERT_EQUAL_UINT32(info.ip.addr, Ethernet.localIP().addr);
	TEST_ASSERT_FALSE(Ethernet.isStaticIP());
	TEST_ASSERT_NOT_EQUAL(0, Ethernet.linkSpeed());
	TEST_ASSERT_EQUAL(1, connected_cb.load());
	TEST_ASSERT_GREATER_OR_EQUAL(1, got_ip_cb.load());

	TEST_ASSERT_TRUE(Ethernet.stop());
	TEST_ASSERT_FALSE(Ethernet.isStarted());
	TEST_ASSERT_FALSE(Ethernet.isConnected());
	TEST_ASSERT_FALSE(Ethernet.hasIP());
	vTaskDelay(pdMS_TO_TICKS(200));                 // let the STOP / DISCONNECTED events run
	TEST_ASSERT_EQUAL(1, disconnected_cb.load());   // exactly once, not twice
	Ethernet.onConnected(nullptr);
	Ethernet.onDisconnected(nullptr);
	Ethernet.onGotIp(nullptr);
	hw_teardown();
}

TEST_CASE("W5500 static IP", "[hw]")
{
	hw_setup();

	esp_ip4_addr_t ip, gw, mask, dns, dns2;
	TEST_ASSERT_EQUAL(ESP_OK, esp_netif_str_to_ip4(CONFIG_TEST_ETH_STATIC_IP, &ip));
	TEST_ASSERT_EQUAL(ESP_OK, esp_netif_str_to_ip4(CONFIG_TEST_ETH_STATIC_GW, &gw));
	mask.addr = ESP_IP4TOADDR(255, 255, 255, 0);
	dns.addr  = ESP_IP4TOADDR(8, 8, 8, 8);
	dns2.addr = ESP_IP4TOADDR(1, 1, 1, 1);

	TEST_ASSERT_TRUE(Ethernet.config(ip, gw, mask, dns, dns2));
	TEST_ASSERT_TRUE(Ethernet.isStaticIP());
	TEST_ASSERT_TRUE(Ethernet.start());
	TEST_ASSERT_TRUE_MESSAGE(wait_for(link_up, 10000), "no link - is the cable connected?");
	TEST_ASSERT_TRUE(wait_for(has_ip, 5000));

	TEST_ASSERT_EQUAL_UINT32(ip.addr,   Ethernet.localIP().addr);
	TEST_ASSERT_EQUAL_UINT32(gw.addr,   Ethernet.gatewayIP().addr);
	TEST_ASSERT_EQUAL_UINT32(mask.addr, Ethernet.subnetMask().addr);
	TEST_ASSERT_EQUAL_UINT32(dns.addr,  Ethernet.dnsIP(0).addr);
	TEST_ASSERT_EQUAL_UINT32(dns2.addr, Ethernet.dnsIP(1).addr);

	// String overload + invalid strings
	TEST_ASSERT_FALSE(Ethernet.config("300.1.1.1", "192.168.1.1", "255.255.255.0"));
	TEST_ASSERT_FALSE(Ethernet.config("192.168.1.5", nullptr, "255.255.255.0"));
	TEST_ASSERT_TRUE(Ethernet.config(CONFIG_TEST_ETH_STATIC_IP, CONFIG_TEST_ETH_STATIC_GW, "255.255.255.0"));
	TEST_ASSERT_EQUAL_UINT32(ip.addr, Ethernet.localIP().addr);

	// Back to DHCP
	TEST_ASSERT_TRUE(Ethernet.config());
	TEST_ASSERT_FALSE(Ethernet.isStaticIP());

	hw_teardown();
}

TEST_CASE("W5500 init / deinit cycles and custom MAC", "[hw]")
{
	uint8_t custom_mac[6] = {0x02, 0x00, 0x00, 0x12, 0x34, 0x56};

	for (int i = 0; i < 3; i++) {
		hw_setup(custom_mac);
		// a second init without deinit must be rejected
		TEST_ASSERT_FALSE(Ethernet.init(TEST_HOST, custom_mac, CONFIG_TEST_ETH_SPI_CS, CONFIG_TEST_ETH_SPI_INT, CONFIG_TEST_ETH_PHY_RST));

		uint8_t mac[6];
		Ethernet.macAddress(mac);
		TEST_ASSERT_EQUAL_UINT8_ARRAY(custom_mac, mac, 6);
		TEST_ASSERT_EQUAL_STRING("02:00:00:12:34:56", Ethernet.macAddress().c_str());
		TEST_ASSERT_TRUE(Ethernet.start());
		hw_teardown();
	}
}

#endif // CONFIG_ETH_USE_SPI_ETHERNET && CONFIG_ETH_SPI_ETHERNET_W5500
