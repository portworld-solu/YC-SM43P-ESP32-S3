# Open-source hub model (rooms / widgets / NVS persistence).
idf_component_register(
    SRCS "src/hub_model.c"
    INCLUDE_DIRS "include"
    REQUIRES nvs_flash
)
