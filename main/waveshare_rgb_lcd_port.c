/*
 * Display bring-up for the supported 800x480 RGB boards. One binary drives
 * both: the board is picked at boot (or forced via menuconfig).
 *
 *  - Waveshare ESP32-S3-Touch-LCD-7: CH422G IO expander on I2C handles the
 *    backlight and touch reset. The expander's presence is also how the
 *    board is detected.
 *  - Guition JC8048W550 (5"): same panel type on different pins, backlight
 *    on a plain GPIO, GT911 reset on a GPIO, touch mirrored in both axes.
 *    Pin map extracted from KamKubicki/flyRadarEsp32 (working device).
 *
 * Adapted from Waveshare's 08_lvgl_Porting demo (CC0-1.0).
 */
#include "waveshare_rgb_lcd_port.h"

static const char *TAG = "lcd_port";

typedef struct {
    const char *name;
    int de, vsync, hsync, pclk;
    int data[16];               /* B0..B4, G0..G5, R0..R4 */
    int i2c_sda, i2c_scl;
    bool has_ch422g;            /* backlight + touch reset via expander */
    int bl_gpio;                /* when !has_ch422g */
    int tp_rst_gpio;            /* when !has_ch422g */
    bool tp_mirror;             /* GT911 reports mirrored coordinates */
} board_cfg_t;

static const board_cfg_t k_waveshare = {
    /* One PCB family: the 4.3", 5" and 7" Waveshare 800x480 boards share
     * every pin, the expander and the timings (verified against the
     * official demos of both the 7 and the 4.3 repos), so this single
     * entry covers them all. */
    .name = "Waveshare ESP32-S3-Touch-LCD (4.3/5/7)",
    .de = 5, .vsync = 3, .hsync = 46, .pclk = 7,
    .data = { 14, 38, 18, 17, 10,       /* B0..B4 */
              39, 0, 45, 48, 47, 21,    /* G0..G5 */
              1, 2, 42, 41, 40 },       /* R0..R4 */
    .i2c_sda = 8, .i2c_scl = 9,
    .has_ch422g = true,
    .bl_gpio = -1,
    .tp_rst_gpio = -1,
    .tp_mirror = false,
};

static const board_cfg_t k_guition = {
    .name = "Guition JC8048W550",
    .de = 40, .vsync = 41, .hsync = 39, .pclk = 42,
    .data = { 8, 3, 46, 9, 1,           /* B0..B4 */
              5, 6, 7, 15, 16, 4,       /* G0..G5 */
              45, 48, 47, 21, 14 },     /* R0..R4 */
    .i2c_sda = 19, .i2c_scl = 20,
    .has_ch422g = false,
    .bl_gpio = 2,
    .tp_rst_gpio = 38,
    .tp_mirror = false,
};

static const board_cfg_t *s_board = &k_waveshare;
static esp_lcd_panel_handle_t s_panel;

const char *waveshare_lcd_board_name(void)
{
    return s_board->name;
}

void waveshare_lcd_get_res(int *w, int *h)
{
    *w = EXAMPLE_LCD_H_RES;
    *h = EXAMPLE_LCD_V_RES;
}

void *waveshare_lcd_get_fb(void)
{
    if (s_panel == NULL) {
        return NULL;
    }
    void *fb0 = NULL, *fb1 = NULL;
    if (esp_lcd_rgb_panel_get_frame_buffer(s_panel, 2, &fb0, &fb1) != ESP_OK) {
        return NULL;
    }
    return fb0;
}

IRAM_ATTR static bool rgb_lcd_on_vsync_event(esp_lcd_panel_handle_t panel,
                                             const esp_lcd_rgb_panel_event_data_t *edata,
                                             void *user_ctx)
{
    return lvgl_port_notify_rgb_vsync();
}

static esp_err_t i2c_master_init(int sda, int scl)
{
    i2c_config_t i2c_conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = sda,
        .scl_io_num = scl,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = I2C_MASTER_FREQ_HZ,
    };
    i2c_param_config(I2C_MASTER_NUM, &i2c_conf);
    return i2c_driver_install(I2C_MASTER_NUM, i2c_conf.mode, 0, 0, 0);
}

/* CH422G I2C expander: raw writes, 0x24 = mode reg (0x01 -> push-pull out),
 * 0x38 = EXIO0-7 output byte. EXIO1=TP_RST, EXIO2=backlight, EXIO3=LCD_RST. */
