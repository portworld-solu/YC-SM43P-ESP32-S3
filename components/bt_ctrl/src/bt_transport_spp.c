#include "bt_management.h"

#include "esp_err.h"

/* Classic Bluetooth SPP server implementation will live here.
 * Placeholder for now; next step will register SPP and pipe BT-CTRL frames. */

#if defined(CONFIG_BT_ENABLED)

esp_err_t bt_transport_spp_start(void)
{
    return ESP_OK;
}

esp_err_t bt_transport_spp_stop(void)
{
    return ESP_OK;
}

#else

esp_err_t bt_transport_spp_start(void)
{
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t bt_transport_spp_stop(void)
{
    return ESP_ERR_NOT_SUPPORTED;
}

#endif

