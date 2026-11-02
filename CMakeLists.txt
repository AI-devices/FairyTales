# The following lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.10)

# The configuration is to prevent compilation errors on the Windows system.
set(CMAKE_OBJECT_PATH_MAX 512)

set(COMPONENTS "main" "espressif__esp32_p4_function_ev_board" "bsp_extra")
include($ENV{IDF_PATH}/tools/cmake/project.cmake)

set(CXX_COMPILE_OPTIONS
    -Wno-deprecated-enum-enum-conversion
    -Wno-write-strings
)

add_compile_options(
    -Wno-format
    -Wno-unused-variable
    -Wno-missing-field-initializers
    -Wno-unused-but-set-variable
    -Wno-cast-function-type
    -Wno-ignored-qualifiers
    -Wno-deprecated-declarations
)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-deprecated-enum-enum-conversion")

project(crowpanel_p4_fairytale VERSION 0.0.0)

set(SPIFFS_SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/spiffs_root")
set(SPIFFS_STAGE_DIR "${CMAKE_BINARY_DIR}/spiffs_stage")

if(EXISTS "${SPIFFS_SRC_DIR}")
    file(GLOB_RECURSE SPIFFS_SOURCE_FILES CONFIGURE_DEPENDS "${SPIFFS_SRC_DIR}/*")

    add_custom_target(
        spiffs_stage ALL
        DEPENDS ${SPIFFS_SOURCE_FILES}
        COMMAND ${CMAKE_COMMAND} -E rm -rf "${SPIFFS_STAGE_DIR}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${SPIFFS_STAGE_DIR}"
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${SPIFFS_SRC_DIR}" "${SPIFFS_STAGE_DIR}"
        COMMAND ${CMAKE_COMMAND} -E rm -f "${SPIFFS_STAGE_DIR}/.DS_Store"
        COMMAND ${CMAKE_COMMAND} -E rm -f "${SPIFFS_STAGE_DIR}/assets/.DS_Store"
        COMMAND ${CMAKE_COMMAND} -E touch "${SPIFFS_STAGE_DIR}/.prepared"
        BYPRODUCTS "${SPIFFS_STAGE_DIR}/.prepared"
        VERBATIM
    )

    spiffs_create_partition_image(spiffs "${SPIFFS_STAGE_DIR}" FLASH_IN_PROJECT)
    add_dependencies(spiffs_spiffs_bin spiffs_stage)
endif()
