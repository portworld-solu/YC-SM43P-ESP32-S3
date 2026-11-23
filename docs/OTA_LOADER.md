# Panel OTA Loader - optional local firmware management

**Device:** YC-SM43P ESP32-S3, 16 MB flash and 8 MB PSRAM
**Implementation:** ESP-IDF 5.5.1, `esp_http_server`, native ESP-IDF OTA APIs
**Contributor:** Orquitech S.A.S.
**Validation:** physical PoE/CH390D laboratory test, November 22, 2026

## What this contributes

- Embedded web interface served by the ESP32-S3, with firmware upload and upload progress.
- `GET /api/info` reports application version, running OTA slot, next slot, maximum binary size and readiness.
- `POST /api/firmware` accepts a raw ESP-IDF **application binary**, streams it to the inactive OTA slot, validates it with `esp_ota_end()`, selects it with `esp_ota_set_boot_partition()`, and **schedules a reboot before sending the HTTP response**.
- `GET /api/mqtt` and `POST /api/mqtt` read/write optional MQTT host, port, username, panel ID and password in an application-selected NVS namespace. The secret itself is not returned by GET.
- `POST /api/reboot` provides a local reboot command.
- Self-contained HTML stored in the firmware (`EMBED_TXTFILES`). The existing SPIFFS storage is untouched by application OTA.

The loader **does not** provide an MQTT client, device discovery, Wi-Fi provisioning, authentication, signed-image verification, cloud hosting or automatic rollback. A host application must use saved NVS values when starting its own MQTT client. All features are explicitly subject to the firmware build configuration and network isolation policy.

## Opt-in integration

The patch kit adds the component at `components/panel_loader/`, a CMake dependency in `main/CMakeLists.txt`, and a guarded initialization in `main/main.c` after `wifi_management_foundation_init()`.

To enable the feature in ESP-IDF 5.5.1:

```bash
idf.py menuconfig
```

Enable **Component config > Panel OTA Loader > Enable local HTTP OTA loader (trusted LAN only)**, then build a device-specific image with `idf.py build`.

This feature is **off by default** in this proposed upstream integration, because TCP/80 accepts arbitrary firmware and MQTT setting changes from any host that can reach it. A factory-fitted, plug-and-play version requires Portworld to decide on the secure default network exposure and to install the loader before shipping.

## Quick operational verification

1. Boot a device running a firmware image with the enabled loader and functional Ethernet/DHCP.
2. Open the assigned panel IP in a browser. The main page shows the current and next OTA slots.
3. Select only a compatible ESP-IDF **application** `.bin` and confirm installation.
4. Wait for the upload to finish, validation to succeed, and the panel to reboot automatically. It should return to the network without cycling PoE.
5. Reload `/api/info`: verify that `running` and `next` swapped. Do not rely on project version text alone unless `PROJECT_VER` was updated.
6. If MQTT is used, enter the broker details and save. Reboot; confirm NVS persistence and verify the host MQTT client consumes the configured namespace.

**Do not flash** a merged binary, `bootloader.bin`, `partition-table.bin` or `storage.bin` through this UI. The browser interface does not repartition, update SPIFFS or install the second-stage bootloader.

## Bench evidence (one test device)

| Test | Observed result |
| --- | --- |
| USB initial ESP32-S3 recognition | Intermittent enumeration on the tested unit; repeated cable/connection attempts required; root cause not established |
| Flash backup before changes | 16,777,216 bytes copied and verified in private laboratory storage |
| Native USB initial app flash to `ota_1` | PASS, esptool verified written image |
| PoE, LCD/GT911, CH390 Ethernet | PASS for startup, touch responsiveness and network reachability |
| Browser GET `/` and `/api/info` | PASS, HTTP 200 and OTA slot metadata |
| MQTT configuration stored across reboot | PASS, NVS retained host/user/secret-presence flag |
| Browser OTA from `ota_1` to `ota_0` | PARTIAL - image booted in `ota_0` after manual PoE restart; automatic reconnect was not confirmed |
| Browser OTA from `ota_0` to `ota_1` | PASS - verified firmware, automatic reboot, browser returned, `/api/info` confirmed `ota_1` |
| Automatic rollback of bad firmware | NOT TESTED |
| Power-loss during flash or prolonged soak test | NOT TESTED |
| Factory firmware equivalence after OTA | NOT TESTED; original factory image preserved privately |

These results establish a **laboratory proof of concept**, not production readiness for every YC-SM43P hardware revision or network environment.

## Security and operational limitations

- The web service is **HTTP without authentication**. Anyone with access can upload a new application, reboot the device or overwrite MQTT settings. Restrict to a dedicated management LAN with enforced firewall rules and disable for untrusted deployments.
- The MQTT password is accepted as HTTP request content and saved to NVS. It is not protected against an attacker already on the permitted LAN. `GET /api/mqtt` returns only `has_password`, not the secret.
- The loader uses the ESP-IDF OTA image validation mechanism; **it does not verify a signature against an authorized publisher**. Do not treat image format checks as publisher authenticity.
- No verified rollback demonstration is available. The original factory bootloader was retained during the bench test; its rollback configuration was not established.
- The browser front end may briefly lose connectivity during reboot. The backend schedules reboot before attempting the HTTP success response, so successful flash commits do not depend on the browser staying connected.
- Firmware version names such as `5117709-dirty` are Git-derived project versions and may remain unchanged across local builds. Prefer an explicit `PROJECT_VER`/release tag for shipped binaries.

## Reference documents

- [ESP-IDF native OTA](https://docs.espressif.com/projects/esp-idf/en/v5.5.1/esp32s3/api-reference/system/ota.html) - `esp_ota_begin`, `esp_ota_write`, `esp_ota_end`, `esp_ota_set_boot_partition`.
- [ESP-IDF HTTPS OTA](https://docs.espressif.com/projects/esp-idf/en/v5.5.1/esp32s3/api-reference/system/esp_https_ota.html) - complementary device-initiated HTTPS download and OTA, not used by this browser upload handler.
- [ESP-IDF HTTP server](https://docs.espressif.com/projects/esp-idf/en/v5.5.1/esp32s3/api-reference/protocols/esp_http_server.html) - lightweight server used for the local management UI.
