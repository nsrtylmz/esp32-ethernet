/**
 * @file    Ethernet.h
 * @brief   Arduino-style Ethernet wrapper for ESP-IDF (v5.3+).
 *
 * Hides the ESP-IDF @c esp_eth driver, @c esp_netif and event-loop boilerplate
 * behind one object (the global ::Ethernet instance, or your own EthernetClass objects).
 *
 * Supported hardware:
 * - SPI Ethernet modules: W5500, DM9051, KSZ8851SNL (@c CONFIG_ETH_USE_SPI_ETHERNET)
 * - Internal ESP32 EMAC with an RMII PHY: IP101, RTL8201, LAN87xx, DP83848,
 *   KSZ8041 / KSZ8081 (@c CONFIG_ETH_USE_ESP32_EMAC)
 *
 * @warning Only the **W5500** has been tested on real hardware. DM9051, KSZ8851SNL and the
 *          internal EMAC use the standard ESP-IDF drivers and compile, but are untested.
 *
 * IP addresses use ESP-IDF's own @c esp_ip4_addr_t (lwIP / network byte order). Useful ESP-IDF helpers:
 * @code
 * esp_ip4_addr_t ip = { .addr = ESP_IP4TOADDR(192, 168, 1, 10) };
 * esp_netif_str_to_ip4("192.168.1.10", &ip);
 * ESP_LOGI(TAG, "IP: " IPSTR, IP2STR(&ip));
 * @endcode
 *
 * @copyright SPDX-License-Identifier: MIT
 */

#ifndef ETHERNET_H_
#define ETHERNET_H_

#include <stdint.h>
#include <string>
#include <functional>

#include "sdkconfig.h"
#include "esp_err.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_eth_driver.h"
#include "esp_eth_mac.h"
#include "esp_eth_phy.h"
#include "esp_eth_netif_glue.h"

#if CONFIG_ETH_USE_SPI_ETHERNET
#include "driver/spi_master.h"
#endif

#if CONFIG_ETH_USE_ESP32_EMAC
#include "esp_eth_mac_esp.h"
#endif


/* -------------------------------------------------------------------------- */
/*  Configuration types                                                       */
/* -------------------------------------------------------------------------- */

/**
 * @brief Ethernet interface kind.
 */
typedef enum {
	SPI_ETHERNET,		/*!< External module on an SPI bus */
	INTERNAL_ETHERNET,	/*!< Internal ESP32 EMAC with an RMII PHY */
} ethernet_t;

/**
 * @brief PHY chips supported by the internal EMAC.
 */
typedef enum {
	PHY_IP101,		/*!< IC Plus IP101 */
	PHY_RTL8201,	/*!< Realtek RTL8201 */
	PHY_LAN87XX,	/*!< Microchip LAN8710 / LAN8720 */
	PHY_DP83848,	/*!< TI DP83848 */
	PHY_KSZ8041,	/*!< Microchip KSZ8041 */
	PHY_KSZ8081,	/*!< Microchip KSZ8081 */
} phy_ethernet_module_t;

/**
 * @brief SPI Ethernet modules.
 *
 * The chip must also be enabled in menuconfig
 * (Component config > Ethernet > Support SPI to Ethernet Module).
 */
typedef enum {
	SPI_MODULE_W5500 = 0,	/*!< WIZnet W5500 (default) */
	SPI_MODULE_DM9051,		/*!< Davicom DM9051 (untested) */
	SPI_MODULE_KSZ8851SNL,	/*!< Microchip KSZ8851SNL (untested) */
} spi_eth_module_type_t;


#if CONFIG_ETH_USE_SPI_ETHERNET || defined(__DOXYGEN__)

#ifndef ETH_SPI_CLOCK_MHZ
/** @brief Default SPI clock in MHz, used when spi_eth_module_config_t::spi_clock_mhz is 0. */
#define ETH_SPI_CLOCK_MHZ	16
#endif

/**
 * @brief Configuration of an SPI Ethernet module.
 *
 * Always initialise it with SPI_ETH_MODULE_DEFAULT_CONFIG() so that every field has a valid default.
 */
