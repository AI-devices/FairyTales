idf_component_register(
    SRCS
        main.cpp
    INCLUDE_DIRS
        .
    REQUIRES
        driver
        espressif__esp32_p4_function_ev_board
        lvgl__lvgl
        ui
        fairytale_runtime
        media_pipeline
        spiffs
        gifdec
        nvs_flash
)

idf_component_get_property(LVGL_LIB lvgl__lvgl COMPONENT_LIB)
target_compile_options(
    ${LVGL_LIB} 
    PRIVATE 
        -DLV_LVGL_H_INCLUDE_SIMPLE
)

if(CONFIG_LV_MEM_CUSTOM)
    target_compile_options(
        ${LVGL_LIB} 
        PRIVATE 
            "-DLV_MEM_CUSTOM_ALLOC(x)=heap_caps_malloc(x, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)"
    )
endif()
