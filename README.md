# ESP32 Ethernet (ESP-IDF component)

**English** | [Türkçe](README.tr.md)

A small Arduino-style C++ wrapper around the ESP-IDF Ethernet driver (`esp_eth` + `esp_netif`).
It hides the driver / netif / event-loop boilerplate behind a single `Ethernet` object.

- **SPI Ethernet modules:** W5500, DM9051, KSZ8851SNL
- **Internal ESP32 EMAC (RMII):** IP101, RTL8201, LAN87xx (LAN8720), DP83848, KSZ8041 / KSZ8081
- DHCP or static IP
- Link / IP callbacks (`onConnected`, `onDisconnected`, `onGotIp`, `onLostIp`)
- Several interfaces at the same time (each `EthernetClass` instance gets its own netif `eth0`, `eth1`, ...)
- Uses only ESP-IDF types (`esp_ip4_addr_t`, `esp_netif_ip_info_t`), no external dependencies

**Requirements:** ESP-IDF **v5.3 or newer** (tested with v5.5).

> **Tested hardware:** the library has only been tested on hardware with a **W5500** (SPI).
> DM9051, KSZ8851SNL and the internal EMAC are implemented with the standard ESP-IDF drivers and compile,
> but have **not been tested** on real hardware.

---

## Installation

Clone the repository into the `components` folder of your project:

```powershell
cd your_project/components
git clone https://github.com/nsrtylmz/esp32-ethernet.git Ethernet
```

or add it as an extra component directory in the project `CMakeLists.txt`:

```cmake
set(EXTRA_COMPONENT_DIRS "path/to/Ethernet")
```

If your `main` component lists its requirements explicitly, add the component name (the folder name, e.g. `Ethernet`):

```cmake
idf_component_register(SRCS "main.cpp" REQUIRES Ethernet)
```

### menuconfig

Enable the Ethernet hardware you use in **Component config → Ethernet**:

| Hardware | Option |
|---|---|
| W5500 | `CONFIG_ETH_USE_SPI_ETHERNET` + `CONFIG_ETH_SPI_ETHERNET_W5500` |
| DM9051 | `CONFIG_ETH_USE_SPI_ETHERNET` + `CONFIG_ETH_SPI_ETHERNET_DM9051` |
| KSZ8851SNL | `CONFIG_ETH_USE_SPI_ETHERNET` + `CONFIG_ETH_SPI_ETHERNET_KSZ8851SNL` |
| Internal EMAC (ESP32) | `CONFIG_ETH_USE_ESP32_EMAC` |

---

## Quick start – W5500

```cpp
#include "esp_log.h"
#include "Ethernet.h"

// Set these to your wiring
#define PIN_MISO  ...
#define PIN_MOSI  ...
#define PIN_SCLK  ...
#define PIN_CS    ...
#define PIN_INT   ...   // -1 = polling mode
#define PIN_RST   ...   // -1 = not connected

extern "C" void app_main(void)
{
    // SPI bus: MISO, MOSI, SCLK
    EthernetClass::spiBusInit(SPI2_HOST, PIN_MISO, PIN_MOSI, PIN_SCLK);

    Ethernet.onGotIp([](esp_netif_ip_info_t* info) {
        ESP_LOGI("app", "IP: " IPSTR, IP2STR(&info->ip));
    });

    // host, mac (nullptr = from eFuse), CS, INT, PHY reset
    Ethernet.init(SPI2_HOST, nullptr, PIN_CS, PIN_INT, PIN_RST);
    Ethernet.setHostname("my-device");
    Ethernet.start();
}
```

### Static IP

