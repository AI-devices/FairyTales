#include <stdio.h>

#include "esp_err.h"
#include "esp_check.h"
#include "esp_log.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "lvgl.h"

#include "bsp/display.h"
#include "bsp/esp-bsp.h"
#include "esp_lvgl_port.h"
#include "fairytale_runtime.h"
#include "media_storage.h"
#include "nvs_flash.h"
#include "story_coordinator.h"
#include "ui.h"
#include "ui_dual_gif_screen.h"

static const char *TAG = "main";
static lv_obj_t *s_boot_progress_bar = nullptr;
static lv_obj_t *s_boot_progress_label = nullptr;

namespace {
constexpr int kLvglTaskStackSize = 24576;
constexpr int kLvglTaskPriority = 6;
constexpr int kLvglTaskCore = 1;
}

static bool fs_ready_cb(lv_fs_drv_t *)
{
    return true;
}

static void *fs_open_cb(lv_fs_drv_t *, const char *path, lv_fs_mode_t mode)
{
    char real[256];
    snprintf(real, sizeof(real), "%s/%s", BSP_SPIFFS_MOUNT_POINT, path);

    const char *file_mode = (mode & LV_FS_MODE_WR) ? ((mode & LV_FS_MODE_RD) ? "rb+" : "wb") : "rb";
    return fopen(real, file_mode);
}

static lv_fs_res_t fs_close_cb(lv_fs_drv_t *, void *f)
{
    return f && fclose(static_cast<FILE *>(f)) == 0 ? LV_FS_RES_OK : LV_FS_RES_FS_ERR;
}

static lv_fs_res_t fs_read_cb(lv_fs_drv_t *, void *f, void *buf, uint32_t btr, uint32_t *br)
{
    if (!f) {
        return LV_FS_RES_FS_ERR;
    }

    size_t n = fread(buf, 1, btr, static_cast<FILE *>(f));
    if (br) {
        *br = static_cast<uint32_t>(n);
    }
    return ferror(static_cast<FILE *>(f)) ? LV_FS_RES_FS_ERR : LV_FS_RES_OK;
}

static lv_fs_res_t fs_seek_cb(lv_fs_drv_t *, void *f, uint32_t pos, lv_fs_whence_t whence)
{
    int origin = SEEK_SET;

    if (!f) {
        return LV_FS_RES_FS_ERR;
    }

    if (whence == LV_FS_SEEK_CUR) {
        origin = SEEK_CUR;
    } else if (whence == LV_FS_SEEK_END) {
        origin = SEEK_END;
    }

    return fseek(static_cast<FILE *>(f), static_cast<long>(pos), origin) == 0 ? LV_FS_RES_OK : LV_FS_RES_FS_ERR;
}

static lv_fs_res_t fs_tell_cb(lv_fs_drv_t *, void *f, uint32_t *pos)
{
    long offset = 0;

    if (!f || !pos) {
        return LV_FS_RES_FS_ERR;
    }

    offset = ftell(static_cast<FILE *>(f));
    if (offset < 0) {
        return LV_FS_RES_FS_ERR;
    }

    *pos = static_cast<uint32_t>(offset);
    return LV_FS_RES_OK;
}

static void lvgl_register_drive_S(void)
{
    lv_fs_drv_t drv;

    lv_fs_drv_init(&drv);
    drv.letter = 'S';
    drv.ready_cb = fs_ready_cb;
    drv.open_cb = fs_open_cb;
    drv.close_cb = fs_close_cb;
    drv.read_cb = fs_read_cb;
    drv.seek_cb = fs_seek_cb;
    drv.tell_cb = fs_tell_cb;
    lv_fs_drv_register(&drv);
}

static esp_err_t init_nvs(void)
{
    esp_err_t err = nvs_flash_init();

    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_RETURN_ON_ERROR(nvs_flash_erase(), TAG, "nvs erase failed");
        err = nvs_flash_init();
    }

    return err;
}

