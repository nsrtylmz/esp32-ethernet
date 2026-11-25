/*
 * Ethernet.cpp
 *
 * SPDX-License-Identifier: MIT
 */

#include <stdio.h>
#include <string.h>

#include "esp_err.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_eth.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "lwip/inet.h"
#include "driver/gpio.h"

#include "Ethernet.h"


static const char *TAG = "Ethernet";

/* Number of EthernetClass instances ever created. Used to give each
 * interface a unique if_key / if_desc / MAC address. */
static int eth_instance_count = 0;


/* -------------------------------------------------------------------------- */
/*  Helpers                                                                   */
/* -------------------------------------------------------------------------- */

static bool netif_init(void)
{
	static bool netif_init_done = false;

	if (netif_init_done) {
		return true;
	}

	esp_err_t err = esp_netif_init();
	if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
		ESP_LOGE(TAG, "esp_netif_init failed: %s", esp_err_to_name(err));
		return false;
	}

	err = esp_event_loop_create_default();
	if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
		ESP_LOGE(TAG, "default event loop could not be created: %s", esp_err_to_name(err));
		return false;
	}

	netif_init_done = true;
	return true;
}


const char* EthernetClass::desc(void) const
{
	if (_eth_netif) {
		const char* d = esp_netif_get_desc(_eth_netif);
		if (d) {
			return d;
		}
	}
	return "eth";
}


/* -------------------------------------------------------------------------- */
/*  Event handler                                                             */
/* -------------------------------------------------------------------------- */

void EthernetClass::eventHandler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
	EthernetClass* eth = static_cast<EthernetClass*>(arg);
	if (eth == nullptr) {
		return;
	}

	if (event_base == ETH_EVENT) {
		// Events of every Ethernet interface are delivered to every instance,
		// so only handle events that belong to this driver.
		if (event_data == nullptr || *(esp_eth_handle_t *)event_data != eth->_eth_handle) {
			return;
		}

		switch (event_id) {
			case ETHERNET_EVENT_START:
				ESP_LOGI(TAG, "%s started", eth->desc());
				eth->_started = true;
				break;

			case ETHERNET_EVENT_STOP:
				// esp_eth_stop() posts ETHERNET_EVENT_DISCONNECTED first when the link was up,
				// so the disconnect callback is called from there (not twice).
				ESP_LOGI(TAG, "%s stopped", eth->desc());
				eth->_started = false;
				eth->_connected = false;
				eth->_has_ip = false;
				break;

			case ETHERNET_EVENT_CONNECTED: {
				uint8_t mac_addr[6] = {0};
				esp_eth_ioctl(eth->_eth_handle, ETH_CMD_G_MAC_ADDR, mac_addr);
				ESP_LOGI(TAG, "%s Link Up", eth->desc());
				ESP_LOGI(TAG, "%s HW Addr %02x:%02x:%02x:%02x:%02x:%02x", eth->desc(),
				         mac_addr[0], mac_addr[1], mac_addr[2], mac_addr[3], mac_addr[4], mac_addr[5]);
				eth->_connected = true;
				if (eth->connect_event) {
					eth->connect_event();
				}
				break;
			}

			case ETHERNET_EVENT_DISCONNECTED:
				ESP_LOGW(TAG, "%s Link Down", eth->desc());
				eth->_connected = false;
				eth->_has_ip = false;
				if (eth->disconnect_event) {
					eth->disconnect_event();
				}
				break;

			default:
				break;
		}
	}
	else if (event_base == IP_EVENT) {
		ip_event_got_ip_t* event = (ip_event_got_ip_t*)event_data;
		if (event == nullptr || event->esp_netif != eth->_eth_netif) {
			return;
		}

		if (event_id == IP_EVENT_ETH_GOT_IP) {
			ESP_LOGI(TAG, "%s Got IP " IPSTR " mask " IPSTR " gw " IPSTR, eth->desc(),
			         IP2STR(&event->ip_info.ip), IP2STR(&event->ip_info.netmask), IP2STR(&event->ip_info.gw));
			eth->_has_ip = true;
			if (eth->got_ip_event) {
				eth->got_ip_event(&event->ip_info);
			}
		}
		else if (event_id == IP_EVENT_ETH_LOST_IP) {
			ESP_LOGW(TAG, "%s Lost IP", eth->desc());
			eth->_has_ip = false;
			if (eth->lost_ip_event) {
				eth->lost_ip_event();
			}
		}
	}
}


