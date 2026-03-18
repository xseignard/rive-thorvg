#include "board_waveshare_1_46b.h"

#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/i2c.h"
#include "driver/ledc.h"
#include "driver/spi_master.h"
#include "esp_err.h"
#include "esp_check.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_log.h"

#include "esp_lcd_spd2010.h"

#define WS_LCD_WIDTH 412
#define WS_LCD_HEIGHT 412

#define WS_LCD_DATA0_GPIO 46
#define WS_LCD_DATA1_GPIO 45
#define WS_LCD_DATA2_GPIO 42
#define WS_LCD_DATA3_GPIO 41
#define WS_LCD_SCK_GPIO 40
#define WS_LCD_CS_GPIO 21
#define WS_LCD_BL_GPIO 5

#define WS_I2C_PORT I2C_NUM_0
#define WS_I2C_SCL_GPIO 10
#define WS_I2C_SDA_GPIO 11
#define WS_I2C_HZ 400000

#define WS_EXIO_ADDR 0x20
#define WS_EXIO_OUTPUT_REG 0x01
#define WS_EXIO_CONFIG_REG 0x03
#define WS_EXIO_TP_RST_BIT (1U << 0)   // EXIO1
#define WS_EXIO_LCD_RST_BIT (1U << 1)  // EXIO2

#define WS_TP_ADDR 0x53
#define WS_TP_INT_GPIO 4

#define WS_LCD_SPI_HOST SPI2_HOST
#define WS_LCD_SPI_HZ (80 * 1000 * 1000)

#define WS_TOUCH_SWAP_XY 0
#define WS_TOUCH_INVERT_X 0
#define WS_TOUCH_INVERT_Y 0

static const char* TAG = "ws_1_46b";

static esp_lcd_panel_handle_t s_panel;
static bool s_ready;
static bool s_touch_ready;
static uint8_t s_exio_output_state;
static uint32_t s_touch_i2c_fail_count;
static SemaphoreHandle_t s_flush_done_sem;
static volatile bool s_flush_in_flight;

typedef struct {
    bool pt_exist;
    bool gesture;
    bool aux;
    bool tic_in_bios;
    bool tic_in_cpu;
    bool cpu_run;
    uint16_t read_len;
} ws_touch_status_t;

typedef struct {
    uint8_t status;
    uint16_t next_packet_len;
} ws_touch_hdp_status_t;

static bool lcd_color_trans_done_cb(esp_lcd_panel_io_handle_t panel_io,
                                    esp_lcd_panel_io_event_data_t* edata,
                                    void* user_ctx) {
    (void)panel_io;
    (void)edata;
    (void)user_ctx;

    BaseType_t high_task_wakeup = pdFALSE;
    if (s_flush_done_sem) {
        xSemaphoreGiveFromISR(s_flush_done_sem, &high_task_wakeup);
    }
    return high_task_wakeup == pdTRUE;
}

static esp_err_t i2c_write_reg(uint8_t addr, uint8_t reg, uint8_t value) {
    uint8_t buf[2] = {reg, value};
    return i2c_master_write_to_device(WS_I2C_PORT,
                                      addr,
                                      buf,
                                      sizeof(buf),
                                      pdMS_TO_TICKS(1000));
}

static esp_err_t i2c_write_reg16(uint8_t addr, uint16_t reg, const uint8_t* data, size_t len) {
    if (!data || len == 0) return ESP_ERR_INVALID_ARG;

    uint8_t buf[34];
    if (len > (sizeof(buf) - 2U)) return ESP_ERR_INVALID_SIZE;
    buf[0] = (uint8_t)((reg >> 8) & 0xFFU);
    buf[1] = (uint8_t)(reg & 0xFFU);
    memcpy(&buf[2], data, len);
    return i2c_master_write_to_device(WS_I2C_PORT, addr, buf, len + 2U, pdMS_TO_TICKS(1000));
}