static esp_err_t ch422g_write(uint8_t addr, uint8_t val)
{
    return i2c_master_write_to_device(I2C_MASTER_NUM, addr, &val, 1,
                                      I2C_MASTER_TIMEOUT_MS / portTICK_PERIOD_MS);
}

/* Board detection: only the Waveshare has the CH422G expander, and probing
 * an I2C address is harmless on the Guition (those pins are RGB data lines,
 * still idle at this point; I2C is open-drain). */
static void board_detect(void)
{
#if CONFIG_CANFLIGHT_BOARD_WAVESHARE_7
    s_board = &k_waveshare;
    i2c_master_init(s_board->i2c_sda, s_board->i2c_scl);
#elif CONFIG_CANFLIGHT_BOARD_GUITION_JC8048W550
    s_board = &k_guition;
    i2c_master_init(s_board->i2c_sda, s_board->i2c_scl);
#else
    i2c_master_init(k_waveshare.i2c_sda, k_waveshare.i2c_scl);
    if (ch422g_write(0x24, 0x01) == ESP_OK) {
        s_board = &k_waveshare;
    } else {
        s_board = &k_guition;
        i2c_driver_delete(I2C_MASTER_NUM);
        i2c_master_init(s_board->i2c_sda, s_board->i2c_scl);
    }
#endif
    ESP_LOGI(TAG, "board: %s", s_board->name);
}

static void touch_reset(void)
{
    if (s_board->has_ch422g) {
        gpio_config_t io_conf = {
            .intr_type = GPIO_INTR_DISABLE,
            .pin_bit_mask = 1ULL << GPIO_TOUCH_INT,
            .mode = GPIO_MODE_OUTPUT,
        };
        gpio_config(&io_conf);

        ch422g_write(0x24, 0x01);
        ch422g_write(0x38, 0x2C);           /* TP_RST low */
        esp_rom_delay_us(100 * 1000);
        gpio_set_level(GPIO_TOUCH_INT, 0);  /* INT low during reset -> addr 0x5D */
        esp_rom_delay_us(100 * 1000);
        ch422g_write(0x38, 0x2E);           /* TP_RST high */
        esp_rom_delay_us(200 * 1000);
    } else {
        gpio_config_t io_conf = {
            .intr_type = GPIO_INTR_DISABLE,
            .pin_bit_mask = 1ULL << s_board->tp_rst_gpio,
            .mode = GPIO_MODE_OUTPUT,
        };
        gpio_config(&io_conf);
        gpio_set_level(s_board->tp_rst_gpio, 0);
        esp_rom_delay_us(100 * 1000);
        gpio_set_level(s_board->tp_rst_gpio, 1);
        esp_rom_delay_us(200 * 1000);
    }
}

