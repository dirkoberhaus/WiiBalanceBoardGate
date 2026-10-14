// TTGO/LILYGO T-Display battery sense. ADC_EN (GPIO14) must be held high to
// power the divider; ADC input is GPIO34 (ADC1_CH6 - ADC1 so it still works
// alongside WiFi, unlike ADC2). Divider is a fixed 1:2.
//
// Only meaningful while running on battery alone: with USB connected, the
// charge controller drives the battery at its (higher) charging voltage,
// not the resting voltage a state-of-charge estimate needs - confirmed on
// real hardware, readings while USB-powered pin at 100%, unplugging gives a
// sane value. There's also some inherent ESP32 ADC nonlinearity without a
// per-chip two-point calibration, so treat this as a rough gauge.
#ifdef DISPLAY_ST7789

#include "battery.h"

#include "driver/gpio.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_log.h"

static const char *TAG = "battery";

#define PIN_ADC_EN GPIO_NUM_14
#define BATTERY_ADC_CHANNEL ADC_CHANNEL_6 // GPIO34
#define BATTERY_DIVIDER_RATIO (2.0f)
#define BATTERY_EMPTY_MV 3300
#define BATTERY_FULL_MV 4200

static adc_oneshot_unit_handle_t s_adc = NULL;
static adc_cali_handle_t s_cali = NULL;

bool battery_init(void)
{
    gpio_config_t en_cfg = {
        .pin_bit_mask = 1ULL << PIN_ADC_EN,
        .mode = GPIO_MODE_OUTPUT,
    };
    gpio_config(&en_cfg);
    gpio_set_level(PIN_ADC_EN, 1);

    adc_oneshot_unit_init_cfg_t unit_cfg = { .unit_id = ADC_UNIT_1 };
    if (adc_oneshot_new_unit(&unit_cfg, &s_adc) != ESP_OK) {
        return false;
    }

    adc_oneshot_chan_cfg_t chan_cfg = {
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    if (adc_oneshot_config_channel(s_adc, BATTERY_ADC_CHANNEL, &chan_cfg) != ESP_OK) {
        return false;
    }

    adc_cali_line_fitting_config_t cali_cfg = {
        .unit_id = ADC_UNIT_1,
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
        .default_vref = 1100, // fallback only if no eFuse Vref/TP calibration is burnt in
    };
    if (adc_cali_create_scheme_line_fitting(&cali_cfg, &s_cali) != ESP_OK) {
        ESP_LOGW(TAG, "No ADC calibration available - readings will be less accurate");
        s_cali = NULL;
    }

    return true;
}

int battery_read_percent(void)
{
    if (!s_adc) {
        return -1;
    }

    int mv;
    if (s_cali) {
        if (adc_oneshot_get_calibrated_result(s_adc, s_cali, BATTERY_ADC_CHANNEL, &mv) != ESP_OK) {
            return -1;
        }
    } else {
        int raw;
        if (adc_oneshot_read(s_adc, BATTERY_ADC_CHANNEL, &raw) != ESP_OK) {
            return -1;
        }
        mv = (raw * 3300) / 4095;
    }

    int batt_mv = (int)(mv * BATTERY_DIVIDER_RATIO);
    int pct = ((batt_mv - BATTERY_EMPTY_MV) * 100) / (BATTERY_FULL_MV - BATTERY_EMPTY_MV);
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    return pct;
}

#endif // DISPLAY_ST7789