/* -------------------------------------------------------------------------- */
/*  Construction                                                              */
/* -------------------------------------------------------------------------- */

EthernetClass::EthernetClass()
{
	_eth_num = eth_instance_count++;
}


EthernetClass::~EthernetClass()
{
	deinit();
}


/* -------------------------------------------------------------------------- */
/*  SPI Ethernet                                                              */
/* -------------------------------------------------------------------------- */

#if CONFIG_ETH_USE_SPI_ETHERNET

bool EthernetClass::spiBusInit(spi_host_device_t host_id, int miso_gpio, int mosi_gpio, int sclk_gpio)
{
	spi_bus_config_t buscfg = {};
	buscfg.miso_io_num = miso_gpio;
	buscfg.mosi_io_num = mosi_gpio;
	buscfg.sclk_io_num = sclk_gpio;
	buscfg.quadwp_io_num = -1;
	buscfg.quadhd_io_num = -1;

	esp_err_t err = spi_bus_initialize(host_id, &buscfg, SPI_DMA_CH_AUTO);
	if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
		ESP_LOGE(TAG, "SPI bus init failed: %s", esp_err_to_name(err));
		return false;
	}
	return true;
}


bool EthernetClass::init(spi_host_device_t host_id, uint8_t* mac_addr, uint8_t cs_gpio, int8_t int_gpio, int8_t phy_reset_gpio, uint8_t phy_addr)
{
	spi_eth_module_config_t cfg = SPI_ETH_MODULE_DEFAULT_CONFIG(cs_gpio, int_gpio);
	cfg.phy_reset_gpio = phy_reset_gpio;
	cfg.phy_addr = phy_addr;
	cfg.mac_addr = mac_addr;
	return init(host_id, &cfg);
}


bool EthernetClass::init(spi_host_device_t host_id, const spi_eth_module_config_t* spi_eth_module_config)
{
	if (spi_eth_module_config == nullptr) {
		ESP_LOGE(TAG, "SPI module config is null");
		return false;
	}
	if (_eth_handle != nullptr) {
		ESP_LOGE(TAG, "%s already initialised", desc());
		return false;
	}
	if (!netif_init()) {
		return false;
	}

	if (spi_eth_module_config->int_gpio >= 0) {
		// Install the GPIO ISR service (needed for the module interrupt) only once
		static bool isr_service_installed = false;
		if (!isr_service_installed) {
			esp_err_t err = gpio_install_isr_service(0);
			if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
				ESP_LOGE(TAG, "GPIO ISR service install failed: %s", esp_err_to_name(err));
				return false;
			}
			isr_service_installed = true;
		}
	}

	// The MAC buffer must stay valid until it was written to the driver.
	uint8_t mac_addr[6] = {0};
	if (spi_eth_module_config->mac_addr != nullptr) {
		memcpy(mac_addr, spi_eth_module_config->mac_addr, sizeof(mac_addr));
	}
	else {
		// SPI modules usually have no factory MAC, derive one from the ESP32 eFuse MAC.
		esp_err_t err = esp_read_mac(mac_addr, ESP_MAC_ETH);
		if (err != ESP_OK) {
			ESP_LOGE(TAG, "MAC address could not be read: %s", esp_err_to_name(err));
			return false;
		}
		mac_addr[5] += _eth_num;
	}

	if (!initSpiDriver(host_id, spi_eth_module_config, mac_addr)) {
		releaseDriver();
		return false;
	}

	if (!attachNetif()) {
		deinit();
		return false;
	}
	return true;
}