static esp_err_t i2c_read_reg16(uint8_t addr, uint16_t reg, uint8_t* data, size_t len) {
    if (!data || len == 0) return ESP_ERR_INVALID_ARG;

    uint8_t reg_buf[2] = {(uint8_t)((reg >> 8) & 0xFFU), (uint8_t)(reg & 0xFFU)};
    return i2c_master_write_read_device(WS_I2C_PORT,
                                        addr,
                                        reg_buf,
                                        sizeof(reg_buf),
                                        data,
                                        len,
                                        pdMS_TO_TICKS(1000));
}

static bool touch_read_status_length(ws_touch_status_t* out_status) {
    if (!out_status) return false;

    uint8_t status[4] = {0};
    if (i2c_read_reg16(WS_TP_ADDR, 0x2000, status, sizeof(status)) != ESP_OK) {
        s_touch_i2c_fail_count++;
        if ((s_touch_i2c_fail_count % 200U) == 1U) {
            ESP_LOGW(TAG, "touch status read failed (%u)", (unsigned)s_touch_i2c_fail_count);
        }
        return false;
    }

    out_status->pt_exist = (status[0] & 0x01U) != 0U;
    out_status->gesture = (status[0] & 0x02U) != 0U;
    out_status->aux = (status[0] & 0x08U) != 0U;
    out_status->tic_in_bios = (status[1] & 0x40U) != 0U;
    out_status->tic_in_cpu = (status[1] & 0x20U) != 0U;
    out_status->cpu_run = (status[1] & 0x08U) != 0U;
    out_status->read_len = (uint16_t)status[2] | ((uint16_t)status[3] << 8);

    return true;
}

static bool touch_read_hdp_status(ws_touch_hdp_status_t* out_status) {
    if (!out_status) return false;

    uint8_t hdp_status[8] = {0};
    if (i2c_read_reg16(WS_TP_ADDR, 0xFC02, hdp_status, sizeof(hdp_status)) != ESP_OK) {
        return false;
    }

    out_status->status = hdp_status[5];
    out_status->next_packet_len = (uint16_t)hdp_status[2] | ((uint16_t)hdp_status[3] << 8);
    return true;
}

static void touch_read_hdp_remain(uint16_t next_packet_len) {
    if (next_packet_len == 0U) return;
    if (next_packet_len > 32U) next_packet_len = 32U;

    uint8_t remain[32] = {0};
    (void)i2c_read_reg16(WS_TP_ADDR, 0x0003, remain, next_packet_len);
}

static esp_err_t exio_write_output(uint8_t value) {
    s_exio_output_state = value;
    return i2c_write_reg(WS_EXIO_ADDR, WS_EXIO_OUTPUT_REG, value);
}

static esp_err_t exio_set_bit(uint8_t bit, bool level) {
    uint8_t value = s_exio_output_state;
    if (level) {
        value |= bit;
    } else {
        value &= (uint8_t)~bit;
    }
    return exio_write_output(value);
}

static esp_err_t exio_init(void) {
    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = WS_I2C_SDA_GPIO,
        .scl_io_num = WS_I2C_SCL_GPIO,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = WS_I2C_HZ,
    };
    ESP_RETURN_ON_ERROR(i2c_param_config(WS_I2C_PORT, &conf), TAG, "i2c_param_config failed");
    ESP_RETURN_ON_ERROR(i2c_driver_install(WS_I2C_PORT, conf.mode, 0, 0, 0), TAG, "i2c_driver_install failed");

    ESP_RETURN_ON_ERROR(i2c_write_reg(WS_EXIO_ADDR, WS_EXIO_CONFIG_REG, 0x00), TAG, "EXIO config failed");
    ESP_RETURN_ON_ERROR(exio_write_output(0x00), TAG, "EXIO output init failed");
    return ESP_OK;
}

