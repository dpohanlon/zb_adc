/* zigbee_adc.c */
#include "zigbee_adc.h"

#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"

#include "esp_log.h"
#include "esp_check.h"
#include "esp_mac.h"
#include "esp_timer.h"

#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"

static const char *TAG = "ADC→ZB";

static adc_oneshot_unit_handle_t oneshot_hdl = NULL;
static adc_cali_handle_t cali_hdl = NULL;

/* Basic + Identify cluster list creator */
static esp_zb_cluster_list_t *add_basic_and_identify_clusters_create(
    esp_zb_identify_cluster_cfg_t *identify_cfg,
    esp_zb_basic_cluster_cfg_t    *basic_cfg)
{
    esp_zb_cluster_list_t   *list  = esp_zb_zcl_cluster_list_create();
    esp_zb_attribute_list_t *basic = esp_zb_basic_cluster_create(basic_cfg);

    ESP_ERROR_CHECK(esp_zb_basic_cluster_add_attr(
        basic,
        ESP_ZB_ZCL_ATTR_BASIC_MANUFACTURER_NAME_ID,
        MANUFACTURER_NAME));
    ESP_ERROR_CHECK(esp_zb_basic_cluster_add_attr(
        basic,
        ESP_ZB_ZCL_ATTR_BASIC_MODEL_IDENTIFIER_ID,
        MODEL_IDENTIFIER));
    ESP_ERROR_CHECK(esp_zb_cluster_list_add_basic_cluster(
        list, basic, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE));

    ESP_ERROR_CHECK(esp_zb_cluster_list_add_identify_cluster(
        list,
        esp_zb_identify_cluster_create(identify_cfg),
        ESP_ZB_ZCL_CLUSTER_SERVER_ROLE));
    ESP_ERROR_CHECK(esp_zb_cluster_list_add_identify_cluster(
        list,
        esp_zb_zcl_attr_list_create(ESP_ZB_ZCL_CLUSTER_ID_IDENTIFY),
        ESP_ZB_ZCL_CLUSTER_CLIENT_ROLE));

    return list;
}

/* Generic endpoint‐adder helper */
static void add_endpoint(esp_zb_ep_list_t *ep_list,
                         uint8_t           ep_id,
                         esp_zb_cluster_list_t *clusters)
{
    esp_zb_endpoint_config_t cfg = {
        .endpoint           = ep_id,
        .app_profile_id     = ESP_ZB_AF_HA_PROFILE_ID,
        .app_device_id      = ESP_ZB_HA_CUSTOM_ATTR_DEVICE_ID,
        .app_device_version = 0,
    };
    esp_zb_ep_list_add_ep(ep_list, clusters, cfg);
}

/* ADC initialization factored out */
esp_err_t init_adc(void)
{
    adc_oneshot_unit_init_cfg_t unit_cfg = {
        .unit_id  = ADC_UNIT,
        .ulp_mode = ADC_ULP_MODE_DISABLE,
    };
    ESP_RETURN_ON_ERROR(
        adc_oneshot_new_unit(&unit_cfg, &oneshot_hdl),
        TAG, "ADC unit init failed");

    adc_oneshot_chan_cfg_t chan_cfg = {
        .bitwidth = ADC_BITWIDTH,
        .atten    = ADC_ATTEN,
    };
    ESP_RETURN_ON_ERROR(
        adc_oneshot_config_channel(oneshot_hdl, ADC_CHANNEL, &chan_cfg),
        TAG, "ADC channel config failed");

    adc_cali_curve_fitting_config_t cali_cfg = {
        .unit_id  = ADC_UNIT,
        .atten    = ADC_ATTEN,
        .bitwidth = ADC_BITWIDTH,
    };
    ESP_RETURN_ON_ERROR(
        adc_cali_create_scheme_curve_fitting(&cali_cfg, &cali_hdl),
        TAG, "ADC calibration failed");

    return ESP_OK;
}

static void identify_notify_cb(uint8_t endpoint)
{
    ESP_LOGI(TAG, "Identify received on endpoint %u", endpoint);
}

static esp_err_t zb_attribute_handler(
    const esp_zb_zcl_set_attr_value_message_t *msg)
{
    ESP_RETURN_ON_FALSE(msg, ESP_FAIL, TAG, "Empty message");
    ESP_RETURN_ON_FALSE(
        msg->info.status == ESP_ZB_ZCL_STATUS_SUCCESS,
        ESP_ERR_INVALID_ARG, TAG,
        "Received message: error status(%d)",
        msg->info.status);

    ESP_LOGI(TAG,
             "Received message: endpoint(%d), cluster(0x%x), attr(0x%x), size(%d)",
             msg->info.dst_endpoint,
             msg->info.cluster,
             msg->attribute.id,
             msg->attribute.data.size);
    return ESP_OK;
}