typedef struct {
	uint8_t spi_cs_gpio;			/*!< SPI chip select GPIO */
	int8_t int_gpio;				/*!< Interrupt GPIO, -1 = polling mode */
	uint32_t polling_ms;			/*!< Polling period when int_gpio is -1 (0 = 10 ms) */
	int8_t phy_reset_gpio;			/*!< PHY reset GPIO, -1 = not used */
	uint8_t phy_addr;				/*!< PHY address (usually 1 for SPI modules) */
	uint8_t *mac_addr;				/*!< MAC address (6 bytes, copied in init()) or nullptr = derived from the eFuse MAC */
	spi_eth_module_type_t module;	/*!< Module type, default SPI_MODULE_W5500 */
	uint8_t spi_clock_mhz;			/*!< SPI clock in MHz, 0 = ETH_SPI_CLOCK_MHZ */
} spi_eth_module_config_t;

/**
 * @brief Default initialiser for spi_eth_module_config_t (W5500, no PHY reset, MAC from eFuse).
 * @param cs   SPI chip select GPIO
 * @param irq  Interrupt GPIO, -1 = polling mode
 */
#define SPI_ETH_MODULE_DEFAULT_CONFIG(cs, irq)	\
	{											\
		.spi_cs_gpio = (cs),					\
		.int_gpio = (irq),						\
		.polling_ms = 0,						\
		.phy_reset_gpio = -1,					\
		.phy_addr = 1,							\
		.mac_addr = nullptr,					\
		.module = SPI_MODULE_W5500,				\
		.spi_clock_mhz = 0,						\
	}
#endif // CONFIG_ETH_USE_SPI_ETHERNET


#if CONFIG_ETH_USE_ESP32_EMAC || defined(__DOXYGEN__)
/**
 * @brief Configuration of the internal EMAC with an RMII PHY.
 *
 * Always initialise it with INTERNAL_ETH_DEFAULT_CONFIG().
 */
typedef struct {
	phy_ethernet_module_t phy;			/*!< PHY chip */
	int phy_addr;						/*!< PHY address, -1 = auto detect */
	int phy_reset_gpio;					/*!< PHY reset / power GPIO, -1 = not used */
	int mdc_gpio;						/*!< SMI MDC GPIO */
	int mdio_gpio;						/*!< SMI MDIO GPIO */
	emac_rmii_clock_mode_t clock_mode;	/*!< RMII clock mode: EMAC_CLK_EXT_IN or EMAC_CLK_OUT */
	int clock_gpio;						/*!< RMII clock GPIO (ESP32: 0 for input, 0 / 16 / 17 for output) */
	uint8_t *mac_addr;					/*!< MAC address (6 bytes) or nullptr = eFuse default */
} internal_eth_config_t;

/**
 * @brief Default initialiser for internal_eth_config_t.
 *
 * Matches common LAN8720 boards (WT32-ETH01, Olimex ESP32-POE): MDC 23, MDIO 18,
 * 50 MHz clock input on GPIO0, PHY address auto detect.
 */
#define INTERNAL_ETH_DEFAULT_CONFIG()		\
	{										\
		.phy = PHY_LAN87XX,					\
		.phy_addr = -1,						\
		.phy_reset_gpio = -1,				\
		.mdc_gpio = 23,						\
		.mdio_gpio = 18,					\
		.clock_mode = EMAC_CLK_EXT_IN,		\
		.clock_gpio = 0,					\
		.mac_addr = nullptr,				\
	}
#endif // CONFIG_ETH_USE_ESP32_EMAC


/** @brief Link up callback. */
typedef std::function<void(void)> eth_connected_cb_t;
/** @brief Link down callback. */
typedef std::function<void(void)> eth_disconnected_cb_t;
/** @brief IP received callback. @p ip_info is only valid during the call. */
typedef std::function<void(esp_netif_ip_info_t* ip_info)> eth_got_ip_cb_t;
/** @brief IP lost callback. */
typedef std::function<void(void)> eth_lost_ip_cb_t;