static esp_err_t lcd_reset_via_exio(void) {
    ESP_RETURN_ON_ERROR(exio_set_bit(WS_EXIO_LCD_RST_BIT, false), TAG, "EXIO lcd rst low failed");
    vTaskDelay(pdMS_TO_TICKS(100));
    ESP_RETURN_ON_ERROR(exio_set_bit(WS_EXIO_LCD_RST_BIT, true), TAG, "EXIO lcd rst high failed");
    vTaskDelay(pdMS_TO_TICKS(100));
    return ESP_OK;
}

static esp_err_t touch_write_cmd(uint16_t reg, uint8_t b0, uint8_t b1) {
    uint8_t payload[2] = {b0, b1};
    esp_err_t err = i2c_write_reg16(WS_TP_ADDR, reg, payload, sizeof(payload));
    if (err == ESP_OK) {
        vTaskDelay(pdMS_TO_TICKS(1));
    }
    return err;
}

static esp_err_t touch_clear_int(void) {
    return touch_write_cmd(0x0200, 0x01, 0x00);
}

static esp_err_t touch_cpu_start(void) {
    return touch_write_cmd(0x0400, 0x01, 0x00);
}

static esp_err_t touch_point_mode(void) {
    return touch_write_cmd(0x5000, 0x00, 0x00);
}

static esp_err_t touch_start(void) {
    return touch_write_cmd(0x4600, 0x00, 0x00);
}

static esp_err_t touch_reset_via_exio(void) {
    ESP_RETURN_ON_ERROR(exio_set_bit(WS_EXIO_TP_RST_BIT, false), TAG, "EXIO touch rst low failed");
    vTaskDelay(pdMS_TO_TICKS(50));
    ESP_RETURN_ON_ERROR(exio_set_bit(WS_EXIO_TP_RST_BIT, true), TAG, "EXIO touch rst high failed");
    vTaskDelay(pdMS_TO_TICKS(50));
    return ESP_OK;
}

static void touch_apply_transform(uint16_t raw_x,
                                  uint16_t raw_y,
                                  uint16_t* out_x,
                                  uint16_t* out_y) {
    uint16_t tx = raw_x;
    uint16_t ty = raw_y;

    if (WS_TOUCH_SWAP_XY) {
        uint16_t tmp = tx;
        tx = ty;
        ty = tmp;
    }

    if (tx >= WS_LCD_WIDTH) tx = WS_LCD_WIDTH - 1;
    if (ty >= WS_LCD_HEIGHT) ty = WS_LCD_HEIGHT - 1;

    if (WS_TOUCH_INVERT_X) {
        tx = (uint16_t)((WS_LCD_WIDTH - 1) - tx);
    }
    if (WS_TOUCH_INVERT_Y) {
        ty = (uint16_t)((WS_LCD_HEIGHT - 1) - ty);
    }

    *out_x = tx;
    *out_y = ty;
}

static esp_err_t backlight_init(void) {
    gpio_config_t bk_gpio_config = {
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = 1ULL << WS_LCD_BL_GPIO,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&bk_gpio_config), TAG, "bk gpio config failed");

    ledc_timer_config_t ledc_timer = {
        .duty_resolution = LEDC_TIMER_13_BIT,
        .freq_hz = 5000,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .timer_num = LEDC_TIMER_0,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    ESP_RETURN_ON_ERROR(ledc_timer_config(&ledc_timer), TAG, "ledc_timer_config failed");

    ledc_channel_config_t ledc_channel = {
        .channel = LEDC_CHANNEL_0,
        .duty = 0,
        .gpio_num = WS_LCD_BL_GPIO,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .timer_sel = LEDC_TIMER_0,
    };
    ESP_RETURN_ON_ERROR(ledc_channel_config(&ledc_channel), TAG, "ledc_channel_config failed");
    ESP_RETURN_ON_ERROR(ledc_fade_func_install(0), TAG, "ledc_fade_func_install failed");

    ESP_RETURN_ON_ERROR(ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, 7000), TAG, "ledc_set_duty failed");
    ESP_RETURN_ON_ERROR(ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0), TAG, "ledc_update_duty failed");
    return ESP_OK;
}