static void clear_boot_screen(void)
{
    if (!lvgl_port_lock(1000)) {
        ESP_LOGW(TAG, "unable to lock LVGL while preparing boot screen");
        return;
    }

    lv_obj_t *screen = lv_scr_act();
    lv_obj_clean(screen);
    lv_obj_set_style_bg_color(screen, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, LV_PART_MAIN);

    s_boot_progress_label = lv_label_create(screen);
    lv_label_set_text(s_boot_progress_label, "Loading story...");
    lv_obj_set_style_text_color(s_boot_progress_label, lv_color_hex(0xFAF190), LV_PART_MAIN);
    lv_obj_set_style_text_font(s_boot_progress_label, &lv_font_montserrat_24, LV_PART_MAIN);
    lv_obj_align(s_boot_progress_label, LV_ALIGN_CENTER, 0, -34);

    s_boot_progress_bar = lv_bar_create(screen);
    lv_obj_set_size(s_boot_progress_bar, 560, 24);
    lv_obj_align(s_boot_progress_bar, LV_ALIGN_CENTER, 0, 18);
    lv_bar_set_range(s_boot_progress_bar, 0, 100);
    lv_bar_set_value(s_boot_progress_bar, 0, LV_ANIM_OFF);
    lv_obj_set_style_radius(s_boot_progress_bar, 12, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_boot_progress_bar, lv_color_hex(0x4A2818), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_boot_progress_bar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(s_boot_progress_bar, 12, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(s_boot_progress_bar, lv_color_hex(0xD8B84C), LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(s_boot_progress_bar, LV_OPA_COVER, LV_PART_INDICATOR);
    lvgl_port_unlock();
}

static void update_boot_progress(uint8_t percent, const char *, void *)
{
    if (percent > 100U) {
        percent = 100U;
    }
    if (!lvgl_port_lock(1000)) {
        ESP_LOGW(TAG, "unable to lock LVGL while updating boot progress");
        return;
    }

    if (s_boot_progress_bar) {
        lv_bar_set_value(s_boot_progress_bar, percent, LV_ANIM_OFF);
    }
    lvgl_port_unlock();
}

static void show_blocking_sdcard_warning(void)
{
    if (!lvgl_port_lock(1000)) {
        ESP_LOGW(TAG, "unable to lock LVGL while showing microSD warning");
        return;
    }

    lv_obj_t *screen = lv_scr_act();
    lv_obj_clean(screen);
    lv_obj_set_style_bg_color(screen, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, LV_PART_MAIN);

    lv_obj_t *panel = lv_obj_create(screen);
    lv_obj_set_size(panel, 760, 260);
    lv_obj_center(panel);
    lv_obj_set_style_bg_color(panel, lv_color_hex(0x101010), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_color(panel, lv_palette_main(LV_PALETTE_ORANGE), LV_PART_MAIN);
    lv_obj_set_style_border_width(panel, 3, LV_PART_MAIN);
    lv_obj_set_style_radius(panel, 16, LV_PART_MAIN);
    lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title = lv_label_create(panel);
    lv_label_set_text(title, "microSD card not detected");
    lv_obj_set_style_text_color(title, lv_palette_main(LV_PALETTE_ORANGE), LV_PART_MAIN);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_32, LV_PART_MAIN);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 24);

    lv_obj_t *body = lv_label_create(panel);
    lv_obj_set_width(body, 680);
    lv_label_set_long_mode(body, LV_LABEL_LONG_WRAP);
    lv_label_set_text(body, "Insert a microSD card into the SD card slot and reboot the panel.");
    lv_obj_set_style_text_align(body, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_color(body, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_text_font(body, &lv_font_montserrat_24, LV_PART_MAIN);
    lv_obj_align(body, LV_ALIGN_CENTER, 0, 28);

    lvgl_port_unlock();
}

static void halt_on_sdcard_warning(void)
{
    show_blocking_sdcard_warning();
    for (;;) {
        vTaskDelay(portMAX_DELAY);
    }
}

extern "C" void app_main(void)
{
    bsp_display_cfg_t display_cfg = {
        .lvgl_port_cfg = ESP_LVGL_PORT_INIT_CONFIG(),
        .buffer_size = BSP_LCD_H_RES * BSP_LCD_V_RES,
        .double_buffer = BSP_LCD_DRAW_BUFF_DOUBLE,
        .flags = {
            .buff_dma = false,
            .buff_spiram = true,
            .sw_rotate = false,
        },
    };
    esp_err_t err = ESP_OK;

    esp_log_level_set("*", ESP_LOG_INFO);
    esp_log_level_set("GT911_TOUCH", ESP_LOG_WARN);

    display_cfg.lvgl_port_cfg.task_stack = kLvglTaskStackSize;
    display_cfg.lvgl_port_cfg.task_priority = kLvglTaskPriority;
    /* Keep LVGL and the local audio worker on separate cores. */
    display_cfg.lvgl_port_cfg.task_affinity = kLvglTaskCore;
    display_cfg.lvgl_port_cfg.task_max_sleep_ms = 10;

    if (!bsp_display_start_with_config(&display_cfg)) {
        ESP_LOGE(TAG, "display init failed");
        return;
    }

    bsp_display_backlight_on();
    clear_boot_screen();

    ESP_LOGI(TAG, "mounting SPIFFS");
    err = bsp_spiffs_mount();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "SPIFFS mount failed: %s", esp_err_to_name(err));
        return;
    }
    lvgl_register_drive_S();
    update_boot_progress(3U, "Loading storage", nullptr);

    ESP_LOGI(TAG, "initializing NVS");
    err = init_nvs();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "NVS init failed: %s", esp_err_to_name(err));
        return;
    }
    update_boot_progress(5U, "Loading settings", nullptr);

    ESP_LOGI(TAG, "initializing runtime");
    err = fairytale_runtime_init(update_boot_progress, nullptr);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "runtime init failed: %s", esp_err_to_name(err));
        return;
    }

    ESP_LOGI(TAG, "mounting microSD");
    err = media_storage_mount_sdcard();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "microSD mount failed: %s", esp_err_to_name(err));
        halt_on_sdcard_warning();
    }
    update_boot_progress(50U, "Loading story media", nullptr);

    ui_dual_gif_screen_preload_initial_media_with_progress(update_boot_progress, nullptr);

    if (!lvgl_port_lock(1000)) {
        ESP_LOGE(TAG, "LVGL lock failed");
        return;
    }
    story_coordinator_init();
    ui_init();
    s_boot_progress_bar = nullptr;
    s_boot_progress_label = nullptr;
    lvgl_port_unlock();

    ESP_LOGI(TAG, "Fairy Tale app started");
}
