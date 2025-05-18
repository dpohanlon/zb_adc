/* zigbee_adc.h */
#ifndef ZIGBEE_ADC_H
#define ZIGBEE_ADC_H

#include <stdint.h>
#include "esp_err.h"
#include "ha/esp_zigbee_ha_standard.h"
#include <zcl/esp_zigbee_zcl_basic.h>
#include <zcl/esp_zigbee_zcl_analog_input.h>

#include "esp_err.h"
#include "esp_check.h"
#include "esp_zigbee_core.h"

/* ADC configuration */
#define ADC_UNIT                    ADC_UNIT_1
#define ADC_CHANNEL                 ADC_CHANNEL_0
#define ADC_ATTEN                   ADC_ATTEN_DB_12
#define ADC_BITWIDTH                ADC_BITWIDTH_DEFAULT

/* End Device config */
#define INSTALLCODE_POLICY_ENABLE   false
#define ED_AGING_TIMEOUT            ESP_ZB_ED_AGING_TIMEOUT_64MIN
#define ED_KEEP_ALIVE               3000    /* ms */

/* Endpoints */
#define HA_ESP_HB_ENDPOINT          1
#define HA_ESP_ANALOG_IN_EP         3

/* Zigbee defaults */
#define ESP_ZB_PRIMARY_CHANNEL_MASK ESP_ZB_TRANSCEIVER_ALL_CHANNELS_MASK
#define ESP_AI_UPDATE_INTERVAL      (1000000)  /* μs */

/* HA descriptors */
#define MANUFACTURER_NAME   "\x0A""Moistmaker"
#define MODEL_IDENTIFIER    "\x08""Moisture"
#define AI_DESCRIPTION      "\x0F""Moisture sensor"

/* Zigbee stack configuration macros */
#define ESP_ZB_ZED_CONFIG()                       \
    {                                             \
        .esp_zb_role = ESP_ZB_DEVICE_TYPE_ED,     \
        .install_code_policy = INSTALLCODE_POLICY_ENABLE, \
        .nwk_cfg.zed_cfg = {                     \
            .ed_timeout = ED_AGING_TIMEOUT,       \
            .keep_alive = ED_KEEP_ALIVE,          \
        },                                        \
    }

#define ESP_ZB_DEFAULT_RADIO_CONFIG()             \
    {                                             \
        .radio_mode = ZB_RADIO_MODE_NATIVE,       \
    }

#define ESP_ZB_DEFAULT_HOST_CONFIG()              \
    {                                             \
        .host_connection_mode = ZB_HOST_CONNECTION_MODE_NONE, \
    }

/* Public API */
esp_err_t init_adc(void);
void esp_zb_app_signal_handler(esp_zb_app_signal_t *signal_struct);
void app_main(void);

#endif /* ZIGBEE_ADC_H */