bool waveshare_1_46b_display_init(void) {
    if (s_ready) return true;

    s_flush_done_sem = xSemaphoreCreateBinary();
    if (!s_flush_done_sem) {
        ESP_LOGE(TAG, "flush semaphore alloc failed");
        return false;
    }
    s_flush_in_flight = false;

    if (exio_init() != ESP_OK) return false;
    if (lcd_reset_via_exio() != ESP_OK) return false;

    spi_bus_config_t host_config = {
        .data0_io_num = WS_LCD_DATA0_GPIO,
        .data1_io_num = WS_LCD_DATA1_GPIO,
        .sclk_io_num = WS_LCD_SCK_GPIO,
        .data2_io_num = WS_LCD_DATA2_GPIO,
        .data3_io_num = WS_LCD_DATA3_GPIO,
        .data4_io_num = -1,
        .data5_io_num = -1,
        .data6_io_num = -1,
        .data7_io_num = -1,
        .max_transfer_sz = 2048,
        .flags = SPICOMMON_BUSFLAG_MASTER,
    };
    if (spi_bus_initialize(WS_LCD_SPI_HOST, &host_config, SPI_DMA_CH_AUTO) != ESP_OK) {
        ESP_LOGE(TAG, "spi_bus_initialize failed");
        return false;
    }

    esp_lcd_panel_io_spi_config_t io_config = {
        .cs_gpio_num = WS_LCD_CS_GPIO,
        .dc_gpio_num = -1,
        .spi_mode = 0,
        .pclk_hz = WS_LCD_SPI_HZ,
        .trans_queue_depth = 10,
        .on_color_trans_done = lcd_color_trans_done_cb,
        .user_ctx = NULL,
        .lcd_cmd_bits = 32,
        .lcd_param_bits = 8,
        .flags = {
            .quad_mode = 1,
        },
    };

    esp_lcd_panel_io_handle_t io_handle = NULL;
    if (esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)WS_LCD_SPI_HOST, &io_config, &io_handle) != ESP_OK) {
        ESP_LOGE(TAG, "esp_lcd_new_panel_io_spi failed");
        return false;
    }

    spd2010_vendor_config_t vendor_config = {
        .flags = {
            .use_qspi_interface = 1,
        },
    };
    esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = -1,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = 16,
        .flags = {
            .reset_active_high = 0,
        },
        .vendor_config = &vendor_config,
    };

    if (esp_lcd_new_panel_spd2010(io_handle, &panel_config, &s_panel) != ESP_OK) {
        ESP_LOGE(TAG, "esp_lcd_new_panel_spd2010 failed");
        return false;
    }
    if (esp_lcd_panel_reset(s_panel) != ESP_OK) return false;
    if (esp_lcd_panel_init(s_panel) != ESP_OK) return false;
    if (esp_lcd_panel_disp_on_off(s_panel, true) != ESP_OK) return false;
    if (backlight_init() != ESP_OK) return false;

    s_ready = true;
    ESP_LOGI(TAG, "display ready (%dx%d)", WS_LCD_WIDTH, WS_LCD_HEIGHT);
    return true;
}

esp_err_t waveshare_1_46b_display_flush_rgb565_async(const uint16_t* pixels, int width, int height) {
    if (!s_ready || !s_panel || !pixels) return ESP_ERR_INVALID_STATE;
    if (width != WS_LCD_WIDTH || height != WS_LCD_HEIGHT) return ESP_ERR_INVALID_ARG;
    if (s_flush_in_flight) return ESP_ERR_INVALID_STATE;

    s_flush_in_flight = true;
    esp_err_t err = esp_lcd_panel_draw_bitmap(s_panel, 0, 0, width, height, pixels);
    if (err != ESP_OK) {
        s_flush_in_flight = false;
    }
    return err;
}

