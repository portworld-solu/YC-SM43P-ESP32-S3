#include "board_ethernet_ch390.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sdkconfig.h"

#if defined(CONFIG_BOARD_ETH_CH390_ENABLE) && CONFIG_BOARD_ETH_CH390_ENABLE

#include "esp_eth.h"
#include "esp_eth_driver.h"
#include "esp_eth_mac_ch390.h"
#include "esp_eth_netif_glue.h"
#include "esp_eth_phy_ch390.h"
#include "esp_event.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_system.h"
#include "esp_heap_caps.h"

#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include "withthewind_board_lvgl_init.h"
#include "ui_runtime.h"
#include "wifi_bemfa_client.h"
#include "app_features.h"

static const char *TAG = "board_eth";

static esp_eth_handle_t s_eth_hdl = NULL;
static esp_netif_t *s_eth_netif = NULL;
static esp_eth_netif_glue_handle_t s_eth_glue = NULL;
static esp_event_handler_instance_t s_eth_ip_inst = NULL;
static esp_event_handler_instance_t s_eth_event_inst = NULL;
static volatile bool s_eth_traffic_paused = false;
static bool s_eth_started = false;
static bool s_eth_link_up = false;
static bool s_eth_log_muted = false;
static TickType_t s_spi_skip_log_tick = 0;
static uint32_t s_spi_xfer_err_count = 0;
/** esp_eth_start/stop 内 ioctl 仍需 SPI，须绕过 pause/低堆门禁。 */
static bool s_spi_mgmt_allow = false;

static void board_eth_stop_driver(void);
static esp_err_t board_eth_start_driver(void);
static void board_eth_soft_pause(const char *why);

/** WiFi/BLE 运行时 SPI 门禁（驱动已安装后的 xfer）。 */
#define BOARD_ETH_SPI_RUNTIME_MIN_FREE  2048u
/** esp_eth_start 门禁：CH390 SPI 需 DMA 能力内部 RAM。 */
#define BOARD_ETH_START_MIN_DMA_FREE    1536u
#define BOARD_ETH_START_MIN_DMA_LARGEST 1280u
/** CH390 init：RX task 栈(字) + RX buf + 驱动结构。 */
#define BOARD_ETH_INIT_MIN_INT_HEAP   6144u
#define BOARD_ETH_INIT_MIN_LARGEST    4096u
/** 2048 ≥ ETH_MAX_PACKET_SIZE(1522)；SPI_DMA 启用时 bus max_transfer_sz。 */
#define BOARD_ETH_SPI_MAX_XFER          2048u
/** 2048 字导致 ch390_tsk stack overflow，勿再降低。 */
#define BOARD_ETH_RX_TASK_STACK       3072

static void board_eth_log_heap(const char *where)
{
    ESP_LOGW(TAG, "%s: dma free=%lu largest=%lu int=%lu",
             where,
             (unsigned long)heap_caps_get_free_size(MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL),
             (unsigned long)heap_caps_get_largest_free_block(MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL),
             (unsigned long)esp_get_free_internal_heap_size());
}

