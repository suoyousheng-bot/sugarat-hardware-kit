# Copyright (c) 2026 SDGOODS. SPDX-License-Identifier: Apache-2.0
# Modified by Sugarat: a named, version-pinned product application.
cmake_minimum_required(VERSION 3.16)
include("${CMAKE_CURRENT_LIST_DIR}/kit-components.cmake" OPTIONAL)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(sugarat_wolf)

foreach(render_component IN ITEMS lvgl sdgoods_board)
    idf_component_get_property(render_library ${render_component} COMPONENT_LIB)
    target_compile_options(${render_library} PRIVATE -O2)
endforeach()