esp_err_t waveshare_1_46b_display_wait_for_flush(uint32_t timeout_ms) {
    if (!s_flush_in_flight) return ESP_OK;
    if (!s_flush_done_sem) return ESP_ERR_INVALID_STATE;

    if (xSemaphoreTake(s_flush_done_sem, pdMS_TO_TICKS(timeout_ms)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    s_flush_in_flight = false;
    return ESP_OK;
}

void waveshare_1_46b_display_flush_rgb565(const uint16_t* pixels, int width, int height) {
    if (waveshare_1_46b_display_flush_rgb565_async(pixels, width, height) != ESP_OK) return;
    (void)waveshare_1_46b_display_wait_for_flush(1000);
}

bool waveshare_1_46b_touch_init(void) {
    if (s_touch_ready) return true;
    if (!s_ready && !waveshare_1_46b_display_init()) return false;

    gpio_config_t int_gpio_config = {
        .mode = GPIO_MODE_INPUT,
        .pin_bit_mask = 1ULL << WS_TP_INT_GPIO,
        .pull_up_en = GPIO_PULLUP_ENABLE,
    };
    if (gpio_config(&int_gpio_config) != ESP_OK) {
        ESP_LOGE(TAG, "touch int gpio config failed");
        return false;
    }

    if (touch_reset_via_exio() != ESP_OK) {
        ESP_LOGE(TAG, "touch reset failed");
        return false;
    }

    if (touch_clear_int() != ESP_OK) {
        ESP_LOGW(TAG, "touch clear int failed during init");
    }
    if (touch_cpu_start() != ESP_OK) {
        ESP_LOGW(TAG, "touch cpu start failed during init");
    }
    if (touch_point_mode() != ESP_OK) {
        ESP_LOGW(TAG, "touch point mode failed during init");
    }
    if (touch_start() != ESP_OK) {
        ESP_LOGW(TAG, "touch start failed during init");
    }

    s_touch_ready = true;
    ESP_LOGI(TAG, "touch ready");
    return true;
}

bool waveshare_1_46b_touch_read(uint16_t* x, uint16_t* y, bool* pressed) {
    if (!x || !y || !pressed || !s_touch_ready) return false;

    *x = 0;
    *y = 0;
    *pressed = false;

    ws_touch_status_t status = {0};
    if (!touch_read_status_length(&status)) {
        return false;
    }

    if (status.tic_in_bios) {
        (void)touch_clear_int();
        (void)touch_cpu_start();
        return true;
    }

    if (status.tic_in_cpu) {
        (void)touch_point_mode();
        (void)touch_start();
        (void)touch_clear_int();
        return true;
    }

    if (status.cpu_run && status.read_len == 0U) {
        (void)touch_clear_int();
        return true;
    }

    if (status.pt_exist || status.gesture) {
        uint16_t read_len = status.read_len;
        if (read_len > 64U) {
            read_len = 64U;
        }

        if (read_len >= 10U) {
            uint8_t packet[64] = {0};
            if (i2c_read_reg16(WS_TP_ADDR, 0x0003, packet, read_len) == ESP_OK) {
                uint8_t touch_id = packet[4];
                if (status.pt_exist && touch_id <= 0x0AU) {
                    uint16_t tx = (uint16_t)(((packet[7] & 0xF0U) << 4) | packet[5]);
                    uint16_t ty = (uint16_t)(((packet[7] & 0x0FU) << 8) | packet[6]);
                    touch_apply_transform(tx, ty, x, y);
                    *pressed = true;
                }
            }
        }

        for (int i = 0; i < 4; ++i) {
            ws_touch_hdp_status_t hdp_status = {0};
            if (!touch_read_hdp_status(&hdp_status)) {
                break;
            }
            if (hdp_status.status == 0x82U) {
                (void)touch_clear_int();
                break;
            }
            if (hdp_status.status == 0x00U) {
                touch_read_hdp_remain(hdp_status.next_packet_len);
                continue;
            }
            break;
        }

        return true;
    }

    if (status.cpu_run && status.aux) {
        (void)touch_clear_int();
    }

    return true;
}
