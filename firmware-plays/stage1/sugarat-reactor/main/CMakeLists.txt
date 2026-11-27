idf_component_register(
    SRCS "main.c" "reactor_transition.c" "lighting_tx.c" "../shared/lighting_sync.c" "${REMOTE_SOURCE}/sugarat/main/manual_control.c"
    PRIV_INCLUDE_DIRS "../shared" "${REMOTE_SOURCE}/sugarat/main"
    PRIV_REQUIRES sdgoods_board sdgoods_launcher sugarat_audio servo_radio driver esp_timer esp_wifi
    EMBED_FILES "assets/reactor_open.rgb565" "assets/reactor_close.rgb565"
                "assets/reactor_open.wav" "assets/reactor_close.wav")
target_compile_definitions(${COMPONENT_LIB} PRIVATE
    SUGARAT_LIGHTING_GROUP=${SUGARAT_LIGHTING_GROUP}
    SERVO_MANUAL_ONLY
    SERVO_PEER_MAC="${SERVO_PEER_MAC}")