bool EthernetClass::initSpiDriver(spi_host_device_t host_id, const spi_eth_module_config_t* cfg, const uint8_t* mac_addr)
{
	eth_mac_config_t mac_config = ETH_MAC_DEFAULT_CONFIG();
	eth_phy_config_t phy_config = ETH_PHY_DEFAULT_CONFIG();
	phy_config.phy_addr = cfg->phy_addr;
	phy_config.reset_gpio_num = cfg->phy_reset_gpio;

	uint8_t clock_mhz = cfg->spi_clock_mhz ? cfg->spi_clock_mhz : ETH_SPI_CLOCK_MHZ;

	spi_device_interface_config_t spi_devcfg = {};
	spi_devcfg.mode = 0;
	spi_devcfg.clock_speed_hz = clock_mhz * 1000 * 1000;
	spi_devcfg.spics_io_num = cfg->spi_cs_gpio;
	spi_devcfg.queue_size = 20;

	int int_gpio = cfg->int_gpio;
	uint32_t poll_ms = 0;
	if (int_gpio < 0) {
		int_gpio = -1;
		poll_ms = cfg->polling_ms ? cfg->polling_ms : 10;
	}

	// Avoid unused warnings when no SPI module is enabled in menuconfig
	(void)mac_config; (void)phy_config; (void)spi_devcfg; (void)int_gpio; (void)poll_ms; (void)host_id;

	switch (cfg->module) {
#if CONFIG_ETH_SPI_ETHERNET_W5500
		case SPI_MODULE_W5500: {
			eth_w5500_config_t w5500_config = ETH_W5500_DEFAULT_CONFIG(host_id, &spi_devcfg);
			w5500_config.int_gpio_num = int_gpio;
			w5500_config.poll_period_ms = poll_ms;
			_mac = esp_eth_mac_new_w5500(&w5500_config, &mac_config);
			_phy = esp_eth_phy_new_w5500(&phy_config);
			break;
		}
#endif
#if CONFIG_ETH_SPI_ETHERNET_DM9051
		case SPI_MODULE_DM9051: {
			eth_dm9051_config_t dm9051_config = ETH_DM9051_DEFAULT_CONFIG(host_id, &spi_devcfg);
			dm9051_config.int_gpio_num = int_gpio;
			dm9051_config.poll_period_ms = poll_ms;
			_mac = esp_eth_mac_new_dm9051(&dm9051_config, &mac_config);
			_phy = esp_eth_phy_new_dm9051(&phy_config);
			break;
		}
#endif
#if CONFIG_ETH_SPI_ETHERNET_KSZ8851SNL
		case SPI_MODULE_KSZ8851SNL: {
			eth_ksz8851snl_config_t ksz_config = ETH_KSZ8851SNL_DEFAULT_CONFIG(host_id, &spi_devcfg);
			ksz_config.int_gpio_num = int_gpio;
			ksz_config.poll_period_ms = poll_ms;
			_mac = esp_eth_mac_new_ksz8851snl(&ksz_config, &mac_config);
			_phy = esp_eth_phy_new_ksz8851snl(&phy_config);
			break;
		}
#endif
		default:
			ESP_LOGE(TAG, "SPI Ethernet module %d is not supported or not enabled in menuconfig "
			              "(Component config > Ethernet > Support SPI to Ethernet Module)", (int)cfg->module);
			return false;
	}

	if (_mac == nullptr || _phy == nullptr) {
		ESP_LOGE(TAG, "SPI Ethernet MAC/PHY could not be created (SPI bus not initialised?)");
		return false;
	}

	esp_eth_config_t eth_config = ETH_DEFAULT_CONFIG(_mac, _phy);
	esp_err_t err = esp_eth_driver_install(&eth_config, &_eth_handle);
	if (err != ESP_OK) {
		ESP_LOGE(TAG, "SPI Ethernet driver install failed: %s", esp_err_to_name(err));
		_eth_handle = nullptr;
		return false;
	}

	// SPI Ethernet modules usually have no burned-in MAC, so set it manually.
	err = esp_eth_ioctl(_eth_handle, ETH_CMD_S_MAC_ADDR, (void*)mac_addr);
	if (err != ESP_OK) {
		ESP_LOGE(TAG, "SPI Ethernet MAC address config failed: %s", esp_err_to_name(err));
		return false;
	}
	return true;
}

#endif // CONFIG_ETH_USE_SPI_ETHERNET


/* -------------------------------------------------------------------------- */
/*  Internal EMAC                                                             */
/* -------------------------------------------------------------------------- */

#if CONFIG_ETH_USE_ESP32_EMAC

bool EthernetClass::init(phy_ethernet_module_t phy, int phy_addr, int mdc_gpio, int mdio_gpio, int phy_reset_gpio,
                         emac_rmii_clock_mode_t clock_mode, int clock_gpio, uint8_t* mac_addr)
{
	internal_eth_config_t cfg = INTERNAL_ETH_DEFAULT_CONFIG();
	cfg.phy = phy;
	cfg.phy_addr = phy_addr;
	cfg.mdc_gpio = mdc_gpio;
	cfg.mdio_gpio = mdio_gpio;
	cfg.phy_reset_gpio = phy_reset_gpio;
	cfg.clock_mode = clock_mode;
	cfg.clock_gpio = clock_gpio;
	cfg.mac_addr = mac_addr;
	return init(&cfg);
}