static esp_err_t zb_action_handler(
    esp_zb_core_action_callback_id_t callback_id,
    const void                      *message)
{
    if (callback_id == ESP_ZB_CORE_SET_ATTR_VALUE_CB_ID) {
        return zb_attribute_handler(
            (esp_zb_zcl_set_attr_value_message_t *)message);
    } else {
        ESP_LOGW(TAG, "Unhandled Zigbee action callback: 0x%x", callback_id);
        return ESP_OK;
    }
}

static void adc_sampling_task(void)
{
    int raw;
    ESP_ERROR_CHECK(adc_oneshot_read(oneshot_hdl, ADC_CHANNEL, &raw));

    int voltage_mv;
    ESP_ERROR_CHECK(adc_cali_raw_to_voltage(cali_hdl, raw, &voltage_mv));
    ESP_LOGI(TAG, "Voltage: %d mV", voltage_mv);

    float v_f = (float)voltage_mv;
    esp_zb_lock_acquire(portMAX_DELAY);
    esp_zb_zcl_set_attribute_val(
        HA_ESP_ANALOG_IN_EP,
        ESP_ZB_ZCL_CLUSTER_ID_ANALOG_INPUT,
        ESP_ZB_ZCL_CLUSTER_SERVER_ROLE,
        ESP_ZB_ZCL_ATTR_ANALOG_INPUT_PRESENT_VALUE_ID,
        &v_f,
        false);
    esp_zb_lock_release();
}

static void report_analog_in_attr(uint8_t ep)
{
    esp_zb_zcl_report_attr_cmd_t cmd = {
        .address_mode  = ESP_ZB_APS_ADDR_MODE_DST_ADDR_ENDP_NOT_PRESENT,
        .attributeID   = ESP_ZB_ZCL_ATTR_ANALOG_INPUT_PRESENT_VALUE_ID,
        .direction     = ESP_ZB_ZCL_CMD_DIRECTION_TO_CLI,
        .clusterID     = ESP_ZB_ZCL_CLUSTER_ID_ANALOG_INPUT,
        .zcl_basic_cmd = { .src_endpoint = ep },
    };

    esp_zb_lock_acquire(portMAX_DELAY);
    esp_zb_zcl_report_attr_cmd_req(&cmd);
    esp_zb_lock_release();
    ESP_EARLY_LOGI(TAG, "Sent AI 'report attributes' command");
}

static void analog_in_timer_callback(void *arg)
{
    ESP_LOGI(TAG, "Analog Input timer callback (boot+%lld µs)",
             esp_timer_get_time());
    adc_sampling_task();
    report_analog_in_attr(HA_ESP_ANALOG_IN_EP);
}

