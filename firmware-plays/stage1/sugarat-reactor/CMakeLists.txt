cmake_minimum_required(VERSION 3.16)

set(REMOTE_SOURCE "${CMAKE_CURRENT_LIST_DIR}/../../../esp32c3supermini")
set(SDGOODS_COMPONENTS "${CMAKE_CURRENT_LIST_DIR}/../sugarat-lunch/components")
set(EXTRA_COMPONENT_DIRS
    "${SDGOODS_COMPONENTS}/lvgl"
    "${SDGOODS_COMPONENTS}/jpegenc"
    "${SDGOODS_COMPONENTS}/sdgoods_board"
    "${SDGOODS_COMPONENTS}/sdgoods_launcher"
    "${SDGOODS_COMPONENTS}/sugarat_audio"
    "${REMOTE_SOURCE}/common/servo_radio")
set(SERVO_PEER_MAC "EC:D6:1B:B6:09:30" CACHE STRING "ESP32-C3 Wi-Fi STA MAC")

include(shared/lighting_group.cmake)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
idf_build_set_property(MINIMAL_BUILD ON)
project(sugarat_reactor)