/* -------------------------------------------------------------------------- */
/*  EthernetClass                                                             */
/* -------------------------------------------------------------------------- */

/**
 * @brief One Ethernet interface (driver + esp_netif + event handling).
 *
 * Typical sequence: init() → optional setHostname() / config() → start().
 *
 * Every instance creates its own network interface (@c eth0, @c eth1, ...). The global
 * ::Ethernet object is the first one. Events are filtered per instance, so the callbacks of an
 * object are only called for its own interface.
 *
 * @note Callbacks run in the context of the default event loop task. Keep them short and
 *       do not block in them.
 */
class EthernetClass
{
public:
	/** @brief Creates an uninitialised interface. Call init() before using it. */
	EthernetClass();
	/** @brief Releases all resources (see deinit()). */
	~EthernetClass();

	EthernetClass(const EthernetClass&) = delete;
	EthernetClass& operator=(const EthernetClass&) = delete;

	/** @name Initialisation */
	///@{

#if CONFIG_ETH_USE_SPI_ETHERNET || defined(__DOXYGEN__)
	/**
	 * @brief Initialises a W5500 on an already initialised SPI bus.
	 *
	 * @param host_id         SPI host, initialised before (e.g. with spiBusInit())
	 * @param mac_addr        MAC address (6 bytes, copied) or nullptr = derived from the eFuse MAC
	 * @param cs_gpio         Chip select GPIO
	 * @param int_gpio        Interrupt GPIO, -1 = polling mode (10 ms)
	 * @param phy_reset_gpio  PHY reset GPIO, -1 = not used
	 * @param phy_addr        PHY address
	 * @return true on success. On failure everything allocated is released.
	 *         Returns false if the object is already initialised.
	 */
	bool init(spi_host_device_t host_id, uint8_t* mac_addr, uint8_t cs_gpio, int8_t int_gpio, int8_t phy_reset_gpio = -1, uint8_t phy_addr = 1);

	/**
	 * @brief Initialises any supported SPI Ethernet module.
	 *
	 * @param host_id                SPI host, initialised before (e.g. with spiBusInit())
	 * @param spi_eth_module_config  Module configuration, see SPI_ETH_MODULE_DEFAULT_CONFIG()
	 * @return true on success. On failure everything allocated is released.
	 */
	bool init(spi_host_device_t host_id, const spi_eth_module_config_t* spi_eth_module_config);

	/**
	 * @brief Initialises an SPI bus for an Ethernet module (DMA channel chosen automatically).
	 *
	 * @param host_id    SPI host (e.g. SPI2_HOST)
	 * @param miso_gpio  MISO GPIO
	 * @param mosi_gpio  MOSI GPIO
	 * @param sclk_gpio  SCLK GPIO
	 * @return true on success or if the bus is already initialised.
	 */
	static bool spiBusInit(spi_host_device_t host_id, int miso_gpio, int mosi_gpio, int sclk_gpio);
#endif

#if CONFIG_ETH_USE_ESP32_EMAC || defined(__DOXYGEN__)
	/**
	 * @brief Initialises the internal EMAC with an RMII PHY.
	 *
	 * @warning Not tested on real hardware yet.
	 *
	 * @param phy             PHY chip
	 * @param phy_addr        PHY address, -1 = auto detect
	 * @param mdc_gpio        SMI MDC GPIO
	 * @param mdio_gpio       SMI MDIO GPIO
	 * @param phy_reset_gpio  PHY reset / power GPIO, -1 = not used
	 * @param clock_mode      RMII clock mode (EMAC_CLK_EXT_IN or EMAC_CLK_OUT)
	 * @param clock_gpio      RMII clock GPIO
	 * @param mac_addr        MAC address (6 bytes) or nullptr = eFuse default
	 * @return true on success. On failure everything allocated is released.
	 */
	bool init(phy_ethernet_module_t phy, int phy_addr = -1, int mdc_gpio = 23, int mdio_gpio = 18, int phy_reset_gpio = -1,
	          emac_rmii_clock_mode_t clock_mode = EMAC_CLK_EXT_IN, int clock_gpio = 0, uint8_t* mac_addr = nullptr);

