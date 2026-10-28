#include "bt_gatt_star.h"

#include <stdio.h>
#include <string.h>

#include "esp_timer.h"

static bt_gatt_star_link_t s_link;
static bt_gatt_star_log_entry_t s_log[BT_GATT_STAR_LOG_MAX];
static unsigned s_log_count;

static uint32_t star_now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000ULL);
}

void bt_gatt_star_set_link(bool connected, bool notify_enabled, uint16_t mtu, const uint8_t peer_bda[6])
{
    s_link.connected = connected;
    s_link.notify_enabled = notify_enabled;
    s_link.mtu = mtu;
    if (peer_bda) {
        memcpy(s_link.peer_bda, peer_bda, 6);
    } else {
        memset(s_link.peer_bda, 0, sizeof(s_link.peer_bda));
    }
}

void bt_gatt_star_get_link(bt_gatt_star_link_t *out)
{
    if (!out) {
        return;
    }
    *out = s_link;
}

static void star_log_push(bool from_central, const uint8_t *data, size_t len)
{
    if (!data || len == 0) {
        return;
    }
    if (len > BT_GATT_STAR_LOG_BYTES) {
        len = BT_GATT_STAR_LOG_BYTES;
    }
    if (s_log_count < BT_GATT_STAR_LOG_MAX) {
        s_log_count++;
    }
    memmove(&s_log[1], &s_log[0], (s_log_count - 1) * sizeof(s_log[0]));
    s_log[0].from_central = from_central;
    s_log[0].len = (uint16_t)len;
    memcpy(s_log[0].data, data, len);
    s_log[0].tick_ms = star_now_ms();
}

void bt_gatt_star_log_rx(const uint8_t *data, size_t len)
{
    star_log_push(true, data, len);
}

void bt_gatt_star_log_tx(const uint8_t *data, size_t len)
{
    star_log_push(false, data, len);
}

unsigned bt_gatt_star_log_count(void)
{
    return s_log_count;
}

bool bt_gatt_star_log_get(unsigned idx_from_newest, bt_gatt_star_log_entry_t *out)
{
    if (!out || idx_from_newest >= s_log_count) {
        return false;
    }
    *out = s_log[idx_from_newest];
    return true;
}

void bt_gatt_star_log_clear(void)
{
    s_log_count = 0;
    memset(s_log, 0, sizeof(s_log));
}

static void append_hex_line(char *buf, size_t cap, size_t *off, const bt_gatt_star_log_entry_t *e)
{
    if (!buf || !e || *off >= cap) {
        return;
    }
    const char *dir = e->from_central ? "RX←" : "TX→";
    int n = snprintf(buf + *off, cap - *off, "%s[%lu] ", dir, (unsigned long)e->tick_ms % 100000u);
    if (n < 0) {
        return;
    }
    *off += (size_t)n;
    for (uint16_t i = 0; i < e->len && *off + 3 < cap; i++) {
        n = snprintf(buf + *off, cap - *off, "%02X ", e->data[i]);
        if (n < 0) {
            break;
        }
        *off += (size_t)n;
    }
    if (*off + 1 < cap) {
        buf[(*off)++] = '\n';
        buf[*off] = '\0';
    }
}

void bt_gatt_star_format_log(char *buf, size_t buf_len, unsigned max_lines)
{
    if (!buf || buf_len == 0) {
        return;
    }
    buf[0] = '\0';
    if (s_log_count == 0) {
        snprintf(buf, buf_len, "(无收发记录)");
        return;
    }
    unsigned show = max_lines ? max_lines : 6;
    if (show > s_log_count) {
        show = s_log_count;
    }
    size_t off = 0;
    for (unsigned i = 0; i < show; i++) {
        append_hex_line(buf, buf_len, &off, &s_log[i]);
    }
}