esp_err_t waveshare_esp32_s3_rgb_lcd_init(void)
{
    board_detect();

    ESP_LOGI(TAG, "Install RGB LCD panel driver");
    esp_lcd_panel_handle_t panel_handle = NULL;
    esp_lcd_rgb_panel_config_t panel_config = {
        .clk_src = LCD_CLK_SRC_DEFAULT,
        .timings = {
            .pclk_hz = EXAMPLE_LCD_PIXEL_CLOCK_HZ,
            .h_res = EXAMPLE_LCD_H_RES,
            .v_res = EXAMPLE_LCD_V_RES,
            .hsync_pulse_width = 4,
            .hsync_back_porch = 8,
            .hsync_front_porch = 8,
            .vsync_pulse_width = 4,
            .vsync_back_porch = 8,
            .vsync_front_porch = 8,
            .flags = {
                .pclk_active_neg = 1,
            },
        },
        .data_width = EXAMPLE_RGB_DATA_WIDTH,
        .bits_per_pixel = EXAMPLE_RGB_BIT_PER_PIXEL,
        .num_fbs = LVGL_PORT_LCD_RGB_BUFFER_NUMS,
        .bounce_buffer_size_px = EXAMPLE_RGB_BOUNCE_BUFFER_SIZE,
        .sram_trans_align = 4,
        .psram_trans_align = 64,
        .hsync_gpio_num = s_board->hsync,
        .vsync_gpio_num = s_board->vsync,
        .de_gpio_num = s_board->de,
        .pclk_gpio_num = s_board->pclk,
        .disp_gpio_num = -1,
        .flags = {
            .fb_in_psram = 1,
        },
    };
    for (int i = 0; i < 16; i++) {
        panel_config.data_gpio_nums[i] = s_board->data[i];
    }
    ESP_ERROR_CHECK(esp_lcd_new_rgb_panel(&panel_config, &panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel_handle));
    s_panel = panel_handle;

    ESP_LOGI(TAG, "Initialize GT911 touch");
    touch_reset();

    esp_lcd_touch_handle_t tp_handle = NULL;
    esp_lcd_panel_io_handle_t tp_io_handle = NULL;
    esp_lcd_panel_io_i2c_config_t tp_io_config = ESP_LCD_TOUCH_IO_I2C_GT911_CONFIG();
    /* Legacy i2c driver sets the bus speed itself; v1 io rejects a non-zero value here. */
    tp_io_config.scl_speed_hz = 0;

    esp_lcd_touch_config_t tp_cfg = {
        .x_max = EXAMPLE_LCD_H_RES,
        .y_max = EXAMPLE_LCD_V_RES,
        .rst_gpio_num = -1,
        .int_gpio_num = -1,
        .levels = {
            .reset = 0,
            .interrupt = 0,
        },
        .flags = {
            .swap_xy = 0,
            .mirror_x = s_board->tp_mirror ? 1 : 0,
            .mirror_y = s_board->tp_mirror ? 1 : 0,
        },
    };

    /* Without the INT-pin trick the GT911 can come up on either address;
     * try the default, fall back to the alternate. */
    esp_err_t terr = ESP_FAIL;
    for (int attempt = 0; attempt < 2 && terr != ESP_OK; attempt++) {
        tp_io_config.dev_addr = attempt == 0
                                    ? ESP_LCD_TOUCH_IO_I2C_GT911_ADDRESS
                                    : ESP_LCD_TOUCH_IO_I2C_GT911_ADDRESS_BACKUP;
        if (esp_lcd_new_panel_io_i2c((esp_lcd_i2c_bus_handle_t)I2C_MASTER_NUM,
                                     &tp_io_config, &tp_io_handle) != ESP_OK) {
            continue;
        }
        terr = esp_lcd_touch_new_i2c_gt911(tp_io_handle, &tp_cfg, &tp_handle);
        if (terr != ESP_OK) {
            esp_lcd_panel_io_del(tp_io_handle);
            tp_io_handle = NULL;
            ESP_LOGW(TAG, "GT911 not at 0x%02x, trying alternate",
                     (unsigned)tp_io_config.dev_addr);
        }
    }
    if (terr != ESP_OK) {
        ESP_LOGE(TAG, "GT911 init failed - running without touch");
        tp_handle = NULL;
    }

    ESP_ERROR_CHECK(lvgl_port_init(panel_handle, tp_handle));

    esp_lcd_rgb_panel_event_callbacks_t cbs = {
#if EXAMPLE_RGB_BOUNCE_BUFFER_SIZE > 0
        .on_bounce_frame_finish = rgb_lcd_on_vsync_event,
#else
        .on_vsync = rgb_lcd_on_vsync_event,
#endif
    };
    ESP_ERROR_CHECK(esp_lcd_rgb_panel_register_event_callbacks(panel_handle, &cbs, NULL));

    return ESP_OK;
}

static esp_err_t bl_gpio_set(int level)
{
    gpio_config_t io_conf = {
        .intr_type = GPIO_INTR_DISABLE,
        .pin_bit_mask = 1ULL << s_board->bl_gpio,
        .mode = GPIO_MODE_OUTPUT,
    };
    gpio_config(&io_conf);
    return gpio_set_level(s_board->bl_gpio, level);
}

esp_err_t waveshare_rgb_lcd_bl_on(void)
{
    if (!s_board->has_ch422g) {
        return bl_gpio_set(1);
    }
    ch422g_write(0x24, 0x01);
    return ch422g_write(0x38, 0x1E);
}

esp_err_t waveshare_rgb_lcd_bl_off(void)
{
    if (!s_board->has_ch422g) {
        return bl_gpio_set(0);
    }
    ch422g_write(0x24, 0x01);
    return ch422g_write(0x38, 0x1A);
}
