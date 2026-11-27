idf_component_register(
    SRCS "main.c" "lighting_art.c" "lighting_selection.c" "light_pixels.c" "${LIGHTING_SHARED}/lighting_sync.c"
    PRIV_INCLUDE_DIRS "${LIGHTING_SHARED}"
    PRIV_REQUIRES sdgoods_board sdgoods_launcher driver esp_timer esp_wifi esp_netif esp_event
    EMBED_FILES "assets/core_dark.rgb565" "assets/core_bright.rgb565" "assets/core_phase.bin"
                "assets/ring_dark.rgb565" "assets/ring_bright.rgb565" "assets/ring_phase.bin"
                "assets/arcs_dark.rgb565" "assets/arcs_bright.rgb565" "assets/arcs_phase.bin")
target_compile_definitions(${COMPONENT_LIB} PRIVATE SUGARAT_LIGHTING_GROUP=${SUGARAT_LIGHTING_GROUP})