static void start_analog_in_timer(void)
{
    const esp_timer_create_args_t timer_args = {
        .callback = &analog_in_timer_callback,
        .name     = "analog_input_timer",
    };
    esp_timer_handle_t timer;
    ESP_ERROR_CHECK(esp_timer_create(&timer_args, &timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(timer, ESP_AI_UPDATE_INTERVAL));
}

static esp_err_t deferred_driver_init(void)
{
    ESP_LOGI(TAG, "Deferred driver init: starting ADC & timer");
    start_analog_in_timer();
    return init_adc();
}

static void bdb_start_top_level_commissioning_cb(uint8_t mode_mask)
{
    ESP_RETURN_ON_FALSE(
        esp_zb_bdb_start_top_level_commissioning(mode_mask) == ESP_OK,
        , TAG, "Failed to start BDB commissioning");
}

void esp_zb_app_signal_handler(esp_zb_app_signal_t *signal_struct)
{
    uint32_t          sig = *signal_struct->p_app_signal;
    esp_err_t         status = signal_struct->esp_err_status;

    switch ((esp_zb_app_signal_type_t)sig) {
    case ESP_ZB_ZDO_SIGNAL_SKIP_STARTUP:
        ESP_LOGI(TAG, "Initialize Zigbee stack");
        esp_zb_bdb_start_top_level_commissioning(
            ESP_ZB_BDB_MODE_INITIALIZATION);
        break;

    case ESP_ZB_BDB_SIGNAL_DEVICE_FIRST_START:
    case ESP_ZB_BDB_SIGNAL_DEVICE_REBOOT:
        if (status == ESP_OK) {
            ESP_LOGI(TAG, "Deferred init: %s",
                     deferred_driver_init() == ESP_OK
                         ? "successful"
                         : "failed");
            if (esp_zb_bdb_is_factory_new()) {
                ESP_LOGI(TAG, "Network steering");
                esp_zb_bdb_start_top_level_commissioning(
                    ESP_ZB_BDB_MODE_NETWORK_STEERING);
            }
        } else {
            ESP_LOGW(TAG, "Zigbee init failed: %s",
                     esp_err_to_name(status));
        }
        break;

    case ESP_ZB_BDB_SIGNAL_STEERING:
        if (status == ESP_OK) {
            esp_zb_ieee_addr_t panid;
            esp_zb_get_extended_pan_id(panid);
            ESP_LOGI(TAG,
                     "Joined: ext PAN %02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x, "
                     "PAN ID 0x%04hx, ch %d, addr 0x%04hx",
                     (unsigned)panid[7], (unsigned)panid[6],
                     (unsigned)panid[5], (unsigned)panid[4],
                     (unsigned)panid[3], (unsigned)panid[2],
                     (unsigned)panid[1], (unsigned)panid[0],
                     (unsigned short)esp_zb_get_pan_id(),
                     (int)esp_zb_get_current_channel(),
                     (unsigned short)esp_zb_get_short_address());
        } else {
            ESP_LOGW(TAG, "Steering failed: %s",
                     esp_err_to_name(status));
            esp_zb_scheduler_alarm(
                (esp_zb_callback_t)bdb_start_top_level_commissioning_cb,
                ESP_ZB_BDB_MODE_NETWORK_STEERING,
                1000);
        }
        break;

    default:
        ESP_LOGI(TAG,
                 "ZDO signal: %s (0x%x), status: %s",
                 esp_zb_zdo_signal_to_string((esp_zb_app_signal_type_t)sig),
                 (unsigned)sig,
                 esp_err_to_name(status));
        break;
    }
}

static void esp_zb_task(void *pvParameters)
{
    esp_zb_cfg_t zb_cfg = ESP_ZB_ZED_CONFIG();
    esp_zb_init(&zb_cfg);

    esp_zb_ep_list_t *ep_list = esp_zb_ep_list_create();

    esp_zb_basic_cluster_cfg_t    basic_cfg = {
        .power_source = ESP_ZB_ZCL_BASIC_POWER_SOURCE_DEFAULT_VALUE,
        .zcl_version  = ESP_ZB_ZCL_BASIC_ZCL_VERSION_DEFAULT_VALUE,
    };
    esp_zb_identify_cluster_cfg_t id_cfg = {
        .identify_time = ESP_ZB_ZCL_IDENTIFY_IDENTIFY_TIME_DEFAULT_VALUE,
    };

    /* Heartbeat endpoint: Basic + Identify */
    esp_zb_cluster_list_t *hb_clusters =
        add_basic_and_identify_clusters_create(&id_cfg, &basic_cfg);
    add_endpoint(ep_list, HA_ESP_HB_ENDPOINT, hb_clusters);
    esp_zb_identify_notify_handler_register(
        HA_ESP_HB_ENDPOINT, identify_notify_cb);

    /* Analog-in endpoint */
    float initial_value = 0x123;
    esp_zb_analog_input_cluster_cfg_t ai_cfg = {
        .out_of_service = ESP_ZB_ZCL_ANALOG_INPUT_OUT_OF_SERVICE_DEFAULT_VALUE,
        .status_flags   = ESP_ZB_ZCL_ANALOG_INPUT_STATUS_FLAG_DEFAULT_VALUE,
        .present_value  = initial_value,
    };
    esp_zb_cluster_list_t   *ai_clusters = esp_zb_zcl_cluster_list_create();
    esp_zb_attribute_list_t *ai_list     = esp_zb_analog_input_cluster_create(&ai_cfg);

    float min_val  = 0x00, max_val = 0x1000;
    uint16_t units = 0x005f;
    ESP_ERROR_CHECK(esp_zb_analog_input_cluster_add_attr(
        ai_list, ESP_ZB_ZCL_ATTR_ANALOG_INPUT_DESCRIPTION_ID, AI_DESCRIPTION));
    ESP_ERROR_CHECK(esp_zb_analog_input_cluster_add_attr(
        ai_list, ESP_ZB_ZCL_ATTR_ANALOG_INPUT_MAX_PRESENT_VALUE_ID, &max_val));
    ESP_ERROR_CHECK(esp_zb_analog_input_cluster_add_attr(
        ai_list, ESP_ZB_ZCL_ATTR_ANALOG_INPUT_MIN_PRESENT_VALUE_ID, &min_val));
    ESP_ERROR_CHECK(esp_zb_analog_input_cluster_add_attr(
        ai_list, ESP_ZB_ZCL_ATTR_ANALOG_INPUT_ENGINEERING_UNITS_ID, &units));
    ESP_ERROR_CHECK(esp_zb_cluster_list_add_analog_input_cluster(
        ai_clusters, ai_list, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE));

    add_endpoint(ep_list, HA_ESP_ANALOG_IN_EP, ai_clusters);

    esp_zb_device_register(ep_list);
    esp_zb_core_action_handler_register(zb_action_handler);
    esp_zb_set_primary_network_channel_set(ESP_ZB_PRIMARY_CHANNEL_MASK);
    esp_zb_start(false);
    esp_zb_stack_main_loop();
}

void app_main(void)
{
    ESP_ERROR_CHECK(nvs_flash_init());

    esp_zb_platform_config_t platform_cfg = {
        .radio_config = ESP_ZB_DEFAULT_RADIO_CONFIG(),
        .host_config  = ESP_ZB_DEFAULT_HOST_CONFIG(),
    };
    ESP_ERROR_CHECK(esp_zb_platform_config(&platform_cfg));

    xTaskCreate(esp_zb_task, "Zigbee_main", 4096, NULL, 5, NULL);
}