static bool board_eth_heap_ok_for_start(void)
{
    const size_t free_bytes = heap_caps_get_free_size(MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    const size_t largest = heap_caps_get_largest_free_block(MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    return free_bytes >= BOARD_ETH_START_MIN_DMA_FREE && largest >= BOARD_ETH_START_MIN_DMA_LARGEST;
}

static bool board_eth_heap_ok_for_spi_xfer(void)
{
    const size_t free_bytes = heap_caps_get_free_size(MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    const size_t largest = heap_caps_get_largest_free_block(MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    return free_bytes >= BOARD_ETH_SPI_RUNTIME_MIN_FREE && largest >= 1024u;
}

#if defined(CONFIG_BOARD_ETH_CH390_SPI_HOST_SPI3) && CONFIG_BOARD_ETH_CH390_SPI_HOST_SPI3
#define BOARD_ETH_SPI_HOST SPI3_HOST
#else
#define BOARD_ETH_SPI_HOST SPI2_HOST
#endif

#if CONFIG_BOARD_ETH_CH390_CS_VIA_IO_EXPANDER

typedef struct {
    spi_device_handle_t hdl;
    SemaphoreHandle_t lock;
} board_ch390_spi_ctx_t;

static void *board_ch390_spi_init(const void *cfg)
{
    const eth_ch390_config_t *ch390_cfg = (const eth_ch390_config_t *)cfg;
    board_ch390_spi_ctx_t *ctx = calloc(1, sizeof(board_ch390_spi_ctx_t));
    if (ctx == NULL) {
        return NULL;
    }

    spi_device_interface_config_t devcfg = *(ch390_cfg->spi_devcfg);
    devcfg.spics_io_num = -1;

    if (spi_bus_add_device(ch390_cfg->spi_host_id, &devcfg, &ctx->hdl) != ESP_OK) {
        free(ctx);
        return NULL;
    }

    ctx->lock = xSemaphoreCreateMutex();
    if (ctx->lock == NULL) {
        spi_bus_remove_device(ctx->hdl);
        free(ctx);
        return NULL;
    }
    return ctx;
}

static esp_err_t board_ch390_spi_deinit(void *spi_ctx)
{
    board_ch390_spi_ctx_t *ctx = (board_ch390_spi_ctx_t *)spi_ctx;
    if (ctx == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    spi_bus_remove_device(ctx->hdl);
    if (ctx->lock) {
        vSemaphoreDelete(ctx->lock);
    }
    free(ctx);
    return ESP_OK;
}

static esp_err_t board_ch390_spi_xfer(void *spi_ctx, uint32_t cmd, uint32_t addr, const void *tx, void *rx,
                                      uint32_t len)
{
    board_ch390_spi_ctx_t *ctx = (board_ch390_spi_ctx_t *)spi_ctx;
    if (ctx == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (len > BOARD_ETH_SPI_MAX_XFER) {
        return ESP_ERR_INVALID_SIZE;
    }
    if (s_eth_traffic_paused && !s_spi_mgmt_allow) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!s_spi_mgmt_allow && !board_eth_heap_ok_for_spi_xfer()) {
        if (s_eth_started) {
            board_eth_soft_pause("int heap low");
        }
        return ESP_ERR_NO_MEM;
    }
    if (xSemaphoreTake(ctx->lock, pdMS_TO_TICKS(50)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    esp_err_t ret = board_io_expander_ch390_cs_set(true);
    if (ret != ESP_OK) {
        xSemaphoreGive(ctx->lock);
        return ret;
    }

    spi_transaction_t trans = {
        .cmd = cmd,
        .addr = addr,
        .length = 8 * len,
        .tx_buffer = tx,
        .rx_buffer = rx,
    };
    ret = spi_device_polling_transmit(ctx->hdl, &trans);
    if (ret != ESP_OK) {
        s_spi_xfer_err_count++;
        if (!s_eth_log_muted && (s_spi_xfer_err_count % 16u) == 1u) {
            ESP_LOGW(TAG, "CH390 SPI xfer len=%lu: %s (#%lu)", (unsigned long)len, esp_err_to_name(ret),
                     (unsigned long)s_spi_xfer_err_count);
        }
    }

    (void)board_io_expander_ch390_cs_set(false);
    xSemaphoreGive(ctx->lock);
    return ret;
}

static esp_err_t board_ch390_spi_write(void *spi_ctx, uint32_t cmd, uint32_t addr, const void *value, uint32_t len)
{
    return board_ch390_spi_xfer(spi_ctx, cmd, addr, value, NULL, len);
}

static esp_err_t board_ch390_spi_read(void *spi_ctx, uint32_t cmd, uint32_t addr, void *value, uint32_t len)
{
    return board_ch390_spi_xfer(spi_ctx, cmd, addr, NULL, value, len);
}

#endif /* CONFIG_BOARD_ETH_CH390_CS_VIA_IO_EXPANDER */

static void board_eth_got_ip_handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    (void)base;
    if (id != IP_EVENT_ETH_GOT_IP || data == NULL) {
        return;
    }

    ip_event_got_ip_t *ev = (ip_event_got_ip_t *)data;
    ESP_LOGI(TAG, "IP_EVENT_ETH_GOT_IP: " IPSTR, IP2STR(&ev->ip_info.ip));

    uint8_t mac[6] = {0};
    char mac_str[18] = {0};
    char ip_str[16] = {0};

    if (s_eth_netif != NULL && esp_netif_get_mac(s_eth_netif, mac) == ESP_OK) {
        snprintf(mac_str, sizeof(mac_str), "%02X:%02X:%02X:%02X:%02X:%02X",
                 mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    }

    snprintf(ip_str, sizeof(ip_str), IPSTR, IP2STR(&ev->ip_info.ip));
    ui_runtime_set_network_info(mac_str[0] ? mac_str : NULL, ip_str);

#if APP_FEATURE_MQTT
    (void)wifi_bemfa_client_start();
#endif
}

static void board_eth_event_handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    (void)base;
    (void)data;
    switch (id) {
    case ETHERNET_EVENT_CONNECTED:
        ESP_LOGI(TAG, "ETH link up");
        s_eth_link_up = true;
        if (s_eth_netif != NULL) {
            (void)esp_netif_dhcpc_stop(s_eth_netif);
            (void)esp_netif_dhcpc_start(s_eth_netif);
        }
        break;
    case ETHERNET_EVENT_DISCONNECTED:
        ESP_LOGW(TAG, "ETH link down");
        s_eth_link_up = false;
        break;
    default:
        break;
    }
}

static esp_err_t board_eth_ensure_gpio_isr_service(void)
{
    static bool s_gpio_isr_ready = false;
    if (s_gpio_isr_ready) {
        return ESP_OK;
    }
    esp_err_t err = gpio_install_isr_service(0);
    if (err == ESP_OK || err == ESP_ERR_INVALID_STATE) {
        s_gpio_isr_ready = true;
        return ESP_OK;
    }
    return err;
}

static void board_eth_set_ch390_log_muted(bool mute)
{
    if (mute == s_eth_log_muted) {
        return;
    }
    s_eth_log_muted = mute;
    esp_log_level_t level = mute ? ESP_LOG_NONE : ESP_LOG_ERROR;
    esp_log_level_set("ch390.mac", level);
    esp_log_level_set("ch390.phy", level);
}

static void board_eth_soft_pause(const char *why)
{
    if (s_eth_traffic_paused) {
        return;
    }
    s_eth_traffic_paused = true;
    board_eth_set_ch390_log_muted(true);
    if (why != NULL) {
        TickType_t now = xTaskGetTickCount();
        if (now - s_spi_skip_log_tick >= pdMS_TO_TICKS(30000)) {
            s_spi_skip_log_tick = now;
            ESP_LOGW(TAG, "CH390 soft pause: %s", why);
        }
    }
}

static void board_eth_stop_driver(void)
{
    if (s_eth_hdl == NULL || !s_eth_started) {
        return;
    }
    if (!board_eth_heap_ok_for_spi_xfer()) {
        ESP_LOGW(TAG, "esp_eth_stop deferred: int heap %lu B",
                 (unsigned long)esp_get_free_internal_heap_size());
        s_eth_traffic_paused = true;
        board_eth_set_ch390_log_muted(true);
        return;
    }
    s_spi_mgmt_allow = true;
    esp_err_t err = esp_eth_stop(s_eth_hdl);
    s_spi_mgmt_allow = false;
    if (err == ESP_OK) {
        s_eth_started = false;
        board_eth_set_ch390_log_muted(true);
    } else {
        ESP_LOGW(TAG, "esp_eth_stop: %s (soft pause)", esp_err_to_name(err));
        s_eth_traffic_paused = true;
        board_eth_set_ch390_log_muted(true);
    }
}

static esp_err_t board_eth_start_driver(void)
{
    if (s_eth_hdl == NULL || s_eth_started) {
        return ESP_OK;
    }
    if (!board_eth_heap_ok_for_start()) {
        return ESP_ERR_NO_MEM;
    }
    s_spi_mgmt_allow = true;
    esp_err_t err = esp_eth_start(s_eth_hdl);
    s_spi_mgmt_allow = false;
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "esp_eth_start: %s", esp_err_to_name(err));
        return err;
    }
    s_eth_started = true;
    board_eth_set_ch390_log_muted(false);
    return ESP_OK;
}

bool board_ethernet_ch390_is_ready(void)
{
    return s_eth_hdl != NULL;
}

bool board_ethernet_ch390_is_started(void)
{
    return s_eth_started && !s_eth_traffic_paused;
}

bool board_ethernet_ch390_link_up(void)
{
    return s_eth_link_up && board_ethernet_ch390_is_started();
}

bool board_ethernet_ch390_get_ip(char *buf, size_t buf_len)
{
    if (!buf || buf_len == 0) {
        return false;
    }
    buf[0] = '\0';
    if (s_eth_netif == NULL) {
        return false;
    }
    esp_netif_ip_info_t info;
    if (esp_netif_get_ip_info(s_eth_netif, &info) != ESP_OK || info.ip.addr == 0) {
        return false;
    }
    snprintf(buf, buf_len, IPSTR, IP2STR(&info.ip));
    return true;
}

bool board_ethernet_ch390_get_link_info(int *mbps, bool *full_duplex, bool *link_up)
{
    const bool up = s_eth_link_up && s_eth_hdl != NULL;
    if (link_up != NULL) {
        *link_up = up;
    }
    if (mbps != NULL) {
        *mbps = 0;
    }
    if (full_duplex != NULL) {
        *full_duplex = false;
    }
    if (s_eth_hdl == NULL) {
        return false;
    }

    eth_speed_t speed = ETH_SPEED_10M;
    eth_duplex_t duplex = ETH_DUPLEX_HALF;
    if (esp_eth_ioctl(s_eth_hdl, ETH_CMD_G_SPEED, &speed) == ESP_OK && mbps != NULL) {
        *mbps = (speed == ETH_SPEED_100M) ? 100 : 10;
    }
    if (esp_eth_ioctl(s_eth_hdl, ETH_CMD_G_DUPLEX_MODE, &duplex) == ESP_OK && full_duplex != NULL) {
        *full_duplex = (duplex == ETH_DUPLEX_FULL);
    }
    return up;
}

esp_err_t board_ethernet_ch390_init(void)
{
    if (s_eth_hdl != NULL) {
        return ESP_OK;
    }

    if (esp_get_free_internal_heap_size() < BOARD_ETH_INIT_MIN_INT_HEAP) {
        board_eth_log_heap("CH390 init skipped");
        return ESP_ERR_NO_MEM;
    }
    if (heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL) < BOARD_ETH_INIT_MIN_LARGEST) {
        board_eth_log_heap("CH390 init skipped (fragmented)");
        return ESP_ERR_NO_MEM;
    }
    const int miso = CONFIG_BOARD_ETH_CH390_SPI_MISO_PIN;
    const int mosi = CONFIG_BOARD_ETH_CH390_SPI_MOSI_PIN;
    const int sclk = CONFIG_BOARD_ETH_CH390_SPI_SCLK_PIN;
#if !CONFIG_BOARD_ETH_CH390_CS_VIA_IO_EXPANDER
    const int cs = CONFIG_BOARD_ETH_CH390_SPI_CS_PIN;
#endif

    if (miso < 0 || mosi < 0 || sclk < 0) {
        ESP_LOGW(TAG, "CH390: SPI 数据脚未配置（MISO/MOSI/SCLK 为 -1），请在 menuconfig 填写");
        return ESP_ERR_NOT_SUPPORTED;
    }

#if CONFIG_BOARD_ETH_CH390_CS_VIA_IO_EXPANDER
    if (!CONFIG_BOARD_IO_EXPANDER_ENABLE) {
        ESP_LOGE(TAG, "CH390 CS on expander requires BOARD_IO_EXPANDER_ENABLE");
        return ESP_ERR_INVALID_STATE;
    }
#else
    if (cs < 0) {
        ESP_LOGW(TAG, "CH390: SPI CS GPIO 未配置，请在 menuconfig 填写或启用 expander CS");
        return ESP_ERR_NOT_SUPPORTED;
    }
#endif

#if CONFIG_BOARD_IO_EXPANDER_ENABLE
    esp_err_t exp_err = board_io_expander_init(NULL);
    if (exp_err != ESP_OK) {
        ESP_LOGE(TAG, "IO expander init: %s", esp_err_to_name(exp_err));
        return exp_err;
    }
#if CONFIG_BOARD_ETH_CH390_RST_VIA_IO_EXPANDER
    ESP_RETURN_ON_ERROR(board_io_expander_ch390_reset_pulse(), TAG, "CH390 reset");
#endif
#endif

    spi_bus_config_t buscfg = {
        .mosi_io_num = mosi,
        .miso_io_num = miso,
        .sclk_io_num = sclk,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = BOARD_ETH_SPI_MAX_XFER,
    };

    esp_err_t err = spi_bus_initialize(BOARD_ETH_SPI_HOST, &buscfg, SPI_DMA_CH_AUTO);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "spi_bus_initialize: %s", esp_err_to_name(err));
        return err;
    }

    spi_device_interface_config_t spi_devcfg = {
        .command_bits = 1,
        .address_bits = 7,
        .mode = 0,
        .clock_speed_hz = CONFIG_BOARD_ETH_CH390_SPI_CLOCK_MHZ * 1000 * 1000,
#if !CONFIG_BOARD_ETH_CH390_CS_VIA_IO_EXPANDER
        .spics_io_num = cs,
#else
        .spics_io_num = -1,
#endif
        .queue_size = 16,
    };

    eth_ch390_config_t ch390_cfg = ETH_CH390_DEFAULT_CONFIG(BOARD_ETH_SPI_HOST, &spi_devcfg);
    ch390_cfg.int_gpio_num = CONFIG_BOARD_ETH_CH390_INT_PIN;
#if CONFIG_BOARD_ETH_CH390_INT_PIN < 0
    ch390_cfg.poll_period_ms = CONFIG_BOARD_ETH_CH390_POLL_PERIOD_MS;
#else
    ESP_RETURN_ON_ERROR(board_eth_ensure_gpio_isr_service(), TAG, "GPIO ISR service");
#endif

#if CONFIG_BOARD_ETH_CH390_CS_VIA_IO_EXPANDER
    ch390_cfg.custom_spi_driver.config = &ch390_cfg;
    ch390_cfg.custom_spi_driver.init = board_ch390_spi_init;
    ch390_cfg.custom_spi_driver.deinit = board_ch390_spi_deinit;
    ch390_cfg.custom_spi_driver.read = board_ch390_spi_read;
    ch390_cfg.custom_spi_driver.write = board_ch390_spi_write;
#endif

    eth_mac_config_t mac_cfg = ETH_MAC_DEFAULT_CONFIG();
    mac_cfg.rx_task_stack_size = BOARD_ETH_RX_TASK_STACK;
    mac_cfg.rx_task_prio = tskIDLE_PRIORITY + 1;

    esp_eth_mac_t *mac = esp_eth_mac_new_ch390(&ch390_cfg, &mac_cfg);
    if (mac == NULL) {
        board_eth_log_heap("esp_eth_mac_new_ch390 failed");
        (void)spi_bus_free(BOARD_ETH_SPI_HOST);
        return ESP_FAIL;
    }

    eth_phy_config_t phy_cfg = ETH_PHY_DEFAULT_CONFIG();
    esp_eth_phy_t *phy = esp_eth_phy_new_ch390(&phy_cfg);
    if (phy == NULL) {
        ESP_LOGE(TAG, "esp_eth_phy_new_ch390 failed");
        (void)mac->del(mac);
        (void)spi_bus_free(BOARD_ETH_SPI_HOST);
        return ESP_FAIL;
    }

    esp_eth_config_t eth_cfg = ETH_DEFAULT_CONFIG(mac, phy);
    err = esp_eth_driver_install(&eth_cfg, &s_eth_hdl);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_eth_driver_install: %s", esp_err_to_name(err));
        (void)phy->del(phy);
        (void)mac->del(mac);
        (void)spi_bus_free(BOARD_ETH_SPI_HOST);
        return err;
    }

    esp_netif_config_t netif_cfg = ESP_NETIF_DEFAULT_ETH();
    s_eth_netif = esp_netif_new(&netif_cfg);
    if (s_eth_netif == NULL) {
        ESP_LOGE(TAG, "esp_netif_new failed");
        (void)esp_eth_driver_uninstall(s_eth_hdl);
        s_eth_hdl = NULL;
        (void)spi_bus_free(BOARD_ETH_SPI_HOST);
        return ESP_FAIL;
    }

    s_eth_glue = esp_eth_new_netif_glue(s_eth_hdl);
    if (s_eth_glue == NULL) {
        ESP_LOGE(TAG, "esp_eth_new_netif_glue failed");
        esp_netif_destroy(s_eth_netif);
        s_eth_netif = NULL;
        (void)esp_eth_driver_uninstall(s_eth_hdl);
        s_eth_hdl = NULL;
        (void)spi_bus_free(BOARD_ETH_SPI_HOST);
        return ESP_FAIL;
    }

    err = esp_netif_attach(s_eth_netif, s_eth_glue);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_netif_attach: %s", esp_err_to_name(err));
        (void)esp_eth_del_netif_glue(s_eth_glue);
        s_eth_glue = NULL;
        esp_netif_destroy(s_eth_netif);
        s_eth_netif = NULL;
        (void)esp_eth_driver_uninstall(s_eth_hdl);
        s_eth_hdl = NULL;
        (void)spi_bus_free(BOARD_ETH_SPI_HOST);
        return err;
    }

    err = esp_event_handler_instance_register(IP_EVENT, IP_EVENT_ETH_GOT_IP, board_eth_got_ip_handler, NULL,
                                            &s_eth_ip_inst);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "register ETH GOT_IP: %s", esp_err_to_name(err));
        esp_netif_destroy(s_eth_netif);
        s_eth_netif = NULL;
        (void)esp_eth_del_netif_glue(s_eth_glue);
        s_eth_glue = NULL;
        (void)esp_eth_driver_uninstall(s_eth_hdl);
        s_eth_hdl = NULL;
        (void)spi_bus_free(BOARD_ETH_SPI_HOST);
        return err;
    }

    err = esp_event_handler_instance_register(ETH_EVENT, ESP_EVENT_ANY_ID, board_eth_event_handler, NULL,
                                            &s_eth_event_inst);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "register ETH_EVENT: %s", esp_err_to_name(err));
    }

