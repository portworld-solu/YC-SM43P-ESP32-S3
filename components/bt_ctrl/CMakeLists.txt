# Open-source Bluetooth management (BLE GATTS + optional Mesh node).
idf_component_register(
    SRCS
        "src/bt_management.c"
        "src/bt_proto.c"
        "src/bt_gatt_star.c"
        "src/bt_transport_ble.c"
        "src/bt_transport_spp.c"
        "src/bt_mesh_node.c"
    INCLUDE_DIRS
        "include"
    REQUIRES
        bt
        nvs_flash
        esp_event
        freertos
)