bool EthernetClass::init(const internal_eth_config_t* config)
{
	if (config == nullptr) {
		ESP_LOGE(TAG, "internal Ethernet config is null");
		return false;
	}
	if (_eth_handle != nullptr) {
		ESP_LOGE(TAG, "%s already initialised", desc());
		return false;
	}
	if (!netif_init()) {
		return false;
	}

	eth_mac_config_t mac_config = ETH_MAC_DEFAULT_CONFIG();
	eth_phy_config_t phy_config = ETH_PHY_DEFAULT_CONFIG();
	phy_config.phy_addr = config->phy_addr;
	phy_config.reset_gpio_num = config->phy_reset_gpio;

	eth_esp32_emac_config_t emac_config = ETH_ESP32_EMAC_DEFAULT_CONFIG();
	emac_config.smi_gpio.mdc_num = config->mdc_gpio;
	emac_config.smi_gpio.mdio_num = config->mdio_gpio;
	emac_config.clock_config.rmii.clock_mode = config->clock_mode;
	emac_config.clock_config.rmii.clock_gpio = (emac_rmii_clock_gpio_t)config->clock_gpio;

	_mac = esp_eth_mac_new_esp32(&emac_config, &mac_config);

	switch (config->phy) {
		case PHY_IP101:   _phy = esp_eth_phy_new_ip101(&phy_config);   break;
		case PHY_RTL8201: _phy = esp_eth_phy_new_rtl8201(&phy_config); break;
		case PHY_LAN87XX: _phy = esp_eth_phy_new_lan87xx(&phy_config); break;
		case PHY_DP83848: _phy = esp_eth_phy_new_dp83848(&phy_config); break;
		case PHY_KSZ8041:
		case PHY_KSZ8081: _phy = esp_eth_phy_new_ksz80xx(&phy_config); break;
		default:
			ESP_LOGE(TAG, "unknown PHY type %d", (int)config->phy);
			releaseDriver();
			return false;
	}

	if (_mac == nullptr || _phy == nullptr) {
		ESP_LOGE(TAG, "EMAC/PHY could not be created");
		releaseDriver();
		return false;
	}

	esp_eth_config_t eth_config = ETH_DEFAULT_CONFIG(_mac, _phy);
	esp_err_t err = esp_eth_driver_install(&eth_config, &_eth_handle);
	if (err != ESP_OK) {
		ESP_LOGE(TAG, "EMAC driver install failed: %s", esp_err_to_name(err));
		_eth_handle = nullptr;
		releaseDriver();
		return false;
	}

	if (config->mac_addr != nullptr) {
		err = esp_eth_ioctl(_eth_handle, ETH_CMD_S_MAC_ADDR, config->mac_addr);
		if (err != ESP_OK) {
			ESP_LOGE(TAG, "EMAC MAC address config failed: %s", esp_err_to_name(err));
			releaseDriver();
			return false;
		}
	}

	if (!attachNetif()) {
		deinit();
		return false;
	}
	return true;
}

#endif // CONFIG_ETH_USE_ESP32_EMAC


/* -------------------------------------------------------------------------- */
/*  Common netif setup / teardown                                             */
/* -------------------------------------------------------------------------- */

