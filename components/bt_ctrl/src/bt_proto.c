#include "bt_proto.h"

#include <string.h>

#include "esp_check.h"

static const char *TAG = "bt_proto";

uint16_t btctrl_crc16_update(uint16_t crc, const uint8_t *data, size_t len)
{
    /* CRC-16/CCITT-FALSE (0x1021, init 0xFFFF) */
    for (size_t i = 0; i < len; i++) {
        crc ^= (uint16_t)data[i] << 8;
        for (int b = 0; b < 8; b++) {
            if (crc & 0x8000) crc = (uint16_t)((crc << 1) ^ 0x1021);
            else crc <<= 1;
        }
    }
    return crc;
}

uint16_t btctrl_crc16(const uint8_t *data, size_t len)
{
    return btctrl_crc16_update(0xFFFF, data, len);
}

esp_err_t btctrl_parse_view(const uint8_t *buf, size_t buf_len, btctrl_frame_view_t *out)
{
    ESP_RETURN_ON_FALSE(buf != NULL && out != NULL, ESP_ERR_INVALID_ARG, TAG, "null");
    ESP_RETURN_ON_FALSE(buf_len >= sizeof(btctrl_hdr_t), ESP_ERR_INVALID_SIZE, TAG, "short");

    btctrl_hdr_t hdr;
    memcpy(&hdr, buf, sizeof(hdr));

    ESP_RETURN_ON_FALSE(hdr.magic[0] == BTCTRL_MAGIC0 && hdr.magic[1] == BTCTRL_MAGIC1,
                        ESP_ERR_INVALID_RESPONSE, TAG, "bad magic");
    ESP_RETURN_ON_FALSE(hdr.ver == BTCTRL_VERSION, ESP_ERR_NOT_SUPPORTED, TAG, "bad ver");

    const size_t total = sizeof(btctrl_hdr_t) + (size_t)hdr.len;
    ESP_RETURN_ON_FALSE(buf_len >= total, ESP_ERR_INVALID_SIZE, TAG, "len");

    /* Validate CRC over (header with crc16=0) + body */
    btctrl_hdr_t hdr_crc = hdr;
    hdr_crc.crc16 = 0;

    uint16_t crc = 0xFFFF;
    crc = btctrl_crc16_update(crc, (const uint8_t *)&hdr_crc, sizeof(hdr_crc));
    crc = btctrl_crc16_update(crc, buf + sizeof(btctrl_hdr_t), hdr.len);

    /* Allow crc16==0 during early bring-up or when transport adds its own integrity. */
    if (hdr.crc16 != 0 && crc != hdr.crc16) {
        return ESP_ERR_INVALID_CRC;
    }

    out->hdr = hdr;
    out->body = buf + sizeof(btctrl_hdr_t);
    return ESP_OK;
}

esp_err_t btctrl_build(uint8_t msg_type, uint16_t seq, uint16_t flags,
                       const uint8_t *body, uint16_t body_len,
                       uint8_t *out, size_t out_cap, size_t *out_len)
{
    ESP_RETURN_ON_FALSE(out != NULL && out_len != NULL, ESP_ERR_INVALID_ARG, TAG, "null");
    ESP_RETURN_ON_FALSE(out_cap >= sizeof(btctrl_hdr_t) + body_len, ESP_ERR_NO_MEM, TAG, "cap");

    btctrl_hdr_t hdr = {
        .magic = { BTCTRL_MAGIC0, BTCTRL_MAGIC1 },
        .ver = BTCTRL_VERSION,
        .msg_type = msg_type,
        .seq = seq,
        .flags = flags,
        .len = body_len,
        .crc16 = 0,
        .reserved = 0,
    };

    memcpy(out, &hdr, sizeof(hdr));
    if (body_len && body != NULL) {
        memcpy(out + sizeof(hdr), body, body_len);
    }

    btctrl_hdr_t hdr_crc = hdr;
    hdr_crc.crc16 = 0;
    uint16_t crc = 0xFFFF;
    crc = btctrl_crc16_update(crc, (const uint8_t *)&hdr_crc, sizeof(hdr_crc));
    crc = btctrl_crc16_update(crc, out + sizeof(hdr), body_len);

    ((btctrl_hdr_t *)out)->crc16 = crc;
    *out_len = sizeof(btctrl_hdr_t) + body_len;
    return ESP_OK;
}

