cmake_minimum_required(VERSION 3.16)

set(SDGOODS_COMPONENTS "${CMAKE_CURRENT_LIST_DIR}/../sugarat-lunch/components")
set(LIGHTING_SHARED "${CMAKE_CURRENT_LIST_DIR}/../sugarat-reactor/shared")
set(EXTRA_COMPONENT_DIRS
    "${SDGOODS_COMPONENTS}/lvgl"
    "${SDGOODS_COMPONENTS}/jpegenc"
    "${SDGOODS_COMPONENTS}/sdgoods_board"
    "${SDGOODS_COMPONENTS}/sdgoods_launcher")
include("${LIGHTING_SHARED}/lighting_group.cmake")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
idf_build_set_property(MINIMAL_BUILD ON)
project(sugarat_reactor_side)