#if CONFIG_FRAME_ENABLE_UI_RUNTIME
    err = board_eth_start_driver();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_eth_start: %s", esp_err_to_name(err));
        (void)esp_eth_stop(s_eth_hdl);
        s_eth_started = false;
        esp_event_handler_instance_unregister(IP_EVENT, IP_EVENT_ETH_GOT_IP, s_eth_ip_inst);
        s_eth_ip_inst = NULL;
        esp_netif_destroy(s_eth_netif);
        s_eth_netif = NULL;
        (void)esp_eth_del_netif_glue(s_eth_glue);
        s_eth_glue = NULL;
        (void)esp_eth_driver_uninstall(s_eth_hdl);
        s_eth_hdl = NULL;
        (void)spi_bus_free(BOARD_ETH_SPI_HOST);
        return err;
    }
#else
    /* 测试台：仅安装驱动；WiFi/BLE 启动后再 esp_eth_start，避免共存 SPI 刷屏/崩溃 */
    board_eth_soft_pause("deferred until Tab");
#endif

    ESP_LOGI(TAG, "CH390 OK (SPI host=%d, INT=%d, CS=%s, RST=%s, dma=auto max_xfer=%u%s)",
             (int)BOARD_ETH_SPI_HOST, CONFIG_BOARD_ETH_CH390_INT_PIN,
#if CONFIG_BOARD_ETH_CH390_CS_VIA_IO_EXPANDER
             "expander P0.5",
#else
             "GPIO",
#endif
#if CONFIG_BOARD_ETH_CH390_RST_VIA_IO_EXPANDER
             "expander P0.7",
#else
             "none",
#endif
             (unsigned)BOARD_ETH_SPI_MAX_XFER,
#if CONFIG_FRAME_ENABLE_UI_RUNTIME
             ", running");