	/**
	 * @brief Initialises the internal EMAC with a configuration struct.
	 * @param config  EMAC / PHY configuration, see INTERNAL_ETH_DEFAULT_CONFIG()
	 * @return true on success. On failure everything allocated is released.
	 */
	bool init(const internal_eth_config_t* config);
#endif

	/**
	 * @brief Stops the driver and releases driver, network interface and event handlers.
	 *
	 * Safe to call on an uninitialised object. init() may be called again afterwards.
	 * The SPI bus is not freed.
	 */
	void deinit(void);

	/**
	 * @brief Starts the driver. With DHCP the client starts automatically when the link comes up.
	 * @return false if not initialised or the driver could not be started.
	 */
	bool start(void);

	/**
	 * @brief Stops the driver.
	 * @return false if not initialised or the driver could not be stopped.
	 */
	bool stop(void);
	///@}

	/** @name IP configuration */
	///@{

	/**
	 * @brief Sets a static IP configuration and stops the DHCP client.
	 *
	 * Can be called before or after start().
	 *
	 * @param local_ip  IP address; 0.0.0.0 switches back to DHCP
	 * @param gateway   Gateway
	 * @param subnet    Subnet mask
	 * @param dns       Main DNS server, 0.0.0.0 = not set
	 * @param dns2      Backup DNS server, 0.0.0.0 = not set
	 * @return false if not initialised or the address could not be set.
	 */
	bool config(esp_ip4_addr_t local_ip, esp_ip4_addr_t gateway, esp_ip4_addr_t subnet, esp_ip4_addr_t dns = {}, esp_ip4_addr_t dns2 = {});

	/**
	 * @brief Same as config(esp_ip4_addr_t, ...) with dotted-decimal strings.
	 *
	 * @code
	 * Ethernet.config("192.168.1.50", "192.168.1.1", "255.255.255.0", "8.8.8.8");
	 * @endcode
	 *
	 * @param local_ip  IP address, e.g. "192.168.1.50"
	 * @param gateway   Gateway
	 * @param subnet    Subnet mask
	 * @param dns       Main DNS server or nullptr
	 * @param dns2      Backup DNS server or nullptr
	 * @return false on an empty / invalid string or if not initialised.
	 */
	bool config(const char* local_ip, const char* gateway, const char* subnet, const char* dns = nullptr, const char* dns2 = nullptr);

	/**
	 * @brief Switches to DHCP.
	 * @return false if not initialised or the DHCP client could not be started.
	 */
	bool config(void);

	/**
	 * @brief Sets the hostname (used by DHCP). Call it before start().
	 * @param hostname  Hostname
	 * @return false if not initialised or @p hostname is nullptr.
	 */
	bool setHostname(const char * hostname);

	/**
	 * @brief Returns the hostname.
	 * @return Hostname or nullptr if not initialised.
	 */
	const char * getHostname(void);
	///@}

	/** @name Status */
	///@{

	/** @return true after a successful init(). */
	bool isInitialized(void) const { return _eth_handle != nullptr; }
	/** @return true while the driver is started. */
	bool isStarted(void) const { return _started; }
	/** @return Link state: true if the cable is plugged in and the link is negotiated. */
	bool isConnected(void) const { return _connected; }
	/** @return Same as isConnected(). */
	bool linkUp(void) const { return _connected; }
	/** @return true if an IP address is assigned (DHCP lease, or static IP with link up). */
	bool hasIP(void) const { return _has_ip; }
	/** @return true if a static IP is configured. */
	bool isStaticIP(void) const { return _useStaticIp; }

	/**
	 * @brief Negotiated link speed.
	 * @return 10 or 100 (Mbit/s), 0 if there is no link.
	 */
	uint16_t linkSpeed(void);

	/**
	 * @brief Negotiated duplex mode.
	 * @return true for full duplex, false for half duplex or no link.
	 */
	bool fullDuplex(void);