```cpp
Ethernet.init(...);

// with strings
Ethernet.config("192.168.1.50", "192.168.1.1", "255.255.255.0", "8.8.8.8");

// or with ESP-IDF types
esp_ip4_addr_t ip   = { .addr = ESP_IP4TOADDR(192, 168, 1, 50) };
esp_ip4_addr_t gw   = { .addr = ESP_IP4TOADDR(192, 168, 1, 1) };
esp_ip4_addr_t mask = { .addr = ESP_IP4TOADDR(255, 255, 255, 0) };
Ethernet.config(ip, gw, mask);

Ethernet.start();

// back to DHCP
Ethernet.config();
```

### Advanced SPI configuration

```cpp
uint8_t mac[6] = {0x02, 0x00, 0x00, 0x12, 0x34, 0x56};

spi_eth_module_config_t cfg = SPI_ETH_MODULE_DEFAULT_CONFIG(PIN_CS, PIN_INT);
cfg.module        = SPI_MODULE_DM9051;   // W5500 (default), DM9051 / KSZ8851SNL (untested)
cfg.spi_clock_mhz = 20;                  // 0 = ETH_SPI_CLOCK_MHZ (16)
cfg.phy_reset_gpio = PIN_RST;
cfg.mac_addr      = mac;                 // copied during init()

Ethernet.init(SPI2_HOST, &cfg);
```

> Always initialise `spi_eth_module_config_t` with `SPI_ETH_MODULE_DEFAULT_CONFIG()`, so new fields get valid defaults.

## Quick start – internal EMAC (LAN8720, WT32-ETH01, Olimex ESP32-POE ...)

> Not tested on real hardware yet.

```cpp
internal_eth_config_t cfg = INTERNAL_ETH_DEFAULT_CONFIG();
cfg.phy            = PHY_LAN87XX;
cfg.phy_addr       = 1;               // -1 = auto detect
cfg.phy_reset_gpio = 16;              // board specific (power / reset pin)
cfg.mdc_gpio       = 23;
cfg.mdio_gpio      = 18;
cfg.clock_mode     = EMAC_CLK_EXT_IN; // 50 MHz clock from PHY on GPIO0
cfg.clock_gpio     = 0;

Ethernet.init(&cfg);
Ethernet.start();
```

## Several interfaces

```cpp
EthernetClass eth1;               // the global "Ethernet" is eth0, this one becomes eth1
eth1.init(SPI3_HOST, nullptr, 15, 2);
eth1.start();
```

The MAC address of the n-th interface is the eFuse Ethernet MAC + n, the route priority decreases with n.
Events are filtered per instance, so every object only receives the callbacks of its own interface.

---

## API reference

### Initialisation

| Method | Description |
|---|---|
| `static bool spiBusInit(host, miso, mosi, sclk)` | Initialises an SPI bus (an already initialised bus is accepted) |
| `bool init(host, mac, cs, int_gpio, phy_reset = -1, phy_addr = 1)` | W5500 on an initialised SPI bus |
| `bool init(host, const spi_eth_module_config_t*)` | Any SPI module with full configuration |
| `bool init(phy, phy_addr, mdc, mdio, phy_reset, clock_mode, clock_gpio, mac)` | Internal EMAC |
| `bool init(const internal_eth_config_t*)` | Internal EMAC with a config struct |
| `void deinit()` | Stops and releases driver, netif and event handlers. `init()` can be called again. |
| `bool start()` / `bool stop()` | Start / stop the driver |

`init()` returns `false` (and releases everything it allocated) when it fails, e.g. missing SPI bus, module
not enabled in menuconfig, or no response from the chip. Calling `init()` twice without `deinit()` returns `false`.

### IP configuration

| Method | Description |
|---|---|
| `bool config(esp_ip4_addr_t ip, gw, mask, dns = {}, dns2 = {})` | Static IP. `ip == 0.0.0.0` switches to DHCP. |
| `bool config(const char* ip, gw, mask, dns = nullptr, dns2 = nullptr)` | Same with strings. Returns `false` on an invalid string. |
| `bool config()` | DHCP |
| `bool setHostname(const char*)` / `const char* getHostname()` | Hostname (set it before `start()` so DHCP uses it) |