bool EthernetClass::attachNetif(void)
{
	char if_key_str[16];
	char if_desc_str[16];
	snprintf(if_key_str, sizeof(if_key_str), "ETH_%d", _eth_num);
	snprintf(if_desc_str, sizeof(if_desc_str), "eth%d", _eth_num);

	esp_netif_inherent_config_t esp_netif_config = ESP_NETIF_INHERENT_DEFAULT_ETH();
	esp_netif_config.if_key = if_key_str;		// copied by esp_netif_new()
	esp_netif_config.if_desc = if_desc_str;		// copied by esp_netif_new()
	esp_netif_config.route_prio -= _eth_num;

	esp_netif_config_t netif_cfg = {};
	netif_cfg.base = &esp_netif_config;
	netif_cfg.driver = nullptr;
	netif_cfg.stack = ESP_NETIF_NETSTACK_DEFAULT_ETH;

	_eth_netif = esp_netif_new(&netif_cfg);
	if (_eth_netif == nullptr) {
		ESP_LOGE(TAG, "esp_netif_new failed");
		return false;
	}

	_glue = esp_eth_new_netif_glue(_eth_handle);
	if (_glue == nullptr) {
		ESP_LOGE(TAG, "esp_eth_new_netif_glue failed");
		return false;
	}

	// Attach Ethernet driver to TCP/IP stack
	esp_err_t err = esp_netif_attach(_eth_netif, _glue);
	if (err != ESP_OK) {
		ESP_LOGE(TAG, "esp_netif_attach failed: %s", esp_err_to_name(err));
		return false;
	}

	err = esp_event_handler_instance_register(ETH_EVENT, ESP_EVENT_ANY_ID, &EthernetClass::eventHandler, this, &_eth_evt);
	if (err == ESP_OK) {
		err = esp_event_handler_instance_register(IP_EVENT, ESP_EVENT_ANY_ID, &EthernetClass::eventHandler, this, &_ip_evt);
	}
	if (err != ESP_OK) {
		ESP_LOGE(TAG, "event handler register failed: %s", esp_err_to_name(err));
		return false;
	}

	ESP_LOGI(TAG, "%s initialised", desc());
	return true;
}


void EthernetClass::releaseDriver(void)
{
	if (_eth_handle != nullptr) {
		esp_err_t err = esp_eth_driver_uninstall(_eth_handle);
		if (err != ESP_OK) {
			ESP_LOGW(TAG, "driver uninstall failed: %s", esp_err_to_name(err));
		}
		_eth_handle = nullptr;
	}
	if (_mac != nullptr) {
		_mac->del(_mac);
		_mac = nullptr;
	}
	if (_phy != nullptr) {
		_phy->del(_phy);
		_phy = nullptr;
	}
}


void EthernetClass::deinit(void)
{
	if (_eth_handle != nullptr && _started) {
		esp_eth_stop(_eth_handle);
	}

	if (_eth_evt != nullptr) {
		esp_event_handler_instance_unregister(ETH_EVENT, ESP_EVENT_ANY_ID, _eth_evt);
		_eth_evt = nullptr;
	}
	if (_ip_evt != nullptr) {
		esp_event_handler_instance_unregister(IP_EVENT, ESP_EVENT_ANY_ID, _ip_evt);
		_ip_evt = nullptr;
	}

	if (_glue != nullptr) {
		esp_eth_del_netif_glue(_glue);
		_glue = nullptr;
	}
	if (_eth_netif != nullptr) {
		esp_netif_destroy(_eth_netif);
		_eth_netif = nullptr;
	}

	releaseDriver();

	_started = false;
	_connected = false;
	_has_ip = false;
	_useStaticIp = false;
}


/* -------------------------------------------------------------------------- */
/*  IP configuration                                                          */
/* -------------------------------------------------------------------------- */

bool EthernetClass::config(esp_ip4_addr_t local_ip, esp_ip4_addr_t gateway, esp_ip4_addr_t subnet, esp_ip4_addr_t dns, esp_ip4_addr_t dns2)
{
	if (_eth_netif == nullptr) {
		ESP_LOGE(TAG, "interface not initialised, call init() first");
		return false;
	}

	const uint32_t ip_u32 = local_ip.addr;
	if (ip_u32 == 0 || ip_u32 == INADDR_NONE) {
		// No static address -> DHCP
		return config();
	}

	esp_netif_ip_info_t ip_info;
	memset(&ip_info, 0, sizeof(ip_info));
	ip_info.ip.addr = ip_u32;
	ip_info.gw = gateway;
	ip_info.netmask = subnet;

	esp_err_t err = esp_netif_dhcpc_stop(_eth_netif);
	if (err != ESP_OK && err != ESP_ERR_ESP_NETIF_DHCP_ALREADY_STOPPED) {
		ESP_LOGE(TAG, "%s DHCP client could not be stopped: %s", desc(), esp_err_to_name(err));
		return false;
	}

	err = esp_netif_set_ip_info(_eth_netif, &ip_info);
	if (err != ESP_OK) {
		ESP_LOGE(TAG, "%s static IP could not be configured: %s", desc(), esp_err_to_name(err));
		return false;
	}
	_useStaticIp = true;

	const uint32_t dns_list[2] = { dns.addr, dns2.addr };
	const esp_netif_dns_type_t dns_type[2] = { ESP_NETIF_DNS_MAIN, ESP_NETIF_DNS_BACKUP };
	for (int i = 0; i < 2; i++) {
		if (dns_list[i] == 0 || dns_list[i] == INADDR_NONE) {
			continue;
		}
		esp_netif_dns_info_t dns_info;
		memset(&dns_info, 0, sizeof(dns_info));
		dns_info.ip.type = ESP_IPADDR_TYPE_V4;
		dns_info.ip.u_addr.ip4.addr = dns_list[i];
		err = esp_netif_set_dns_info(_eth_netif, dns_type[i], &dns_info);
		if (err != ESP_OK) {
			ESP_LOGW(TAG, "%s DNS%d could not be set: %s", desc(), i + 1, esp_err_to_name(err));
		}
	}

	return true;
}


