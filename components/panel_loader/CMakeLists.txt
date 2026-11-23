idf_component_register(
    SRCS "panel_loader.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES esp_http_server app_update esp_app_format esp_partition nvs_flash json esp_system freertos
    EMBED_TXTFILES "ui/index.html"
)