### Status

| Method | Description |
|---|---|
| `bool isInitialized()` / `isStarted()` | Driver state |
| `bool isConnected()` / `linkUp()` | Link (cable) state |
| `bool hasIP()` | An IP address is assigned |
| `bool isStaticIP()` | Static IP is configured |
| `uint16_t linkSpeed()` | 10 / 100 Mbit/s, 0 if no link |
| `bool fullDuplex()` | Duplex mode |
| `esp_ip4_addr_t localIP()` / `subnetMask()` / `gatewayIP()` | Current addresses (0.0.0.0 if none) |
| `esp_ip4_addr_t dnsIP(n = 0)` | DNS server (0 = main, 1 = backup, 2 = fallback) |
| `bool getIpInfo(esp_netif_ip_info_t*)` | IP / mask / gateway in one call |
| `static std::string ipToString(esp_ip4_addr_t)` | `"a.b.c.d"` |
| `uint8_t* macAddress(uint8_t mac[6])` / `std::string macAddress()` | MAC address |
| `esp_netif_t* netif()` / `esp_eth_handle_t handle()` | Raw ESP-IDF handles for advanced use |

Printing addresses with ESP-IDF macros:

```cpp
esp_ip4_addr_t ip = Ethernet.localIP();
ESP_LOGI(TAG, "IP: " IPSTR, IP2STR(&ip));
```

### Callbacks

| Method | Called when |
|---|---|
| `onConnected(std::function<void()>)` | Link up |
| `onDisconnected(std::function<void()>)` | Link down, or driver stopped while the link was up |
| `onGotIp(std::function<void(esp_netif_ip_info_t*)>)` | IP received (DHCP) or static IP applied on link up |
| `onLostIp(std::function<void()>)` | DHCP lease lost |

Callbacks run in the context of the **default event loop task**: keep them short and do not block in them.
Pass `nullptr` to remove a callback.

---

## Examples

| Folder | Description |
|---|---|
| [`examples/spi_w5500`](examples/spi_w5500) | W5500 over SPI, DHCP or static IP, status output |
| [`examples/internal_emac`](examples/internal_emac) | ESP32 internal EMAC with LAN8720 (untested) |

```powershell
cd examples/spi_w5500
idf.py set-target esp32
idf.py build flash monitor
```

## Tests

[`test_apps/ethernet_test`](test_apps/ethernet_test) is a Unity test application.

- `[api]` tests need **no Ethernet hardware** and run automatically after boot
  (behaviour of an uninitialised object, invalid configuration, cleanup / memory leak on failed `init()`).
- `[hw]` tests need a **W5500** and a network with a DHCP server (DHCP, static IP, init/deinit cycles, custom MAC).
  Set the pins in `menuconfig → Ethernet Test Configuration`, then start them from the Unity menu by typing `[hw]`.

```powershell
cd test_apps/ethernet_test
idf.py set-target esp32
idf.py menuconfig     # Ethernet Test Configuration: pins, static IP for the test
idf.py build flash monitor
```

---

## Known issues

- **W5500:** when the link goes down or the driver is stopped, ESP-IDF logs errors like
  `emac_w5500_del_mac_filter: IPv6 multicast is always filtered in by W5500` /
  `Failed to remove multicast filter for IPv6`. They come from the ESP-IDF W5500 driver (lwIP removes its
  IPv6 multicast filter, which the W5500 cannot do) and are harmless. Hiding them would require
  silencing the tags `w5500.mac`, `esp_eth`, `esp_eth.netif.netif_glue` and `esp_netif_lwip`, which also hides
  real errors, so this is not recommended.

---

## API documentation (Doxygen)

All public types and methods in `Ethernet.h` are documented with Doxygen comments.
To build the HTML documentation locally:

```powershell
doxygen Doxyfile
start docs/html/index.html
```

---

## License

[MIT](LICENSE)