#else
             ", deferred start)");
#endif
    return ESP_OK;
}

void board_ethernet_ch390_suspend_for_coexist(void)
{
    if (s_eth_hdl == NULL || !s_eth_started) {
        return;
    }
    if (board_eth_heap_ok_for_spi_xfer()) {
        board_eth_stop_driver();
    }
    board_eth_soft_pause("WiFi/BLE coexist");
}

void board_ethernet_ch390_set_traffic_paused(bool paused)
{
    if (paused == s_eth_traffic_paused) {
        return;
    }
    s_eth_traffic_paused = paused;
    board_eth_set_ch390_log_muted(paused);
}

static TickType_t s_try_start_log_tick;

esp_err_t board_ethernet_ch390_try_start(void)
{
    if (s_eth_hdl == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (s_eth_started && !s_eth_traffic_paused) {
        return ESP_OK;
    }
    if (!board_eth_heap_ok_for_start()) {
        const TickType_t now = xTaskGetTickCount();
        if (now - s_try_start_log_tick >= pdMS_TO_TICKS(30000)) {
            s_try_start_log_tick = now;
            board_eth_log_heap("try_start blocked");
        }
        return ESP_ERR_NO_MEM;
    }
    s_eth_traffic_paused = false;
    board_eth_set_ch390_log_muted(false);

    const esp_err_t err = board_eth_start_driver();
    if (err == ESP_OK) {
        if (s_eth_netif != NULL) {
            (void)esp_netif_dhcpc_start(s_eth_netif);
        }
        ESP_LOGI(TAG, "CH390 started");
    } else {
        board_eth_log_heap("try_start failed");
    }
    return err;
}

#else /* CH390 disabled */

esp_err_t board_ethernet_ch390_init(void)
{
    return ESP_OK;
}

void board_ethernet_ch390_set_traffic_paused(bool paused)
{
    (void)paused;
}

esp_err_t board_ethernet_ch390_try_start(void)
{
    return ESP_ERR_NOT_SUPPORTED;
}

void board_ethernet_ch390_suspend_for_coexist(void)
{
}

bool board_ethernet_ch390_is_ready(void)
{
    return false;
}

bool board_ethernet_ch390_is_started(void)
{
    return false;
}

bool board_ethernet_ch390_link_up(void)
{
    return false;
}

bool board_ethernet_ch390_get_ip(char *buf, size_t buf_len)
{
    if (buf && buf_len) {
        buf[0] = '\0';
    }
    return false;
}

bool board_ethernet_ch390_get_link_info(int *mbps, bool *full_duplex, bool *link_up)
{
    if (mbps) {
        *mbps = 0;
    }
    if (full_duplex) {
        *full_duplex = false;
    }
    if (link_up) {
        *link_up = false;
    }
    return false;
}

#endif
