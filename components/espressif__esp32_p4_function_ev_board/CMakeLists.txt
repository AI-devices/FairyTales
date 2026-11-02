# set(ELECROW_DIR ./elecrow_ui/images)
file(GLOB_RECURSE ELECROW_C_SRCS ./elecrow_ui/images/*.c)

idf_component_register(
    SRCS "esp32_p4_function_ev_board.c" "my_codec.c" "elecrow_ui/elecrow_ui.c" "bsp_stc8h1kxx.c" ${ELECROW_C_SRCS} #"./elecrow_ui/images/*.c" 
    INCLUDE_DIRS "include" "elecrow_ui/include"
    PRIV_INCLUDE_DIRS "priv_include"
    REQUIRES driver
    PRIV_REQUIRES esp_lcd usb spiffs fatfs
)
