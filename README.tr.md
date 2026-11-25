# ESP32 Ethernet (ESP-IDF bileşeni)

[English](README.md) | **Türkçe**

ESP-IDF Ethernet sürücüsü (`esp_eth` + `esp_netif`) için Arduino tarzı, küçük bir C++ sarmalayıcı.
Sürücü / netif / olay döngüsü kurulumunu tek bir `Ethernet` nesnesinin arkasına gizler.

- **SPI Ethernet modülleri:** W5500, DM9051, KSZ8851SNL
- **ESP32 dahili EMAC (RMII):** IP101, RTL8201, LAN87xx (LAN8720), DP83848, KSZ8041 / KSZ8081
- DHCP veya statik IP
- Bağlantı / IP callback'leri (`onConnected`, `onDisconnected`, `onGotIp`, `onLostIp`)
- Aynı anda birden fazla arayüz (her `EthernetClass` nesnesi kendi netif'ini alır: `eth0`, `eth1`, ...)
- Yalnızca ESP-IDF tipleri kullanılır (`esp_ip4_addr_t`, `esp_netif_ip_info_t`), harici bağımlılık yoktur

**Gereksinimler:** ESP-IDF **v5.3 veya üzeri** (v5.5 ile test edildi).

> **Test edilen donanım:** kütüphane gerçek donanımda yalnızca **W5500** (SPI) ile test edilmiştir.
> DM9051, KSZ8851SNL ve dahili EMAC standart ESP-IDF sürücüleriyle yazılmıştır ve derlenir,
> ancak gerçek donanımda **test edilmemiştir**.

---

## Kurulum

Depoyu projenizin `components` klasörüne klonlayın:

```powershell
cd your_project/components
git clone https://github.com/nsrtylmz/esp32-ethernet.git Ethernet
```

veya projenin `CMakeLists.txt` dosyasına ek bileşen dizini olarak ekleyin:

```cmake
set(EXTRA_COMPONENT_DIRS "path/to/Ethernet")
```

`main` bileşeniniz bağımlılıklarını açıkça listeliyorsa bileşen adını (klasör adı, ör. `Ethernet`) ekleyin:

```cmake
idf_component_register(SRCS "main.cpp" REQUIRES Ethernet)
```

### menuconfig

Kullandığınız Ethernet donanımını **Component config → Ethernet** altında etkinleştirin:

| Donanım | Seçenek |
|---|---|
| W5500 | `CONFIG_ETH_USE_SPI_ETHERNET` + `CONFIG_ETH_SPI_ETHERNET_W5500` |
| DM9051 | `CONFIG_ETH_USE_SPI_ETHERNET` + `CONFIG_ETH_SPI_ETHERNET_DM9051` |
| KSZ8851SNL | `CONFIG_ETH_USE_SPI_ETHERNET` + `CONFIG_ETH_SPI_ETHERNET_KSZ8851SNL` |
| Dahili EMAC (ESP32) | `CONFIG_ETH_USE_ESP32_EMAC` |

---

## Hızlı başlangıç – W5500

```cpp
#include "esp_log.h"
#include "Ethernet.h"

// Kendi bağlantınıza göre ayarlayın
#define PIN_MISO  ...
#define PIN_MOSI  ...
#define PIN_SCLK  ...
#define PIN_CS    ...
#define PIN_INT   ...   // -1 = polling modu
#define PIN_RST   ...   // -1 = bağlı değil

extern "C" void app_main(void)
{
    // SPI veriyolu: MISO, MOSI, SCLK
    EthernetClass::spiBusInit(SPI2_HOST, PIN_MISO, PIN_MOSI, PIN_SCLK);

    Ethernet.onGotIp([](esp_netif_ip_info_t* info) {
        ESP_LOGI("app", "IP: " IPSTR, IP2STR(&info->ip));
    });

    // host, mac (nullptr = eFuse'tan), CS, INT, PHY reset
    Ethernet.init(SPI2_HOST, nullptr, PIN_CS, PIN_INT, PIN_RST);
    Ethernet.setHostname("my-device");
    Ethernet.start();
}
```

### Statik IP

```cpp
Ethernet.init(...);

// string ile
Ethernet.config("192.168.1.50", "192.168.1.1", "255.255.255.0", "8.8.8.8");

// veya ESP-IDF tipleriyle
esp_ip4_addr_t ip   = { .addr = ESP_IP4TOADDR(192, 168, 1, 50) };
esp_ip4_addr_t gw   = { .addr = ESP_IP4TOADDR(192, 168, 1, 1) };
esp_ip4_addr_t mask = { .addr = ESP_IP4TOADDR(255, 255, 255, 0) };
Ethernet.config(ip, gw, mask);

Ethernet.start();

// DHCP'ye geri dön
Ethernet.config();
```

### Gelişmiş SPI yapılandırması

```cpp
uint8_t mac[6] = {0x02, 0x00, 0x00, 0x12, 0x34, 0x56};

spi_eth_module_config_t cfg = SPI_ETH_MODULE_DEFAULT_CONFIG(PIN_CS, PIN_INT);
cfg.module        = SPI_MODULE_DM9051;   // W5500 (varsayılan), DM9051 / KSZ8851SNL (test edilmedi)
cfg.spi_clock_mhz = 20;                  // 0 = ETH_SPI_CLOCK_MHZ (16)
cfg.phy_reset_gpio = PIN_RST;
cfg.mac_addr      = mac;                 // init() sırasında kopyalanır

Ethernet.init(SPI2_HOST, &cfg);
```

> `spi_eth_module_config_t` yapısını her zaman `SPI_ETH_MODULE_DEFAULT_CONFIG()` ile başlatın; böylece yeni alanlar geçerli varsayılan değerler alır.

## Hızlı başlangıç – dahili EMAC (LAN8720, WT32-ETH01, Olimex ESP32-POE ...)

> Henüz gerçek donanımda test edilmedi.

```cpp
internal_eth_config_t cfg = INTERNAL_ETH_DEFAULT_CONFIG();
cfg.phy            = PHY_LAN87XX;
cfg.phy_addr       = 1;               // -1 = otomatik algıla
cfg.phy_reset_gpio = 16;              // karta özel (güç / reset pini)
cfg.mdc_gpio       = 23;
cfg.mdio_gpio      = 18;
cfg.clock_mode     = EMAC_CLK_EXT_IN; // PHY'den GPIO0'a gelen 50 MHz saat
cfg.clock_gpio     = 0;

Ethernet.init(&cfg);
Ethernet.start();
```

## Birden fazla arayüz

```cpp
EthernetClass eth1;               // global "Ethernet" eth0'dır, bu nesne eth1 olur
eth1.init(SPI3_HOST, nullptr, 15, 2);
eth1.start();
```

n'inci arayüzün MAC adresi eFuse Ethernet MAC + n'dir, rota önceliği n arttıkça azalır.
Olaylar nesne başına filtrelenir; her nesne yalnızca kendi arayüzünün callback'lerini alır.

---

## API referansı

### Başlatma

| Metot | Açıklama |
|---|---|
| `static bool spiBusInit(host, miso, mosi, sclk)` | SPI veriyolunu başlatır (zaten başlatılmış veriyolu kabul edilir) |
| `bool init(host, mac, cs, int_gpio, phy_reset = -1, phy_addr = 1)` | Başlatılmış SPI veriyolunda W5500 |
| `bool init(host, const spi_eth_module_config_t*)` | Tam yapılandırmayla herhangi bir SPI modülü |
| `bool init(phy, phy_addr, mdc, mdio, phy_reset, clock_mode, clock_gpio, mac)` | Dahili EMAC |
| `bool init(const internal_eth_config_t*)` | Yapılandırma struct'ı ile dahili EMAC |
| `void deinit()` | Sürücüyü durdurur; sürücü, netif ve olay işleyicilerini serbest bırakır. `init()` tekrar çağrılabilir. |
| `bool start()` / `bool stop()` | Sürücüyü başlat / durdur |

`init()` başarısız olduğunda `false` döner (ve ayırdığı her şeyi serbest bırakır); ör. SPI veriyolu yok,
modül menuconfig'te etkin değil veya çip yanıt vermiyor. `deinit()` çağırmadan `init()`'i iki kez çağırmak `false` döner.

### IP yapılandırması

| Metot | Açıklama |
|---|---|
| `bool config(esp_ip4_addr_t ip, gw, mask, dns = {}, dns2 = {})` | Statik IP. `ip == 0.0.0.0` DHCP'ye geçer. |
| `bool config(const char* ip, gw, mask, dns = nullptr, dns2 = nullptr)` | Aynısı, string ile. Geçersiz string'de `false` döner. |
| `bool config()` | DHCP |
| `bool setHostname(const char*)` / `const char* getHostname()` | Hostname (DHCP'nin kullanması için `start()`'tan önce ayarlayın) |

### Durum

| Metot | Açıklama |
|---|---|
| `bool isInitialized()` / `isStarted()` | Sürücü durumu |
| `bool isConnected()` / `linkUp()` | Bağlantı (kablo) durumu |
| `bool hasIP()` | IP adresi atanmış |
| `bool isStaticIP()` | Statik IP yapılandırılmış |
| `uint16_t linkSpeed()` | 10 / 100 Mbit/s, bağlantı yoksa 0 |
| `bool fullDuplex()` | Duplex modu |
| `esp_ip4_addr_t localIP()` / `subnetMask()` / `gatewayIP()` | Geçerli adresler (yoksa 0.0.0.0) |
| `esp_ip4_addr_t dnsIP(n = 0)` | DNS sunucusu (0 = ana, 1 = yedek, 2 = fallback) |
| `bool getIpInfo(esp_netif_ip_info_t*)` | IP / maske / ağ geçidi tek çağrıda |
| `static std::string ipToString(esp_ip4_addr_t)` | `"a.b.c.d"` |
| `uint8_t* macAddress(uint8_t mac[6])` / `std::string macAddress()` | MAC adresi |
| `esp_netif_t* netif()` / `esp_eth_handle_t handle()` | İleri kullanım için ham ESP-IDF handle'ları |

Adresleri ESP-IDF makrolarıyla yazdırma:

```cpp
esp_ip4_addr_t ip = Ethernet.localIP();
ESP_LOGI(TAG, "IP: " IPSTR, IP2STR(&ip));
```

### Callback'ler

| Metot | Ne zaman çağrılır |
|---|---|
| `onConnected(std::function<void()>)` | Bağlantı kuruldu |
| `onDisconnected(std::function<void()>)` | Bağlantı koptu veya bağlantı varken sürücü durduruldu |
| `onGotIp(std::function<void(esp_netif_ip_info_t*)>)` | IP alındı (DHCP) veya bağlantı kurulunca statik IP uygulandı |
| `onLostIp(std::function<void()>)` | DHCP kirası kaybedildi |

Callback'ler **varsayılan olay döngüsü görevinin** (default event loop task) bağlamında çalışır: kısa tutun ve içlerinde bloklamayın.
Bir callback'i kaldırmak için `nullptr` verin.

---

## Örnekler

| Klasör | Açıklama |
|---|---|
| [`examples/spi_w5500`](examples/spi_w5500) | SPI üzerinden W5500, DHCP veya statik IP, durum çıktısı |
| [`examples/internal_emac`](examples/internal_emac) | LAN8720 ile ESP32 dahili EMAC (test edilmedi) |

```powershell
cd examples/spi_w5500
idf.py set-target esp32
idf.py build flash monitor
```

## Testler

[`test_apps/ethernet_test`](test_apps/ethernet_test) bir Unity test uygulamasıdır.

- `[api]` testleri **Ethernet donanımı gerektirmez** ve açılıştan sonra otomatik çalışır
  (başlatılmamış nesnenin davranışı, geçersiz yapılandırma, başarısız `init()` sonrası temizlik / bellek sızıntısı).
- `[hw]` testleri bir **W5500** ve DHCP sunucusu olan bir ağ gerektirir (DHCP, statik IP, init/deinit döngüleri, özel MAC).
  Pinleri `menuconfig → Ethernet Test Configuration` altında ayarlayın, ardından Unity menüsünde `[hw]` yazarak başlatın.

```powershell
cd test_apps/ethernet_test
idf.py set-target esp32
idf.py menuconfig     # Ethernet Test Configuration: pinler, test için statik IP
idf.py build flash monitor
```

---

## Bilinen sorunlar

- **W5500:** bağlantı koptuğunda veya sürücü durdurulduğunda ESP-IDF şu tür hatalar loglar:
  `emac_w5500_del_mac_filter: IPv6 multicast is always filtered in by W5500` /
  `Failed to remove multicast filter for IPv6`. Bunlar ESP-IDF W5500 sürücüsünden gelir (lwIP, W5500'ün
  yapamadığı IPv6 multicast filtresini kaldırmaya çalışır) ve zararsızdır. Gizlemek için `w5500.mac`, `esp_eth`,
  `esp_eth.netif.netif_glue` ve `esp_netif_lwip` etiketlerinin susturulması gerekir; bu gerçek hataları da gizleyeceği
  için önerilmez.

---

## API dokümantasyonu (Doxygen)

`Ethernet.h` içindeki tüm public tipler ve metotlar Doxygen yorumlarıyla belgelenmiştir.
HTML dokümantasyonu yerelde üretmek için:

```powershell
doxygen Doxyfile
start docs/html/index.html
```

---

## Lisans

[MIT](LICENSE)
