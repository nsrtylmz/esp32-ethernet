/*
 * API tests that do not need Ethernet hardware:
 * every method must fail gracefully (no crash, no leak) on an uninitialised
 * object or with an invalid configuration.
 */

#include <string.h>
#include "unity.h"
#include "esp_heap_caps.h"
#include "Ethernet.h"

static esp_ip4_addr_t ip4(uint8_t a, uint8_t b, uint8_t c, uint8_t d)
{
	esp_ip4_addr_t r;
	r.addr = ESP_IP4TOADDR(a, b, c, d);
	return r;
}

TEST_CASE("ipToString formats addresses", "[api]")
{
	TEST_ASSERT_EQUAL_STRING("0.0.0.0", EthernetClass::ipToString(esp_ip4_addr_t{}).c_str());
	TEST_ASSERT_EQUAL_STRING("192.168.1.10", EthernetClass::ipToString(ip4(192, 168, 1, 10)).c_str());
	TEST_ASSERT_EQUAL_STRING("255.255.255.255", EthernetClass::ipToString(ip4(255, 255, 255, 255)).c_str());
}

TEST_CASE("uninitialised object fails gracefully", "[api]")
{
	EthernetClass eth;

	TEST_ASSERT_FALSE(eth.isInitialized());
	TEST_ASSERT_FALSE(eth.isStarted());
	TEST_ASSERT_FALSE(eth.isConnected());
	TEST_ASSERT_FALSE(eth.hasIP());
	TEST_ASSERT_NULL(eth.netif());
	TEST_ASSERT_NULL(eth.handle());

	TEST_ASSERT_FALSE(eth.start());
	TEST_ASSERT_FALSE(eth.stop());
	TEST_ASSERT_FALSE(eth.config());
	TEST_ASSERT_FALSE(eth.config(ip4(192, 168, 1, 2), ip4(192, 168, 1, 1), ip4(255, 255, 255, 0)));
	TEST_ASSERT_FALSE(eth.config("192.168.1.2", "192.168.1.1", "255.255.255.0"));

	TEST_ASSERT_EQUAL_UINT32(0, eth.localIP().addr);
	TEST_ASSERT_EQUAL_UINT32(0, eth.subnetMask().addr);
	TEST_ASSERT_EQUAL_UINT32(0, eth.gatewayIP().addr);
	TEST_ASSERT_EQUAL_UINT32(0, eth.dnsIP(0).addr);
	TEST_ASSERT_EQUAL_UINT32(0, eth.dnsIP(5).addr);

	esp_netif_ip_info_t info;
	memset(&info, 0xAA, sizeof(info));
	TEST_ASSERT_FALSE(eth.getIpInfo(&info));
	TEST_ASSERT_EQUAL_UINT32(0, info.ip.addr);
	TEST_ASSERT_FALSE(eth.getIpInfo(nullptr));

	TEST_ASSERT_EQUAL_UINT16(0, eth.linkSpeed());
	TEST_ASSERT_FALSE(eth.fullDuplex());

	uint8_t mac[6];
	memset(mac, 0xAA, sizeof(mac));
	TEST_ASSERT_EQUAL_PTR(mac, eth.macAddress(mac));
	const uint8_t zero[6] = {0};
	TEST_ASSERT_EQUAL_UINT8_ARRAY(zero, mac, 6);
	TEST_ASSERT_NULL(eth.macAddress(nullptr));
	TEST_ASSERT_EQUAL_STRING("00:00:00:00:00:00", eth.macAddress().c_str());

	TEST_ASSERT_FALSE(eth.setHostname("test"));
	TEST_ASSERT_FALSE(eth.setHostname(nullptr));
	TEST_ASSERT_NULL(eth.getHostname());

	eth.deinit();   // must be safe on an uninitialised object
	eth.deinit();
}

TEST_CASE("callbacks can be set and cleared", "[api]")
{
	EthernetClass eth;
	int calls = 0;
	eth.onConnected([&]() { calls++; });
	eth.onDisconnected([&]() { calls++; });
	eth.onGotIp([&](esp_netif_ip_info_t*) { calls++; });
	eth.onLostIp([&]() { calls++; });
	eth.onConnected(nullptr);
	eth.onDisconnected(nullptr);
	eth.onGotIp(nullptr);
	eth.onLostIp(nullptr);
	TEST_ASSERT_EQUAL(0, calls);
}

#if CONFIG_ETH_USE_SPI_ETHERNET

TEST_CASE("SPI init rejects null config", "[api]")
{
	EthernetClass eth;
	TEST_ASSERT_FALSE(eth.init(SPI2_HOST, (const spi_eth_module_config_t*)nullptr));
	TEST_ASSERT_FALSE(eth.isInitialized());
}

#if !CONFIG_ETH_SPI_ETHERNET_DM9051
TEST_CASE("SPI init rejects module not enabled in menuconfig", "[api]")
{
	EthernetClass eth;
	spi_eth_module_config_t cfg = SPI_ETH_MODULE_DEFAULT_CONFIG(5, -1);
	cfg.module = SPI_MODULE_DM9051;
	TEST_ASSERT_FALSE(eth.init(SPI2_HOST, &cfg));
	TEST_ASSERT_FALSE(eth.isInitialized());
	TEST_ASSERT_NULL(eth.netif());
}
#endif

TEST_CASE("SPI init fails cleanly when SPI bus is not initialised", "[api]")
{
	// Warm up lazily allocated system resources (netif, event loop, ISR service)
	{
		EthernetClass warmup;
		warmup.init(SPI3_HOST, nullptr, 5, -1);
	}

	size_t heap_before = heap_caps_get_free_size(MALLOC_CAP_DEFAULT);
	for (int i = 0; i < 3; i++) {
		EthernetClass eth;
		TEST_ASSERT_FALSE(eth.init(SPI3_HOST, nullptr, 5, -1));
		TEST_ASSERT_FALSE(eth.isInitialized());
		TEST_ASSERT_NULL(eth.netif());
		TEST_ASSERT_FALSE(eth.start());
	}
	size_t heap_after = heap_caps_get_free_size(MALLOC_CAP_DEFAULT);
	// small tolerance for log buffers etc.
	TEST_ASSERT_INT_WITHIN(256, heap_before, heap_after);
}

TEST_CASE("spiBusInit accepts an already initialised bus", "[api]")
{
	TEST_ASSERT_TRUE(EthernetClass::spiBusInit(SPI3_HOST, -1, -1, -1));
	TEST_ASSERT_TRUE(EthernetClass::spiBusInit(SPI3_HOST, -1, -1, -1));   // ESP_ERR_INVALID_STATE -> ok
	TEST_ASSERT_EQUAL(ESP_OK, spi_bus_free(SPI3_HOST));
}

#endif // CONFIG_ETH_USE_SPI_ETHERNET