	/** @return Current IP address, 0.0.0.0 if none. */
	esp_ip4_addr_t localIP(void);
	/** @return Current subnet mask, 0.0.0.0 if none. */
	esp_ip4_addr_t subnetMask(void);
	/** @return Current gateway, 0.0.0.0 if none. */
	esp_ip4_addr_t gatewayIP(void);

	/**
	 * @brief DNS server address.
	 * @param dns_no  0 = main, 1 = backup, 2 = fallback
	 * @return DNS server, 0.0.0.0 if not set or @p dns_no is out of range.
	 */
	esp_ip4_addr_t dnsIP(uint8_t dns_no = 0);

	/**
	 * @brief IP address, netmask and gateway in one call.
	 * @param[out] info  Filled with the current values (zeroed on failure)
	 * @return false if not initialised or @p info is nullptr.
	 */
	bool getIpInfo(esp_netif_ip_info_t* info);

	/**
	 * @brief Formats an address as "a.b.c.d".
	 * @param ip  Address
	 * @return Dotted-decimal string.
	 */
	static std::string ipToString(esp_ip4_addr_t ip);

	/**
	 * @brief Reads the MAC address.
	 * @param[out] mac  Buffer of 6 bytes (all zero if not initialised)
	 * @return @p mac, or nullptr if @p mac is nullptr.
	 */
	uint8_t * macAddress(uint8_t* mac);

	/**
	 * @brief MAC address as a string.
	 * @return "AA:BB:CC:DD:EE:FF" ("00:00:00:00:00:00" if not initialised).
	 */
	std::string macAddress(void);

	/** @return Raw ESP-IDF network interface, nullptr if not initialised. */
	esp_netif_t* netif(void) const { return _eth_netif; }
	/** @return Raw ESP-IDF Ethernet driver handle, nullptr if not initialised. */
	esp_eth_handle_t handle(void) const { return _eth_handle; }
	///@}

	/** @name Callbacks
	 *  Pass nullptr to remove a callback. Callbacks run in the default event loop task.
	 */
	///@{

	/** @brief Called when the link comes up. @param cb Callback */
	void onConnected(eth_connected_cb_t cb);
	/** @brief Called when the link goes down, or when the driver is stopped while the link was up. @param cb Callback */
	void onDisconnected(eth_disconnected_cb_t cb);
	/** @brief Called when an IP is received (DHCP) or a static IP is applied after link up. @param cb Callback */
	void onGotIp(eth_got_ip_cb_t cb);
	/** @brief Called when the DHCP lease is lost. @param cb Callback */
	void onLostIp(eth_lost_ip_cb_t cb);
	///@}

private:
	int _eth_num = 0;
	esp_netif_t *_eth_netif = nullptr;
	esp_eth_handle_t _eth_handle = nullptr;
	esp_eth_mac_t *_mac = nullptr;
	esp_eth_phy_t *_phy = nullptr;
	esp_eth_netif_glue_handle_t _glue = nullptr;
	esp_event_handler_instance_t _eth_evt = nullptr;
	esp_event_handler_instance_t _ip_evt = nullptr;
	volatile bool _useStaticIp = false;
	volatile bool _connected = false;
	volatile bool _has_ip = false;
	volatile bool _started = false;

#if CONFIG_ETH_USE_SPI_ETHERNET
	bool initSpiDriver(spi_host_device_t host_id, const spi_eth_module_config_t* cfg, const uint8_t* mac_addr);
#endif
	bool attachNetif(void);
	void releaseDriver(void);
	const char* desc(void) const;

	eth_connected_cb_t connect_event = nullptr;
	eth_disconnected_cb_t disconnect_event = nullptr;
	eth_got_ip_cb_t got_ip_event = nullptr;
	eth_lost_ip_cb_t lost_ip_event = nullptr;

	static void eventHandler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data);
};


/** @brief Global instance for the first Ethernet interface (@c eth0). */
extern EthernetClass Ethernet;


/**
 * @example spi_w5500/main/main.cpp
 * W5500 over SPI with DHCP or static IP.
 *
 * @example internal_emac/main/main.cpp
 * Internal ESP32 EMAC with a LAN8720 PHY (untested on hardware).
 */


#endif /* ETHERNET_H_ */