bool EthernetClass::config(const char* local_ip, const char* gateway, const char* subnet, const char* dns, const char* dns2)
{
	const char* str[5] = { local_ip, gateway, subnet, dns, dns2 };
	esp_ip4_addr_t addr[5] = {};
	for (int i = 0; i < 5; i++) {
		if (str[i] == nullptr || *str[i] == 0) {
			if (i < 3) {
				ESP_LOGE(TAG, "IP / gateway / subnet must not be empty");
				return false;
			}
			continue;
		}
		if (esp_netif_str_to_ip4(str[i], &addr[i]) != ESP_OK) {
			ESP_LOGE(TAG, "invalid IP address string: \"%s\"", str[i]);
			return false;
		}
	}
	return config(addr[0], addr[1], addr[2], addr[3], addr[4]);
}


bool EthernetClass::config(void)
{
	if (_eth_netif == nullptr) {
		ESP_LOGE(TAG, "interface not initialised, call init() first");
		return false;
	}

	esp_err_t err = esp_netif_dhcpc_start(_eth_netif);
	if (err != ESP_OK && err != ESP_ERR_ESP_NETIF_DHCP_ALREADY_STARTED) {
		ESP_LOGE(TAG, "%s DHCP client could not be started: %s", desc(), esp_err_to_name(err));
		return false;
	}

	_useStaticIp = false;
	return true;
}


/* -------------------------------------------------------------------------- */
/*  Start / stop                                                              */
/* -------------------------------------------------------------------------- */

bool EthernetClass::start(void)
{
	if (_eth_handle == nullptr) {
		ESP_LOGE(TAG, "not initialised, call init() first");
		return false;
	}

	if (_useStaticIp) {
		// Make sure DHCP does not overwrite the static address
		esp_netif_dhcpc_stop(_eth_netif);
	}
	// With DHCP the client is started automatically by esp_netif when the link comes up.

	esp_err_t err = esp_eth_start(_eth_handle);
	if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
		ESP_LOGE(TAG, "%s start failed: %s", desc(), esp_err_to_name(err));
		return false;
	}
	_started = true;
	return true;
}


bool EthernetClass::stop(void)
{
	if (_eth_handle == nullptr) {
		return false;
	}

	esp_err_t err = esp_eth_stop(_eth_handle);
	if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
		ESP_LOGE(TAG, "%s stop failed: %s", desc(), esp_err_to_name(err));
		return false;
	}
	// Update the state immediately; the STOP / DISCONNECTED events arrive asynchronously.
	_started = false;
	_connected = false;
	_has_ip = false;
	return true;
}


/* -------------------------------------------------------------------------- */
/*  Status                                                                    */
/* -------------------------------------------------------------------------- */

uint16_t EthernetClass::linkSpeed(void)
{
	if (_eth_handle == nullptr || !_connected) {
		return 0;
	}
	eth_speed_t speed = ETH_SPEED_10M;
	if (esp_eth_ioctl(_eth_handle, ETH_CMD_G_SPEED, &speed) != ESP_OK) {
		return 0;
	}
	return (speed == ETH_SPEED_100M) ? 100 : 10;
}


bool EthernetClass::fullDuplex(void)
{
	if (_eth_handle == nullptr || !_connected) {
		return false;
	}
	eth_duplex_t duplex = ETH_DUPLEX_HALF;
	if (esp_eth_ioctl(_eth_handle, ETH_CMD_G_DUPLEX_MODE, &duplex) != ESP_OK) {
		return false;
	}
	return duplex == ETH_DUPLEX_FULL;
}


esp_ip4_addr_t EthernetClass::localIP(void)
{
	esp_netif_ip_info_t ip;
	memset(&ip, 0, sizeof(ip));
	if (_eth_netif == nullptr || esp_netif_get_ip_info(_eth_netif, &ip) != ESP_OK) {
		return esp_ip4_addr_t{};
	}
	return ip.ip;
}


esp_ip4_addr_t EthernetClass::subnetMask(void)
{
	esp_netif_ip_info_t ip;
	memset(&ip, 0, sizeof(ip));
	if (_eth_netif == nullptr || esp_netif_get_ip_info(_eth_netif, &ip) != ESP_OK) {
		return esp_ip4_addr_t{};
	}
	return ip.netmask;
}


esp_ip4_addr_t EthernetClass::gatewayIP(void)
{
	esp_netif_ip_info_t ip;
	memset(&ip, 0, sizeof(ip));
	if (_eth_netif == nullptr || esp_netif_get_ip_info(_eth_netif, &ip) != ESP_OK) {
		return esp_ip4_addr_t{};
	}
	return ip.gw;
}


esp_ip4_addr_t EthernetClass::dnsIP(uint8_t dns_no)
{
	if (_eth_netif == nullptr || dns_no > 2) {
		return esp_ip4_addr_t{};
	}

	const esp_netif_dns_type_t type = (dns_no == 0) ? ESP_NETIF_DNS_MAIN :
	                                  (dns_no == 1) ? ESP_NETIF_DNS_BACKUP : ESP_NETIF_DNS_FALLBACK;
	esp_netif_dns_info_t dns_info;
	memset(&dns_info, 0, sizeof(dns_info));
	if (esp_netif_get_dns_info(_eth_netif, type, &dns_info) != ESP_OK || dns_info.ip.type != ESP_IPADDR_TYPE_V4) {
		return esp_ip4_addr_t{};
	}
	return dns_info.ip.u_addr.ip4;
}


bool EthernetClass::getIpInfo(esp_netif_ip_info_t* info)
{
	if (info == nullptr) {
		return false;
	}
	memset(info, 0, sizeof(*info));
	return _eth_netif != nullptr && esp_netif_get_ip_info(_eth_netif, info) == ESP_OK;
}


std::string EthernetClass::ipToString(esp_ip4_addr_t ip)
{
	char buf[16];
	snprintf(buf, sizeof(buf), IPSTR, IP2STR(&ip));
	return std::string(buf);
}


uint8_t* EthernetClass::macAddress(uint8_t* mac)
{
	if (mac == nullptr) {
		return nullptr;
	}
	memset(mac, 0, 6);
	if (_eth_handle != nullptr) {
		esp_eth_ioctl(_eth_handle, ETH_CMD_G_MAC_ADDR, mac);
	}
	return mac;
}


std::string EthernetClass::macAddress(void)
{
	uint8_t mac[6];
	char macStr[18];
	macAddress(mac);
	snprintf(macStr, sizeof(macStr), "%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
	return std::string(macStr);
}


const char* EthernetClass::getHostname(void)
{
	const char* hostname = nullptr;
	if (_eth_netif == nullptr) {
		return nullptr;
	}
	if (esp_netif_get_hostname(_eth_netif, &hostname) != ESP_OK) {
		return nullptr;
	}
	return hostname;
}


bool EthernetClass::setHostname(const char * hostname)
{
	if (_eth_netif == nullptr || hostname == nullptr) {
		return false;
	}
	return esp_netif_set_hostname(_eth_netif, hostname) == ESP_OK;
}


/* -------------------------------------------------------------------------- */
/*  Callbacks                                                                 */
/* -------------------------------------------------------------------------- */

void EthernetClass::onConnected(eth_connected_cb_t cb)
{
	connect_event = cb;
}


void EthernetClass::onDisconnected(eth_disconnected_cb_t cb)
{
	disconnect_event = cb;
}


void EthernetClass::onGotIp(eth_got_ip_cb_t cb)
{
	got_ip_event = cb;
}


void EthernetClass::onLostIp(eth_lost_ip_cb_t cb)
{
	lost_ip_event = cb;
}


EthernetClass Ethernet;
