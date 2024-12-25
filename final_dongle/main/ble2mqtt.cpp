extern "C"
{
#include "config.h"
#include "broadcasters.h"
#include "ble.h"
#include "ble_utils.h"
#include "eth.h"
#include "httpd.h"
#include "log.h"
#include "mqtt.h"
#include "resolve.h"
#include "wifi.h"
#include <esp_err.h>
#include <esp_log.h>
#include <esp_system.h>
#include <esp_timer.h>
#include <nvs.h>
#include <nvs_flash.h>
#include <cJSON.h>
#include <mdns.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include <freertos/timers.h>
#include <string.h>
#include <esp_gatts_api.h>
#include <esp_bt_defs.h>
#include <esp_gatt_defs.h>
#include "wifi.h"
#include <esp_wifi.h>
#include "button.h"
}
#include <ui.h>

#define MAX_TOPIC_LEN 256
static const char *TAG = "BLE2MQTT";
uint8_t num_network_disconnections = 0;

typedef struct
{
    mac_addr_t mac;
    ble_uuid_t service;
    ble_uuid_t characteristic;
    uint8_t index;
} mqtt_ctx_t;

static TaskHandle_t task1_t;
static TaskHandle_t task2_t;
static bool isPass = false;
static bool isAPMode = false;
static bool isConnect = false;
static bool isf = true;

#define GATTS_TAG "GATTS_DEMO"
#define MAX_TOPIC_LEN 256
#define GATTS_SERVICE_UUID_TEST_A 0x00FF
#define GATTS_CHAR_UUID_TEST_A 0xFF01
#define GATTS_DESCR_UUID_TEST_A 0x3333
#define GATTS_NUM_HANDLE_TEST_A 4

#define GATTS_SERVICE_UUID_TEST_B 0x00EE
#define GATTS_CHAR_UUID_TEST_B 0xEE01
#define GATTS_DESCR_UUID_TEST_B 0x2222
#define GATTS_NUM_HANDLE_TEST_B 4

#define TEST_DEVICE_NAME "SMART_THERMOMETER"
#define TEST_MANUFACTURER_DATA_LEN 17

#define GATTS_DEMO_CHAR_VAL_LEN_MAX 0x40

#define PREPARE_BUF_MAX_SIZE 1024
static const char *states[] =
    {
        [BUTTON_PRESSED] = "pressed",
        [BUTTON_RELEASED] = "released",
        [BUTTON_CLICKED] = "clicked",
        [BUTTON_PRESSED_LONG] = "pressed long",
};

static button_t btn;
static char *last_message;
extern "C"
{
    static uint8_t char1_str[] = {0x11, 0x22, 0x33};
    static esp_gatt_char_prop_t a_property = 0;
    static esp_gatt_char_prop_t b_property = 0;

    static esp_attr_value_t gatts_demo_char1_val =
        {
            .attr_max_len = GATTS_DEMO_CHAR_VAL_LEN_MAX,
            .attr_len = sizeof(char1_str),
            .attr_value = char1_str,
    };

    static uint8_t adv_config_done = 0;
#define adv_config_flag (1 << 0)
#define scan_rsp_config_flag (1 << 1)

#ifdef CONFIG_SET_RAW_ADV_DATA
    static uint8_t raw_adv_data[] = {
        0x02, 0x01, 0x06,
        0x02, 0x0a, 0xeb, 0x03, 0x03, 0xab, 0xcd};
    static uint8_t raw_scan_rsp_data[] = {
        0x0f, 0x09, 0x45, 0x53, 0x50, 0x5f, 0x47, 0x41, 0x54, 0x54, 0x53, 0x5f, 0x44,
        0x45, 0x4d, 0x4f};
#else

    static uint8_t adv_service_uuid128[32] = {
        /* LSB <--------------------------------------------------------------------------------> MSB */
        // first uuid, 16bit, [12],[13] is the value
        0xfb,
        0x34,
        0x9b,
        0x5f,
        0x80,
        0x00,
        0x00,
        0x80,
        0x00,
        0x10,
        0x00,
        0x00,
        0xEE,
        0x00,
        0x00,
        0x00,
        // second uuid, 32bit, [12], [13], [14], [15] is the value
        0xfb,
        0x34,
        0x9b,
        0x5f,
        0x80,
        0x00,
        0x00,
        0x80,
        0x00,
        0x10,
        0x00,
        0x00,
        0xFF,
        0x00,
        0x00,
        0x00,
    };

    // The length of adv data must be less than 31 bytes
    // static uint8_t test_manufacturer[TEST_MANUFACTURER_DATA_LEN] =  {0x12, 0x23, 0x45, 0x56};
    // adv data
    static esp_ble_adv_data_t adv_data = {
        .set_scan_rsp = false,
        .include_name = true,
        .include_txpower = false,
        .min_interval = 0x0006, // slave connection min interval, Time = min_interval * 1.25 msec
        .max_interval = 0x0010, // slave connection max interval, Time = max_interval * 1.25 msec
        .appearance = 0x00,
        .manufacturer_len = 0,       // TEST_MANUFACTURER_DATA_LEN,
        .p_manufacturer_data = NULL, //&test_manufacturer[0],
        .service_data_len = 0,
        .p_service_data = NULL,
        .service_uuid_len = sizeof(adv_service_uuid128),
        .p_service_uuid = adv_service_uuid128,
        .flag = (ESP_BLE_ADV_FLAG_GEN_DISC | ESP_BLE_ADV_FLAG_BREDR_NOT_SPT),
    };
    // scan response data
    static esp_ble_adv_data_t scan_rsp_data = {
        .set_scan_rsp = true,
        .include_name = true,
        .include_txpower = true,
        //.min_interval = 0x0006,
        //.max_interval = 0x0010,
        .appearance = 0x00,
        .manufacturer_len = 0,       // TEST_MANUFACTURER_DATA_LEN,
        .p_manufacturer_data = NULL, //&test_manufacturer[0],
        .service_data_len = 0,
        .p_service_data = NULL,
        .service_uuid_len = sizeof(adv_service_uuid128),
        .p_service_uuid = adv_service_uuid128,
        .flag = (ESP_BLE_ADV_FLAG_GEN_DISC | ESP_BLE_ADV_FLAG_BREDR_NOT_SPT),
    };

#endif /* CONFIG_SET_RAW_ADV_DATA */

    static esp_ble_adv_params_t adv_params = {
        .adv_int_min = 0x20,
        .adv_int_max = 0x40,
        .adv_type = ADV_TYPE_IND,
        .own_addr_type = BLE_ADDR_TYPE_PUBLIC,
        //.peer_addr            =
        //.peer_addr_type       =
        .channel_map = ADV_CHNL_ALL,
        .adv_filter_policy = ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY,
    };
    /// Declare the static function
    static void gatts_profile_a_event_handler(esp_gatts_cb_event_t event, esp_gatt_if_t gatts_if, esp_ble_gatts_cb_param_t *param);
    static void gatts_profile_b_event_handler(esp_gatts_cb_event_t event, esp_gatt_if_t gatts_if, esp_ble_gatts_cb_param_t *param);

#define PROFILE_NUM 2
#define PROFILE_A_APP_ID 0
#define PROFILE_B_APP_ID 1
    char ESPStatustopic[12] = "/ESPStatus";
    struct gatts_profile_inst
    {
        esp_gatts_cb_t gatts_cb;
        uint16_t gatts_if;
        uint16_t app_id;
        uint16_t conn_id;
        uint16_t service_handle;
        esp_gatt_srvc_id_t service_id;
        uint16_t char_handle;
        esp_bt_uuid_t char_uuid;
        esp_gatt_perm_t perm;
        esp_gatt_char_prop_t property;
        uint16_t descr_handle;
        esp_bt_uuid_t descr_uuid;
    };
    /* One gatt-based profile one app_id and one gatts_if, this array will store the gatts_if returned by ESP_GATTS_REG_EVT */
    static struct gatts_profile_inst gl_profile_tab[PROFILE_NUM] = {
        [PROFILE_A_APP_ID] = {
            .gatts_cb = gatts_profile_a_event_handler,
            .gatts_if = ESP_GATT_IF_NONE, /* Not get the gatt_if, so initial is ESP_GATT_IF_NONE */
        },
        [PROFILE_B_APP_ID] = {
            .gatts_cb = gatts_profile_b_event_handler, /* This demo does not implement, similar as profile A */
            .gatts_if = ESP_GATT_IF_NONE,              /* Not get the gatt_if, so initial is ESP_GATT_IF_NONE */
        },
    };

    typedef struct
    {
        uint8_t *prepare_buf;
        int prepare_len;
    } prepare_type_env_t;

    static prepare_type_env_t a_prepare_write_env;
    static prepare_type_env_t b_prepare_write_env;
    static char *get_mac_thermo(mac_addr_t mac)
    {
        static char topic[MAX_TOPIC_LEN];

        snprintf(topic, MAX_TOPIC_LEN, "%s" MAC_FMT,
                 config_mqtt_prefix_get(), MAC_PARAM(mac));

        return topic;
    }

    static const char *device_mac_get(void)
    {
        static const char *name = NULL;
        uint8_t *mac = NULL;

        if (name)
            return name;

        if ((name = config_network_hostname_get()))
            return name;

        switch (config_network_type_get())
        {
        case NETWORK_TYPE_ETH:
            mac = eth_mac_get();
            break;
        case NETWORK_TYPE_WIFI:
            mac = wifi_mac_get();
            break;
        }
        name = (char *)malloc(24);
        sprintf((char *)name, "%02x:%02x:%02x:%02x:%02x:%02x", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

        return name;
        // char name[25];
        // uint8_t *mac = wifi_mac_get();

        // // name = (char *)malloc(14);
        // sprintf(name, "%02x%02x%02x%02x%02x%02x", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

        // // snprintf(status_text, sizeof(status_text), "%.1f °C", temp);
        // return name;
    }
    static const char *device_name_get(void)
    {
        static const char *name = NULL;
        uint8_t *mac = NULL;

        if (name)
            return name;

        if ((name = config_network_hostname_get()))
            return name;

        switch (config_network_type_get())
        {
        case NETWORK_TYPE_ETH:
            mac = eth_mac_get();
            break;
        case NETWORK_TYPE_WIFI:
            mac = wifi_mac_get();
            break;
        }
        name = (char *)malloc(14);
        sprintf((char *)name, "BLE2MQTT-%02X%02X", mac[4], mac[5]);

        return name;
    }

    void example_write_event_env(esp_gatt_if_t gatts_if, prepare_type_env_t *prepare_write_env, esp_ble_gatts_cb_param_t *param)
    {
        esp_gatt_status_t status = ESP_GATT_OK;
        if (param->write.need_rsp)
        {
            if (param->write.is_prep)
            {
                if (prepare_write_env->prepare_buf == NULL)
                {
                    prepare_write_env->prepare_buf = (uint8_t *)malloc(PREPARE_BUF_MAX_SIZE * sizeof(uint8_t));
                    prepare_write_env->prepare_len = 0;
                    if (prepare_write_env->prepare_buf == NULL)
                    {
                        ESP_LOGE(GATTS_TAG, "Gatt_server prep no mem");
                        status = ESP_GATT_NO_RESOURCES;
                    }
                }
                else
                {
                    if (param->write.offset > PREPARE_BUF_MAX_SIZE)
                    {
                        status = ESP_GATT_INVALID_OFFSET;
                    }
                    else if ((param->write.offset + param->write.len) > PREPARE_BUF_MAX_SIZE)
                    {
                        status = ESP_GATT_INVALID_ATTR_LEN;
                    }
                }

                esp_gatt_rsp_t *gatt_rsp = (esp_gatt_rsp_t *)malloc(sizeof(esp_gatt_rsp_t));
                gatt_rsp->attr_value.len = param->write.len;
                gatt_rsp->attr_value.handle = param->write.handle;
                gatt_rsp->attr_value.offset = param->write.offset;
                gatt_rsp->attr_value.auth_req = ESP_GATT_AUTH_REQ_NONE;
                memcpy(gatt_rsp->attr_value.value, param->write.value, param->write.len);
                esp_err_t response_err = esp_ble_gatts_send_response(gatts_if, param->write.conn_id, param->write.trans_id, status, gatt_rsp);
                if (response_err != ESP_OK)
                {
                    ESP_LOGE(GATTS_TAG, "Send response error");
                }
                free(gatt_rsp);
                if (status != ESP_GATT_OK)
                {
                    return;
                }
                memcpy(prepare_write_env->prepare_buf + param->write.offset,
                       param->write.value,
                       param->write.len);
                prepare_write_env->prepare_len += param->write.len;
            }
            else
            {
                esp_ble_gatts_send_response(gatts_if, param->write.conn_id, param->write.trans_id, status, NULL);
            }
        }
    }

    void example_exec_write_event_env(prepare_type_env_t *prepare_write_env, esp_ble_gatts_cb_param_t *param)
    {
        if (param->exec_write.exec_write_flag == ESP_GATT_PREP_WRITE_EXEC)
        {
            esp_log_buffer_hex(GATTS_TAG, prepare_write_env->prepare_buf, prepare_write_env->prepare_len);
        }
        else
        {
            ESP_LOGI(GATTS_TAG, "ESP_GATT_PREP_WRITE_CANCEL");
        }
        if (prepare_write_env->prepare_buf)
        {
            free(prepare_write_env->prepare_buf);
            prepare_write_env->prepare_buf = NULL;
        }
        prepare_write_env->prepare_len = 0;
    }

    void task2(void *arg)
    {
        esp_ble_gatts_cb_param_t *param = (esp_ble_gatts_cb_param_t *)arg;
        cJSON *wifi = cJSON_Parse((char *)param->write.value);
        cJSON *c_ssid = cJSON_GetObjectItemCaseSensitive(wifi, "ssid");
        cJSON *c_password = cJSON_GetObjectItemCaseSensitive(wifi, "password");

        char *ssid = "";
        if (cJSON_IsString(c_ssid))
            ssid = c_ssid->valuestring;
        char *password = "";
        if (cJSON_IsString(c_password))
            password = c_password->valuestring;
        // wifi_config_t wifi_config = {.ap = {.max_connection = 1}};
        // strncpy((char *)wifi_config.ap.ssid, ssid, 32);
        // if (password)
        // {
        //     strncpy((char *)wifi_config.sta.password, password, 64);
        //     wifi_config.ap.authmode = WIFI_AUTH_WPA2_PSK;
        // }
        // else
        //     wifi_config.ap.authmode = WIFI_AUTH_OPEN;

        wifi_disconnect();
        ESP_ERROR_CHECK(esp_wifi_stop());
        ESP_LOGI(GATTS_TAG, "u : %s , p : %s", ssid, password);
        // xTaskCreate(, "network_wifi_add", 2048 * 5, NULL, 4, NULL);
        long h = (unsigned long)esp_get_free_internal_heap_size();
        ESP_LOGI(TAG, "----->>>>>>>>> Heap Free size %lx  <<<<<-----", h);
        network_wifi_add(ssid, password);
        h = (unsigned long)esp_get_free_internal_heap_size();
        ESP_LOGI(TAG, "----->>>>>>>>> Heap Free size %lx  <<<<<-----", h);

        // ShowStatus("กำลังตั้งค่า WIFI");
        isConnect = false;
        wifi_connect(config_network_wifi_ssid_get(), config_network_wifi_password_get(),
                     wifi_eap_atomethod(config_eap_method_get()),
                     config_eap_identity_get(),
                     config_eap_username_get(), config_eap_password_get(),
                     config_eap_ca_cert_get(), config_eap_client_cert_get(),
                     config_eap_client_key_get());
        h = (unsigned long)esp_get_free_internal_heap_size();
        ESP_LOGI(TAG, "----->>>>>>>>> Heap Free size %lx  <<<<<-----", h);
        vTaskSuspend(task2_t);
    }
    static void gatts_profile_a_event_handler(esp_gatts_cb_event_t event, esp_gatt_if_t gatts_if, esp_ble_gatts_cb_param_t *param)
    {
        switch (event)
        {
        case ESP_GATTS_REG_EVT:
        {
            ESP_LOGI(GATTS_TAG, "REGISTER_APP_EVT, status %d, app_id %d", param->reg.status, param->reg.app_id);
            gl_profile_tab[PROFILE_A_APP_ID].service_id.is_primary = true;
            gl_profile_tab[PROFILE_A_APP_ID].service_id.id.inst_id = 0x00;
            gl_profile_tab[PROFILE_A_APP_ID].service_id.id.uuid.len = ESP_UUID_LEN_16;
            gl_profile_tab[PROFILE_A_APP_ID].service_id.id.uuid.uuid.uuid16 = GATTS_SERVICE_UUID_TEST_A;

            esp_err_t set_dev_name_ret = esp_ble_gap_set_device_name(TEST_DEVICE_NAME);
            if (set_dev_name_ret)
            {
                ESP_LOGE(GATTS_TAG, "set device name failed, error code = %x", set_dev_name_ret);
            }
            // config adv data
            esp_err_t ret = esp_ble_gap_config_adv_data(&adv_data);
            if (ret)
            {
                ESP_LOGE(GATTS_TAG, "config adv data failed, error code = %x", ret);
            }
            adv_config_done |= adv_config_flag;
            // config scan response data
            ret = esp_ble_gap_config_adv_data(&scan_rsp_data);
            if (ret)
            {
                ESP_LOGE(GATTS_TAG, "config scan response data failed, error code = %x", ret);
            }
            adv_config_done |= scan_rsp_config_flag;
            esp_ble_gatts_create_service(gatts_if, &gl_profile_tab[PROFILE_A_APP_ID].service_id, GATTS_NUM_HANDLE_TEST_A);
            break;
        }
        case ESP_GATTS_READ_EVT:
        {
            ESP_LOGI(GATTS_TAG, "GATT_READ_EVT, read conn_id %d, trans_id %" PRIu32 ", handle %d", param->read.conn_id, param->read.trans_id, param->read.handle);
            esp_gatt_rsp_t rsp;
            memset(&rsp, 0, sizeof(esp_gatt_rsp_t));
            rsp.attr_value.handle = param->read.handle;
            // rsp.attr_value.len = 4;
            // rsp.attr_value.value[0] = 0xde;
            // rsp.attr_value.value[1] = 0xed;
            // rsp.attr_value.value[2] = 0xbe;
            // rsp.attr_value.value[3] = 0xef;
            // const char *wifi = config_network_wifi_get();
            const char *user = config_network_wifi_ssid_get();
            const char *pass = config_network_wifi_password_get();
            const char *macEsp32 = device_mac_get();
            uint8_t ssid[32];
            uint8_t password[64];
            strncpy((char *)ssid, user, 32);
            strncpy((char *)password, pass, 64);
            uint8_t data[256];
            snprintf((char *)rsp.attr_value.value, sizeof(data), "{ \"ssid\": \"%s\",\"password\" : \"%s\", \"macAddress\": \"%s\" }", ssid, password, macEsp32);
            rsp.attr_value.len = sizeof(data);
            ESP_LOGI(GATTS_TAG, "GATT_READ_EVT, read data : %s : %d", rsp.attr_value.value, rsp.attr_value.len);

            // config_network_wifi_ssid_get();
            esp_ble_gatts_send_response(gatts_if, param->read.conn_id, param->read.trans_id,
                                        ESP_GATT_OK, &rsp);
            break;
        }
        case ESP_GATTS_WRITE_EVT:
        {
            ESP_LOGI(GATTS_TAG, "GATT_WRITE_EVT, conn_id %d, trans_id %" PRIu32 ", handle %d", param->write.conn_id, param->write.trans_id, param->write.handle);
            if (!param->write.is_prep)
            {
                ESP_LOGI(GATTS_TAG, "GATT_WRITE_EVT, value len %d, value :", param->write.len);
                // xTaskCreatePinnedToCore(task2, "task2", 8 * 1024, param, 5,
                //                        &task2_t, 1);
                esp_log_buffer_hex(GATTS_TAG, param->write.value, param->write.len);
                ble_disconnect_all();
                ble_scan_stop();
                ESP_ERROR_CHECK_WITHOUT_ABORT(esp_wifi_stop());
                cJSON *wifi = cJSON_Parse((char *)param->write.value);
                cJSON *c_ssid = cJSON_GetObjectItemCaseSensitive(wifi, "ssid");
                cJSON *c_password = cJSON_GetObjectItemCaseSensitive(wifi, "password");

                char *ssid = "";
                if (cJSON_IsString(c_ssid))
                    ssid = c_ssid->valuestring;
                char *password = "";
                if (cJSON_IsString(c_password))
                    password = c_password->valuestring;
                // wifi_config_t wifi_config = {.ap = {.max_connection = 1}};
                // strncpy((char *)wifi_config.ap.ssid, ssid, 32);
                // if (password)
                // {
                //     strncpy((char *)wifi_config.sta.password, password, 64);
                //     wifi_config.ap.authmode = WIFI_AUTH_WPA2_PSK;
                // }
                // else
                //     wifi_config.ap.authmode = WIFI_AUTH_OPEN;
                num_network_disconnections = 0;
                isf = true;
                wifi_disconnect();
                ESP_ERROR_CHECK(esp_wifi_stop());
                ESP_LOGI(GATTS_TAG, "u : %s , p : %s", ssid, password);
                // xTaskCreate(, "network_wifi_add", 2048 * 5, NULL, 4, NULL);
                long h = (unsigned long)esp_get_free_internal_heap_size();
                ESP_LOGI(TAG, "----->>>>>>>>> Heap Free size %lx  <<<<<-----", h);
                network_wifi_add(ssid, password);
                h = (unsigned long)esp_get_free_internal_heap_size();
                ESP_LOGI(TAG, "----->>>>>>>>> Heap Free size %lx  <<<<<-----", h);

                // ShowStatus("กำลังตั้งค่า WIFI");

                isConnect = false;
                wifi_connect(config_network_wifi_ssid_get(), config_network_wifi_password_get(),
                             wifi_eap_atomethod(config_eap_method_get()),
                             config_eap_identity_get(),
                             config_eap_username_get(), config_eap_password_get(),
                             config_eap_ca_cert_get(), config_eap_client_cert_get(),
                             config_eap_client_key_get());
                h = (unsigned long)esp_get_free_internal_heap_size();
                ESP_LOGI(TAG, "----->>>>>>>>> Heap Free size %lx  <<<<<-----", h);

                // esp_restart();
                // ESP_ERROR_CHECK(esp_wifi_stop());
                // ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
                // ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
                // ESP_ERROR_CHECK(esp_wifi_start());
                // char wifi_connecting[256];
                // snprintf(wifi_connecting, sizeof(wifi_connecting), "connecting to WiFi \n \"ssid\": \"%s\" \n\"password\" : \"%s\" ", ssid, password);

                // ESP_LOGI(GATTS_TAG, "%s", wifi_connecting);
                // ESP_ERROR_CHECK(esp_wifi_connect());
                if (gl_profile_tab[PROFILE_A_APP_ID].descr_handle == param->write.handle && param->write.len == 2)
                {
                    uint16_t descr_value = param->write.value[1] << 8 | param->write.value[0];
                    if (descr_value == 0x0001)
                    {
                        if (a_property & ESP_GATT_CHAR_PROP_BIT_NOTIFY)
                        {
                            ESP_LOGI(GATTS_TAG, "notify enable");
                            uint8_t notify_data[15];
                            for (int i = 0; i < sizeof(notify_data); ++i)
                            {
                                notify_data[i] = i % 0xff;
                            }
                            // the size of notify_data[] need less than MTU size
                            esp_ble_gatts_send_indicate(gatts_if, param->write.conn_id, gl_profile_tab[PROFILE_A_APP_ID].char_handle,
                                                        sizeof(notify_data), notify_data, false);
                        }
                    }
                    else if (descr_value == 0x0002)
                    {
                        if (a_property & ESP_GATT_CHAR_PROP_BIT_INDICATE)
                        {
                            ESP_LOGI(GATTS_TAG, "indicate enable");
                            uint8_t indicate_data[15];
                            for (int i = 0; i < sizeof(indicate_data); ++i)
                            {
                                indicate_data[i] = i % 0xff;
                            }
                            // the size of indicate_data[] need less than MTU size
                            esp_ble_gatts_send_indicate(gatts_if, param->write.conn_id, gl_profile_tab[PROFILE_A_APP_ID].char_handle,
                                                        sizeof(indicate_data), indicate_data, true);
                        }
                    }
                    else if (descr_value == 0x0000)
                    {
                        ESP_LOGI(GATTS_TAG, "notify/indicate disable ");
                    }
                    else
                    {
                        ESP_LOGE(GATTS_TAG, "unknown descr value");
                        esp_log_buffer_hex(GATTS_TAG, param->write.value, param->write.len);
                    }
                }
            }
            example_write_event_env(gatts_if, &a_prepare_write_env, param);
            break;
        }
        case ESP_GATTS_EXEC_WRITE_EVT:
            ESP_LOGI(GATTS_TAG, "ESP_GATTS_EXEC_WRITE_EVT");
            esp_ble_gatts_send_response(gatts_if, param->write.conn_id, param->write.trans_id, ESP_GATT_OK, NULL);
            example_exec_write_event_env(&a_prepare_write_env, param);
            break;
        case ESP_GATTS_MTU_EVT:
            ESP_LOGI(GATTS_TAG, "ESP_GATTS_MTU_EVT, MTU %d", param->mtu.mtu);
            break;
        case ESP_GATTS_UNREG_EVT:
            break;
        case ESP_GATTS_CREATE_EVT:
        {
            ESP_LOGI(GATTS_TAG, "CREATE_SERVICE_EVT, status %d,  service_handle %d", param->create.status, param->create.service_handle);
            gl_profile_tab[PROFILE_A_APP_ID].service_handle = param->create.service_handle;
            gl_profile_tab[PROFILE_A_APP_ID].char_uuid.len = ESP_UUID_LEN_16;
            gl_profile_tab[PROFILE_A_APP_ID].char_uuid.uuid.uuid16 = GATTS_CHAR_UUID_TEST_A;

            esp_ble_gatts_start_service(gl_profile_tab[PROFILE_A_APP_ID].service_handle);
            a_property = ESP_GATT_CHAR_PROP_BIT_READ | ESP_GATT_CHAR_PROP_BIT_WRITE | ESP_GATT_CHAR_PROP_BIT_NOTIFY;
            esp_err_t add_char_ret = esp_ble_gatts_add_char(gl_profile_tab[PROFILE_A_APP_ID].service_handle, &gl_profile_tab[PROFILE_A_APP_ID].char_uuid,
                                                            ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE,
                                                            a_property,
                                                            &gatts_demo_char1_val, NULL);
            if (add_char_ret)
            {
                ESP_LOGE(GATTS_TAG, "add char failed, error code =%x", add_char_ret);
            }
            break;
        }
        case ESP_GATTS_ADD_INCL_SRVC_EVT:
            break;
        case ESP_GATTS_ADD_CHAR_EVT:
        {
            uint16_t length = 0;
            const uint8_t *prf_char;

            ESP_LOGI(GATTS_TAG, "ADD_CHAR_EVT, status %d,  attr_handle %d, service_handle %d",
                     param->add_char.status, param->add_char.attr_handle, param->add_char.service_handle);
            gl_profile_tab[PROFILE_A_APP_ID].char_handle = param->add_char.attr_handle;
            gl_profile_tab[PROFILE_A_APP_ID].descr_uuid.len = ESP_UUID_LEN_16;
            gl_profile_tab[PROFILE_A_APP_ID].descr_uuid.uuid.uuid16 = ESP_GATT_UUID_CHAR_CLIENT_CONFIG;
            esp_err_t get_attr_ret = esp_ble_gatts_get_attr_value(param->add_char.attr_handle, &length, &prf_char);
            if (get_attr_ret == ESP_FAIL)
            {
                ESP_LOGE(GATTS_TAG, "ILLEGAL HANDLE");
            }

            ESP_LOGI(GATTS_TAG, "the gatts demo char length = %x", length);
            for (int i = 0; i < length; i++)
            {
                ESP_LOGI(GATTS_TAG, "prf_char[%x] =%x", i, prf_char[i]);
            }
            esp_err_t add_descr_ret = esp_ble_gatts_add_char_descr(gl_profile_tab[PROFILE_A_APP_ID].service_handle, &gl_profile_tab[PROFILE_A_APP_ID].descr_uuid,
                                                                   ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE, NULL, NULL);
            if (add_descr_ret)
            {
                ESP_LOGE(GATTS_TAG, "add char descr failed, error code =%x", add_descr_ret);
            }
            break;
        }
        case ESP_GATTS_ADD_CHAR_DESCR_EVT:
        {
            gl_profile_tab[PROFILE_A_APP_ID].descr_handle = param->add_char_descr.attr_handle;
            ESP_LOGI(GATTS_TAG, "ADD_DESCR_EVT, status %d, attr_handle %d, service_handle %d",
                     param->add_char_descr.status, param->add_char_descr.attr_handle, param->add_char_descr.service_handle);
            break;
        }
        case ESP_GATTS_DELETE_EVT:
            break;
        case ESP_GATTS_START_EVT:
        {
            ESP_LOGI(GATTS_TAG, "SERVICE_START_EVT, status %d, service_handle %d",
                     param->start.status, param->start.service_handle);
            break;
        }
        case ESP_GATTS_STOP_EVT:
            break;
        case ESP_GATTS_CONNECT_EVT:
        {
            esp_ble_conn_update_params_t conn_params = {0};
            memcpy(conn_params.bda, param->connect.remote_bda, sizeof(esp_bd_addr_t));
            /* For the IOS system, please reference the apple official documents about the ble connection parameters restrictions. */
            conn_params.latency = 0;
            conn_params.max_int = 0x20; // max_int = 0x20*1.25ms = 40ms
            conn_params.min_int = 0x10; // min_int = 0x10*1.25ms = 20ms
            conn_params.timeout = 400;  // timeout = 400*10ms = 4000ms
            ESP_LOGI(GATTS_TAG, "ESP_GATTS_CONNECT_EVT, conn_id %d, remote %02x:%02x:%02x:%02x:%02x:%02x:",
                     param->connect.conn_id,
                     param->connect.remote_bda[0], param->connect.remote_bda[1], param->connect.remote_bda[2],
                     param->connect.remote_bda[3], param->connect.remote_bda[4], param->connect.remote_bda[5]);
            gl_profile_tab[PROFILE_A_APP_ID].conn_id = param->connect.conn_id;
            // start sent the update connection parameters to the peer device.
            esp_ble_gap_update_conn_params(&conn_params);
            // wifi_disconnect();
            // ESP_ERROR_CHECK(esp_wifi_stop());

            // ShowStatus("กำลังตั้งค่า WIFI");
            break;
        }
        case ESP_GATTS_DISCONNECT_EVT:
        {
            ESP_LOGI(GATTS_TAG, "ESP_GATTS_DISCONNECT_EVT, disconnect reason 0x%x", param->disconnect.reason);
            esp_ble_gap_start_advertising(&adv_params);
            // wifi_reconnect();
            // wifi_connect(config_network_wifi_ssid_get(), config_network_wifi_password_get(),
            //              wifi_eap_atomethod(config_eap_method_get()),
            //              config_eap_identity_get(),
            //              config_eap_username_get(), config_eap_password_get(),
            //              config_eap_ca_cert_get(), config_eap_client_cert_get(),
            //              config_eap_client_key_get());
            break;
        }
        case ESP_GATTS_CONF_EVT:
        {
            ESP_LOGI(GATTS_TAG, "ESP_GATTS_CONF_EVT, status %d attr_handle %d", param->conf.status, param->conf.handle);
            if (param->conf.status != ESP_GATT_OK)
            {
                esp_log_buffer_hex(GATTS_TAG, param->conf.value, param->conf.len);
            }
            break;
        }
        case ESP_GATTS_OPEN_EVT:
        case ESP_GATTS_CANCEL_OPEN_EVT:
        case ESP_GATTS_CLOSE_EVT:
        case ESP_GATTS_LISTEN_EVT:
        case ESP_GATTS_CONGEST_EVT:
        default:
            break;
        }
    }

    static void gatts_profile_b_event_handler(esp_gatts_cb_event_t event, esp_gatt_if_t gatts_if, esp_ble_gatts_cb_param_t *param)
    {
        switch (event)
        {
        case ESP_GATTS_REG_EVT:
            ESP_LOGI(GATTS_TAG, "REGISTER_APP_EVT, status %d, app_id %d", param->reg.status, param->reg.app_id);
            gl_profile_tab[PROFILE_B_APP_ID].service_id.is_primary = true;
            gl_profile_tab[PROFILE_B_APP_ID].service_id.id.inst_id = 0x00;
            gl_profile_tab[PROFILE_B_APP_ID].service_id.id.uuid.len = ESP_UUID_LEN_16;
            gl_profile_tab[PROFILE_B_APP_ID].service_id.id.uuid.uuid.uuid16 = GATTS_SERVICE_UUID_TEST_B;

            esp_ble_gatts_create_service(gatts_if, &gl_profile_tab[PROFILE_B_APP_ID].service_id, GATTS_NUM_HANDLE_TEST_B);
            break;
        case ESP_GATTS_READ_EVT:
        {
            ESP_LOGI(GATTS_TAG, "GATT_READ_EVT, conn_id %d, trans_id %" PRIu32 ", handle %d", param->read.conn_id, param->read.trans_id, param->read.handle);
            esp_gatt_rsp_t rsp;
            memset(&rsp, 0, sizeof(esp_gatt_rsp_t));
            rsp.attr_value.handle = param->read.handle;
            rsp.attr_value.len = 4;
            rsp.attr_value.value[0] = 0xde;
            rsp.attr_value.value[1] = 0xed;
            rsp.attr_value.value[2] = 0xbe;
            rsp.attr_value.value[3] = 0xef;
            esp_ble_gatts_send_response(gatts_if, param->read.conn_id, param->read.trans_id,
                                        ESP_GATT_OK, &rsp);
            break;
        }
        case ESP_GATTS_WRITE_EVT:
        {
            ESP_LOGI(GATTS_TAG, "GATT_WRITE_EVT, conn_id %d, trans_id %" PRIu32 ", handle %d", param->write.conn_id, param->write.trans_id, param->write.handle);
            if (!param->write.is_prep)
            {
                ESP_LOGI(GATTS_TAG, "GATT_WRITE_EVT, value len %d, value :", param->write.len);
                esp_log_buffer_hex(GATTS_TAG, param->write.value, param->write.len);
                // ESP_ERROR_CHECK(esp_wifi_stop());
                //  ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
                //  ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
                //  ESP_ERROR_CHECK(esp_wifi_start());
                //  char wifi_connecting[256];
                //  snprintf(wifi_connecting, sizeof(wifi_connecting), "connecting to WiFi \n \"ssid\": \"%s\" \n\"password\" : \"%s\" ", ssid, password);

                // ESP_LOGI(GATTS_TAG, "%s", wifi_connecting);
                // ESP_ERROR_CHECK(esp_wifi_connect());
                wifi_disconnect();
                ESP_ERROR_CHECK(esp_wifi_stop());

                // ShowStatus("กำลังตั้งค่า WIFI");
                isConnect = false;

                wifi_connect(config_network_wifi_ssid_get(), config_network_wifi_password_get(),
                             wifi_eap_atomethod(config_eap_method_get()),
                             config_eap_identity_get(),
                             config_eap_username_get(), config_eap_password_get(),
                             config_eap_ca_cert_get(), config_eap_client_cert_get(),
                             config_eap_client_key_get());
                if (gl_profile_tab[PROFILE_B_APP_ID].descr_handle == param->write.handle && param->write.len == 2)
                {
                    uint16_t descr_value = param->write.value[1] << 8 | param->write.value[0];
                    if (descr_value == 0x0001)
                    {
                        if (b_property & ESP_GATT_CHAR_PROP_BIT_NOTIFY)
                        {
                            ESP_LOGI(GATTS_TAG, "notify enable");
                            uint8_t notify_data[15];
                            for (int i = 0; i < sizeof(notify_data); ++i)
                            {
                                notify_data[i] = i % 0xff;
                            }
                            // the size of notify_data[] need less than MTU size
                            esp_ble_gatts_send_indicate(gatts_if, param->write.conn_id, gl_profile_tab[PROFILE_B_APP_ID].char_handle,
                                                        sizeof(notify_data), notify_data, false);
                        }
                    }
                    else if (descr_value == 0x0002)
                    {
                        if (b_property & ESP_GATT_CHAR_PROP_BIT_INDICATE)
                        {
                            ESP_LOGI(GATTS_TAG, "indicate enable");
                            uint8_t indicate_data[15];
                            for (int i = 0; i < sizeof(indicate_data); ++i)
                            {
                                indicate_data[i] = i % 0xff;
                            }
                            // the size of indicate_data[] need less than MTU size
                            esp_ble_gatts_send_indicate(gatts_if, param->write.conn_id, gl_profile_tab[PROFILE_B_APP_ID].char_handle,
                                                        sizeof(indicate_data), indicate_data, true);
                        }
                    }
                    else if (descr_value == 0x0000)
                    {
                        ESP_LOGI(GATTS_TAG, "notify/indicate disable ");
                    }
                    else
                    {
                        ESP_LOGE(GATTS_TAG, "unknown value");
                    }
                }
            }
            example_write_event_env(gatts_if, &b_prepare_write_env, param);
            break;
        }
        case ESP_GATTS_EXEC_WRITE_EVT:
        {
            ESP_LOGI(GATTS_TAG, "ESP_GATTS_EXEC_WRITE_EVT");
            esp_ble_gatts_send_response(gatts_if, param->write.conn_id, param->write.trans_id, ESP_GATT_OK, NULL);
            example_exec_write_event_env(&b_prepare_write_env, param);
            break;
        }
        case ESP_GATTS_MTU_EVT:
        {
            ESP_LOGI(GATTS_TAG, "ESP_GATTS_MTU_EVT, MTU %d", param->mtu.mtu);
            break;
        }
        case ESP_GATTS_UNREG_EVT:
            break;
        case ESP_GATTS_CREATE_EVT:
        {
            ESP_LOGI(GATTS_TAG, "CREATE_SERVICE_EVT, status %d,  service_handle %d", param->create.status, param->create.service_handle);
            gl_profile_tab[PROFILE_B_APP_ID].service_handle = param->create.service_handle;
            gl_profile_tab[PROFILE_B_APP_ID].char_uuid.len = ESP_UUID_LEN_16;
            gl_profile_tab[PROFILE_B_APP_ID].char_uuid.uuid.uuid16 = GATTS_CHAR_UUID_TEST_B;

            esp_ble_gatts_start_service(gl_profile_tab[PROFILE_B_APP_ID].service_handle);
            b_property = ESP_GATT_CHAR_PROP_BIT_READ | ESP_GATT_CHAR_PROP_BIT_WRITE | ESP_GATT_CHAR_PROP_BIT_NOTIFY;
            esp_err_t add_char_ret = esp_ble_gatts_add_char(gl_profile_tab[PROFILE_B_APP_ID].service_handle, &gl_profile_tab[PROFILE_B_APP_ID].char_uuid,
                                                            ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE,
                                                            b_property,
                                                            NULL, NULL);
            if (add_char_ret)
            {
                ESP_LOGE(GATTS_TAG, "add char failed, error code =%x", add_char_ret);
            }
            break;
        }
        case ESP_GATTS_ADD_INCL_SRVC_EVT:
            break;
        case ESP_GATTS_ADD_CHAR_EVT:
            ESP_LOGI(GATTS_TAG, "ADD_CHAR_EVT, status %d,  attr_handle %d, service_handle %d",
                     param->add_char.status, param->add_char.attr_handle, param->add_char.service_handle);

            gl_profile_tab[PROFILE_B_APP_ID].char_handle = param->add_char.attr_handle;
            gl_profile_tab[PROFILE_B_APP_ID].descr_uuid.len = ESP_UUID_LEN_16;
            gl_profile_tab[PROFILE_B_APP_ID].descr_uuid.uuid.uuid16 = ESP_GATT_UUID_CHAR_CLIENT_CONFIG;
            esp_ble_gatts_add_char_descr(gl_profile_tab[PROFILE_B_APP_ID].service_handle, &gl_profile_tab[PROFILE_B_APP_ID].descr_uuid,
                                         ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE,
                                         NULL, NULL);
            break;
        case ESP_GATTS_ADD_CHAR_DESCR_EVT:
            gl_profile_tab[PROFILE_B_APP_ID].descr_handle = param->add_char_descr.attr_handle;
            ESP_LOGI(GATTS_TAG, "ADD_DESCR_EVT, status %d, attr_handle %d, service_handle %d",
                     param->add_char_descr.status, param->add_char_descr.attr_handle, param->add_char_descr.service_handle);
            break;
        case ESP_GATTS_DELETE_EVT:
            break;
        case ESP_GATTS_START_EVT:
            ESP_LOGI(GATTS_TAG, "SERVICE_START_EVT, status %d, service_handle %d",
                     param->start.status, param->start.service_handle);
            break;
        case ESP_GATTS_STOP_EVT:
            break;
        case ESP_GATTS_CONNECT_EVT:
            ESP_LOGI(GATTS_TAG, "CONNECT_EVT, conn_id %d, remote %02x:%02x:%02x:%02x:%02x:%02x:",
                     param->connect.conn_id,
                     param->connect.remote_bda[0], param->connect.remote_bda[1], param->connect.remote_bda[2],
                     param->connect.remote_bda[3], param->connect.remote_bda[4], param->connect.remote_bda[5]);
            gl_profile_tab[PROFILE_B_APP_ID].conn_id = param->connect.conn_id;
            break;
        case ESP_GATTS_CONF_EVT:
            ESP_LOGI(GATTS_TAG, "ESP_GATTS_CONF_EVT status %d attr_handle %d", param->conf.status, param->conf.handle);
            if (param->conf.status != ESP_GATT_OK)
            {
                esp_log_buffer_hex(GATTS_TAG, param->conf.value, param->conf.len);
            }
            break;
        case ESP_GATTS_DISCONNECT_EVT:
        case ESP_GATTS_OPEN_EVT:
        case ESP_GATTS_CANCEL_OPEN_EVT:
        case ESP_GATTS_CLOSE_EVT:
        case ESP_GATTS_LISTEN_EVT:
        case ESP_GATTS_CONGEST_EVT:
        default:
            break;
        }
    }
    static void gatts_event_handler(esp_gatts_cb_event_t event, esp_gatt_if_t gatts_if, esp_ble_gatts_cb_param_t *param)
    {
        /* If event is register event, store the gatts_if for each profile */
        if (event == ESP_GATTS_REG_EVT)
        {
            if (param->reg.status == ESP_GATT_OK)
            {
                gl_profile_tab[param->reg.app_id].gatts_if = gatts_if;
            }
            else
            {
                ESP_LOGI(GATTS_TAG, "Reg app failed, app_id %04x, status %d",
                         param->reg.app_id,
                         param->reg.status);
                return;
            }
        }

        /* If the gatts_if equal to profile A, call profile A cb handler,
         * so here call each profile's callback */
        do
        {
            int idx;
            for (idx = 0; idx < PROFILE_NUM; idx++)
            {
                if (gatts_if == ESP_GATT_IF_NONE || /* ESP_GATT_IF_NONE, not specify a certain gatt_if, need to call every profile cb function */
                    gatts_if == gl_profile_tab[idx].gatts_if)
                {
                    if (gl_profile_tab[idx].gatts_cb)
                    {
                        gl_profile_tab[idx].gatts_cb(event, gatts_if, param);
                    }
                }
            }
        } while (0);
    }

    /* Bookkeeping functions */

    static void self_publish_status(void)
    {

        // const char *macEsp32 = device_mac_get();
        // char payload2[700] = "";
        size_t i, number_of_devices;
        cJSON *response = cJSON_CreateArray();
        ble_dev_t *devices = ble_devices_list_get(&number_of_devices);
        int countConnect = 0;
        for (i = 0; i < number_of_devices; i++)
        {
            if (devices[i].connected)
            {

                ESP_LOGI(TAG, "----->>>>>>>>> Device List : %s   <<<<<-----", devices[i].name);
                countConnect++;
                cJSON *object = cJSON_CreateObject();
                const char *MAC = mactoa(devices[i].mac);
                cJSON_AddStringToObject(object, "deviceUUID", MAC);
                if (*devices[i].name)
                    cJSON_AddStringToObject(object, "deviceName", devices[i].name);
                else
                    cJSON_AddNullToObject(object, "deviceName");
                cJSON_AddItemToArray(response, object);
            }
            else if (devices[i].name[0] != '\0')
            {

                ESP_LOGI(TAG, "----->>>>>>>>> Device List : %s  not connect <<<<<-----", devices[i].name);
            }
        }
        // char *printdevicelist = cJSON_PrintUnformatted(response);
        char *response_str = cJSON_PrintUnformatted(response);
        char payload2[700] = "";
        uint8_t *mac = wifi_mac_get();
        char macEsp32[24];
        sprintf(macEsp32, "%02x:%02x:%02x:%02x:%02x:%02x", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
        snprintf(payload2, 700, "{"
                                " \"platform\":  \"esp32\","
                                "\"os\": \"esp32\","
                                "\"appVersion\": \"1\","
                                "\"lang\": \"th\","
                                "\"ipAddress\": \"%s\","
                                "\"macAddress\": \"%s\","
                                "\"thermometer\": %s"
                                "}",
                 macEsp32, macEsp32, response_str);
        // ESP_LOGI(TAG, "----->>>>>>>>> ESPStatus : %d | %s  <<<<<-----", countConnect, payload2);

        cJSON_free(response_str);
        cJSON_Delete(response);
        ble_devices_list_free(devices);
        // snprintf(topic, MAX_TOPIC_LEN, "/ESPStatus", device_name_get());
        mqtt_publish(ESPStatustopic, (uint8_t *)payload2, strlen(payload2),
                     config_mqtt_qos_get(), config_mqtt_retained_get());
        // free(payload2);
    }
    static void uptime_publish(void)
    {
        char topic[MAX_TOPIC_LEN];
        char buf[16];

        /* Only publish uptime when connected, we don't want it to be queued */
        if (!mqtt_is_connected())
            return;

        // /* Uptime (in seconds) */
        // sprintf(buf, "%" PRId64, esp_timer_get_time() / 1000 / 1000);
        // snprintf(topic, MAX_TOPIC_LEN, "%s/Uptime", device_name_get());
        // mqtt_publish(topic, (uint8_t *)buf, strlen(buf), config_mqtt_qos_get(),
        //              config_mqtt_retained_get());

        /* Free memory (in bytes) */
        sprintf(buf, "%" PRIu32, esp_get_free_heap_size());
        ESP_LOGI(TAG, "----->>>>>>>>> FreeMemory size %s  <<<<<-----", buf);
        snprintf(topic, MAX_TOPIC_LEN, "%s/FreeMemory", device_name_get());
        mqtt_publish(topic, (uint8_t *)buf, strlen(buf), config_mqtt_qos_get(),
                     config_mqtt_retained_get());
        self_publish_status();
    }
    static void self_publish(void)
    {
        char topic[MAX_TOPIC_LEN];
        char *payload;

        /* Current status */
        payload = "Online";
        snprintf(topic, MAX_TOPIC_LEN, "%s/Status", device_name_get());
        mqtt_publish(topic, (uint8_t *)payload, strlen(payload),
                     config_mqtt_qos_get(), config_mqtt_retained_get());

        /* App version */
        payload = BLE2MQTT_VER;
        snprintf(topic, MAX_TOPIC_LEN, "%s/Version", device_name_get());
        mqtt_publish(topic, (uint8_t *)payload, strlen(payload),
                     config_mqtt_qos_get(), config_mqtt_retained_get());

        /* Config version */
        payload = config_version_get();
        snprintf(topic, MAX_TOPIC_LEN, "%s/ConfigVersion", device_name_get());
        mqtt_publish(topic, (uint8_t *)payload, strlen(payload),
                     config_mqtt_qos_get(), config_mqtt_retained_get());

        uptime_publish();
    }

    /* Management functions */
    static void management_on_restart_mqtt(const char *topic,
                                           const uint8_t *payload, size_t len, void *ctx)
    {
        if (len != 4 || strncmp((char *)payload, "true", len))
            return;

        abort();
    }

    static void _management_on_restart_mqtt(const char *topic,
                                            const uint8_t *payload, size_t len, void *ctx);

    static void management_subscribe(void)
    {
        char topic[MAX_TOPIC_LEN];

        snprintf(topic, MAX_TOPIC_LEN, "%s/Restart", device_name_get());
        mqtt_subscribe(topic, 0, _management_on_restart_mqtt, NULL, NULL);
        mqtt_subscribe("BLE2MQTT/Restart", 0, _management_on_restart_mqtt, NULL,
                       NULL);
    }

    static void management_unsubscribe(void)
    {
        char topic[MAX_TOPIC_LEN];

        snprintf(topic, MAX_TOPIC_LEN, "%s/Restart", device_name_get());
        mqtt_unsubscribe(topic);
        mqtt_unsubscribe("BLE2MQTT/Restart");
    }

    static void cleanup(void)
    {
        // ble_disconnect_all();
        // ble_scan_stop();
        management_unsubscribe();
    }

    /* Network callback functions */
    static void network_on_connecting(void)
    {
        char status_topic[MAX_TOPIC_LEN];
        // printText(0x000000u, 0xffffffu, "connecting ..");
        snprintf(status_topic, MAX_TOPIC_LEN, "กำลังเชื่อมต่อ Wifi : %s", config_network_wifi_ssid_get());
        ShowStatus(status_topic, 0xfcdce2, 0xffffff);
        char status_topic_storage[100];
        strcpy(status_topic_storage,status_topic);
        last_message = strdup(status_topic_storage);
    }
    static void network_on_connected(void)
    {
        isf = false;
        isConnect = true;
        char status_topic[MAX_TOPIC_LEN];
        char showText[MAX_TOPIC_LEN];
        snprintf(showText, MAX_TOPIC_LEN, "เชื่อมต่อ Wifi : %s", config_network_wifi_ssid_get());
        ShowStatus(showText, 0xffff00, 0x000000);
        last_message = showText;
        log_start(config_log_host_get(), config_log_port_get());
        ESP_LOGI(TAG, "Connected to the network, connecting to MQTT");
        snprintf(status_topic, MAX_TOPIC_LEN, "%s/Status", device_name_get());

        mqtt_connect(config_mqtt_host_get(), config_mqtt_port_get(),
                     config_mqtt_client_id_get(), config_mqtt_username_get(),
                     config_mqtt_password_get(), config_mqtt_ssl_get(),
                     config_mqtt_server_cert_get(), config_mqtt_client_cert_get(),
                     config_mqtt_client_key_get(), status_topic, "Offline",
                     config_mqtt_qos_get(), config_mqtt_retained_get());
    }

    static void network_on_disconnected(void)
    {
        isConnect = false;
        char showText[MAX_TOPIC_LEN];
        snprintf(showText, MAX_TOPIC_LEN, "ไม่สามารถเชื่อมต่อ Wifi : %s", config_network_wifi_ssid_get());
        ShowStatus(showText, 0xff0000, 0xffffff);
        last_message = showText;
        log_stop();
        ESP_LOGI(TAG, "Disconnected from the network");
        // mqtt_disconnect();
        /* We don't get notified when manually stopping MQTT */
        cleanup();
        if (isf)
        {
            ESP_LOGI(TAG, "wifi_reconnect : %d", num_network_disconnections);
            if (num_network_disconnections < 3)
            {
                num_network_disconnections = num_network_disconnections + 1;
                vTaskDelay(3000 / portTICK_PERIOD_MS);
                wifi_reconnect();
            }
            else
            {

                // isf = false; // ถ้าต้องการให้ลองหลายครั้งเอาออกได้เลย
            }
        }
    }

    /* MQTT callback functions */
    static void mqtt_on_connected(void)
    {
        char showText[MAX_TOPIC_LEN];
        snprintf(showText, MAX_TOPIC_LEN, "เชื่อมต่อ Wifi : %s , และเชื่อมต่อกับ Server แล้ว", config_network_wifi_ssid_get());
        ShowStatus(showText, 0x00ff00, 0x000000);
        last_message = showText;
        ESP_LOGI(TAG, "Connected to MQTT, scanning for BLE devices");
        self_publish();
        management_subscribe();
        ble_scan_start();
    }

    static void mqtt_on_disconnected(void)
    {
        ESP_LOGI(TAG, "Disconnected from MQTT, stopping BLE");
        static uint8_t num_disconnections = 0;
        cleanup();

        if (!isAPMode)
        {
            char showText[MAX_TOPIC_LEN];
            if (isConnect)
            {

                snprintf(showText, MAX_TOPIC_LEN, "เชื่อมต่อ Wifi : %s , และไม่สามารถเชื่อมต่อ Server", config_network_wifi_ssid_get());
                ShowStatus(showText, 0xffa500, 0x000000);
            }
            else
            {

                snprintf(showText, MAX_TOPIC_LEN, "ไม่สามารถเชื่อมต่อ Wifi : %s", config_network_wifi_ssid_get());
                ShowStatus(showText, 0xff0000, 0xffffff);
                static char message_storage[100];
                strcpy(message_storage,showText);
                last_message = message_storage;
            }
            ESP_LOGI(TAG, "%s", showText);
            static char message_storage[100];
            strcpy(message_storage,showText);
            last_message = message_storage;
            
            // cleanup();

            if (++num_disconnections % 3 == 0)
            {
                ESP_LOGI(TAG,
                         "Failed connecting to MQTT 5 times, reconnecting to the network");
                ShowStatus("ไม่สามารถเชื่อมต่อ Server 5 ครั้ง กำลังเชื่อมต่อ Wifi", 0xffa500, 0x000000);
                isConnect = false;
                wifi_reconnect();
            }
        }
    }

    /* BLE functions */
    static void ble_on_mqtt_connected_cb(const char *topic, const uint8_t *payload,
                                         size_t len, void *ctx)
    {
        char new_topic[MAX_TOPIC_LEN];

        if (len == 4 && !strncmp((char *)payload, "true", len))
            return;

        /* Someone published our device is disconnected, set them straight */
        snprintf(new_topic, MAX_TOPIC_LEN, "%s%s/Connected",
                 config_mqtt_prefix_get(), (char *)ctx);
        mqtt_publish(new_topic, (uint8_t *)"true", 4, config_mqtt_qos_get(),
                     config_mqtt_retained_get());
    }

    static void _ble_on_mqtt_connected_cb(const char *topic, const uint8_t *payload,
                                          size_t len, void *ctx);

    static void ble_publish_connected(mac_addr_t mac, uint8_t is_connected)
    {
        char topic[MAX_TOPIC_LEN];

        snprintf(topic, MAX_TOPIC_LEN, "%s" MAC_FMT "/Connected",
                 config_mqtt_prefix_get(), MAC_PARAM(mac));

        if (!is_connected)
            mqtt_unsubscribe(topic);

        mqtt_publish(topic, (uint8_t *)(is_connected ? "true" : "false"),
                     is_connected ? 4 : 5, config_mqtt_qos_get(),
                     config_mqtt_retained_get());

        if (is_connected)
        {
            const char *device_name = device_name_get();

            /* Subscribe for other devices claiming this device is disconnected */
            mqtt_subscribe(topic, config_mqtt_qos_get(), _ble_on_mqtt_connected_cb,
                           strdup(mactoa(mac)), free);
            /* We are now the owner of this device */
            snprintf(topic, MAX_TOPIC_LEN, "%s" MAC_FMT "/Owner",
                     config_mqtt_prefix_get(), MAC_PARAM(mac));
            mqtt_publish(topic, (uint8_t *)device_name, strlen(device_name),
                         config_mqtt_qos_get(), config_mqtt_retained_get());
        }
    }

    static mqtt_ctx_t *ble_ctx_gen(mac_addr_t mac, ble_uuid_t service,
                                   ble_uuid_t characteristic, uint8_t index)
    {
        mqtt_ctx_t *ctx = (mqtt_ctx_t *)malloc(sizeof(mqtt_ctx_t));

        memcpy(ctx->mac, mac, sizeof(mac_addr_t));
        memcpy(ctx->service, service, sizeof(ble_uuid_t));
        memcpy(ctx->characteristic, characteristic, sizeof(ble_uuid_t));
        ctx->index = index;

        return ctx;
    }

    /* BLE callback functions */
    static void ble_on_broadcaster_metadata(char *name, char *val, void *ctx)
    {
        char topic[MAX_TOPIC_LEN];

        sprintf(topic, "%s/%s/%s", device_name_get(), (char *)ctx, name);
        /* Broadcaster topics shouldn't be retained */
        // mqtt_publish(topic, (uint8_t *)val, strlen(val), config_mqtt_qos_get(), 0);
    }

    static void ble_on_broadcaster_discovered(mac_addr_t mac, uint8_t *adv_data,
                                              size_t adv_data_len, int rssi, broadcaster_ops_t *ops)
    {
        char *mac_str = strdup(mactoa(mac));
        char rssi_str[6];
        ESP_LOGI(TAG, "Discovered %s broadcaster", ops->name);

        ble_on_broadcaster_metadata("Type", ops->name, mac_str);
        sprintf(rssi_str, "%d", rssi);
        ble_on_broadcaster_metadata("RSSI", rssi_str, mac_str);
        ops->metadata_get(adv_data, adv_data_len, rssi, ble_on_broadcaster_metadata,
                          mac_str);

        free(mac_str);
    }

    static void ble_on_device_discovered(mac_addr_t mac, char *name, int rssi)
    {
        // ble_device_t *device = get_ble_device(mac);
        // char *name = ble_device_name_get2(mac);
        // ESP_LOGI(TAG, "%s", name);
        // uint8_t connect = config_ble_should_connect(mactoa(mac));
        if (name)
        {
            ESP_LOGI(TAG, "name : %s", name);
        }

        uint8_t connect = config_ble_should_connect2(name);

        ESP_LOGI(TAG, "Discovered BLE device: " MAC_FMT " (RSSI: %d), %sconnecting",
                 MAC_PARAM(mac), rssi, connect ? "" : "not ");
        // else
        // {

        //     ESP_LOGI(TAG, "name : none");
        // }
        // if (device)
        // {
        // if (device->name)
        // {
        //     // ESP_LOGI(TAG, "name : %s", device->name);
        // }
        // }
        if (!connect)
            return;

        ble_connect(mac);
    }

    static void ble_on_device_connected(mac_addr_t mac)
    {
        // int listDevice = json_is_in_lists_prefix();
        // ShowNumDevice(listDevice);
        ESP_LOGI(TAG, "Connected to device: " MAC_FMT ", scanning",
                 MAC_PARAM(mac));
        ble_publish_connected(mac, 1);
        ble_services_scan(mac);
    }

    static char *ble_topic_suffix(char *base, uint8_t is_get)
    {
        static char topic[MAX_TOPIC_LEN];

        sprintf(topic, "%s%s", base, is_get ? config_mqtt_get_suffix_get() : config_mqtt_set_suffix_get());

        return topic;
    }

    static char *ble_topic(mac_addr_t mac, ble_uuid_t service_uuid,
                           ble_uuid_t characteristic_uuid, uint8_t index)
    {
        static char topic[MAX_TOPIC_LEN];
        int i = 0;

        i += snprintf(topic + i, MAX_TOPIC_LEN, "%s" MAC_FMT "/%s",
                      config_mqtt_prefix_get(), MAC_PARAM(mac),
                      ble_service_name_get(service_uuid));
        i += snprintf(topic + i, MAX_TOPIC_LEN - i, "/%s",
                      ble_characteristic_name_get(characteristic_uuid));
        // uint8_t json_is_in_lists_prefix();
        if (index > 0)
            i += snprintf(topic + i, MAX_TOPIC_LEN - i, "_%u", index);

        return topic;
    }

    static void ble_on_device_disconnected(mac_addr_t mac)
    {
        // int listDevice = json_is_in_lists_prefix();
        // ShowNumDevice(listDevice);
        char topic[MAX_TOPIC_LEN];

        ESP_LOGI(TAG, "Disconnected from device: " MAC_FMT, MAC_PARAM(mac));
        ble_publish_connected(mac, 0);
        snprintf(topic, MAX_TOPIC_LEN, "%s" MAC_FMT "/",
                 config_mqtt_prefix_get(), MAC_PARAM(mac));
        mqtt_unsubscribe_topic_prefix(topic);
    }

    static void ble_on_mqtt_get(const char *topic, const uint8_t *payload,
                                size_t len, void *ctx)
    {
        ESP_LOGD(TAG, "Got read request: %s", topic);
        mqtt_ctx_t *data = (mqtt_ctx_t *)ctx;

        ble_characteristic_read(data->mac, data->service, data->characteristic,
                                data->index);
    }

    static void ble_on_mqtt_set(const char *topic, const uint8_t *payload,
                                size_t len, void *ctx)
    {
        ESP_LOGD(TAG, "Got write request: %s, len: %u", topic, len);
        mqtt_ctx_t *data = (mqtt_ctx_t *)ctx;
        size_t buf_len;
        uint8_t *buf = atochar(data->characteristic, (const char *)payload,
                               len, &buf_len);

        ble_characteristic_write(data->mac, data->service, data->characteristic,
                                 data->index, buf, buf_len);

        /* Issue a read request to get latest value */
        ble_characteristic_read(data->mac, data->service, data->characteristic,
                                data->index);
    }

    static void _ble_on_mqtt_get(const char *topic, const uint8_t *payload,
                                 size_t len, void *ctx);
    static void _ble_on_mqtt_set(const char *topic, const uint8_t *payload,
                                 size_t len, void *ctx);

    static void ble_on_characteristic_found(mac_addr_t mac, ble_uuid_t service_uuid,
                                            ble_uuid_t characteristic_uuid, uint8_t index, uint8_t properties)
    {
        ESP_LOGD(TAG, "Found new characteristic: service: " UUID_FMT ", characteristic: " UUID_FMT ", index: %u, properties: 0x%x",
                 UUID_PARAM(service_uuid), UUID_PARAM(characteristic_uuid), index,
                 properties);
        char *topic;

        if (!config_ble_service_should_include(uuidtoa(service_uuid)) ||
            !config_ble_characteristic_should_include(uuidtoa(characteristic_uuid)))
        {
            return;
        }

        topic = ble_topic(mac, service_uuid, characteristic_uuid, index);

        /* Characteristic is readable */
        if (properties & CHAR_PROP_READ)
        {
            mqtt_subscribe(ble_topic_suffix(topic, 1), config_mqtt_qos_get(),
                           _ble_on_mqtt_get, ble_ctx_gen(mac, service_uuid, characteristic_uuid, index), free);
            ble_characteristic_read(mac, service_uuid, characteristic_uuid, index);
        }

        /* Characteristic is writable */
        if (properties & (CHAR_PROP_WRITE | CHAR_PROP_WRITE_NR))
        {
            mqtt_subscribe(ble_topic_suffix(topic, 0), config_mqtt_qos_get(),
                           _ble_on_mqtt_set, ble_ctx_gen(mac, service_uuid, characteristic_uuid, index), free);
        }

        /* Characteristic can notify / indicate on changes */
        if (properties & (CHAR_PROP_NOTIFY | CHAR_PROP_INDICATE))
        {
            ble_characteristic_notify_register(mac, service_uuid,
                                               characteristic_uuid, index);
        }
    }

    static void ble_on_device_services_discovered(mac_addr_t mac)
    {
        ESP_LOGD(TAG, "Services discovered on device: " MAC_FMT, MAC_PARAM(mac));
        ble_foreach_characteristic(mac, ble_on_characteristic_found);
    }
    static char *get_characteristic(ble_uuid_t characteristic_uuid)
    {
        static char topic[MAX_TOPIC_LEN];
        int i = 0;

        snprintf(topic + i, MAX_TOPIC_LEN - i, "%s",
                 ble_characteristic_name_get(characteristic_uuid));

        return topic;
    }

    static void ble_on_device_characteristic_value(mac_addr_t mac,
                                                   ble_uuid_t service, ble_uuid_t characteristic, uint8_t index,
                                                   uint8_t *value, size_t value_len)
    {
        char *topic = "/thermov2"; //
        char *raw = ble_topic(mac, service, characteristic, index);
        char *payload = chartoa(characteristic, value, value_len);
        char *characteristicUUID = get_characteristic(characteristic);
        // const char *characteristicUUID = ble_characteristic_name_get(characteristic);
        // ESP_LOGI(TAG, "Publishing: %s = %s ", topic, payload);

        if (strcmp("0000fe10-0000-1000-8000-00805f9b34fb", characteristicUUID) == 0)
        {
            // ESP_LOGI(TAG, "Publishing: %s = %s | %s ", raw, payload, characteristicUUID);
            long h = (unsigned long)esp_get_free_internal_heap_size();
            ESP_LOGI(TAG, "----->>>>>>>>> Heap Free size %lx  <<<<<-----", h);
            char *macThermo = get_mac_thermo(mac);
            const char *macEsp32 = device_mac_get();
            ShowTempNone();
            char *sensorMode = "";
            uint8_t tempPre = 0X00;
            uint8_t tempSuf = 0x00;
            uint16_t temp = 0x0000;

            if (value_len == 15)
            {
                if (value[9] == 0)
                {
                    tempPre = (uint8_t)value[6];
                    tempSuf = (uint8_t)value[5];
                    sensorMode = "body";
                }
                if (value[9] == 1)
                {
                    tempPre = (uint8_t)value[4];
                    tempSuf = (uint8_t)value[3];
                    sensorMode = "surface";
                }
                if (value[9] == 2)
                {
                    tempPre = (uint8_t)value[8];
                    tempSuf = (uint8_t)value[7];
                    sensorMode = "room";
                }
                temp = (tempPre << 8) | (tempSuf);
                float tempF = temp / 10.0;
                char payload2[700] = "pre_";

                snprintf(payload2, 700, "{"
                                        " \"platform\":  \"esp32\","
                                        "\"os\": \"esp32\","
                                        "\"appVersion\": \"1\","
                                        "\"lang\": \"th\","
                                        "\"ipAddress\": \"%s\","
                                        "\"macAddress\": \"%s\","
                                        "\"heapFreeSize\": \"%lx\","
                                        "\"tempValue\": %.1f"
                                        "}",
                         macThermo, macEsp32, h, tempF);
                ShowTemp((double)tempF);
                size_t payload_len = strlen(payload2);
                ESP_LOGI(TAG, "Publishing2: %s = %s", topic, payload2);
                mqtt_publish(topic, (uint8_t *)payload2, payload_len, config_mqtt_qos_get(),
                             config_mqtt_retained_get());
            }
        }
    }

    static void ble_on_device_characteristic_value2(mac_addr_t mac,
                                                    ble_uuid_t service, ble_uuid_t characteristic, uint8_t index,
                                                    uint8_t *value, size_t value_len)
    {
        char *topic = ble_topic(mac, service, characteristic, index);
        char *payload = chartoa(characteristic, value, value_len);
        size_t payload_len = strlen(payload);

        ESP_LOGI(TAG, "Publishing: %s = %s", topic, payload);
        mqtt_publish(topic, (uint8_t *)payload, payload_len, config_mqtt_qos_get(),
                     config_mqtt_retained_get());
    }

    static uint32_t ble_on_passkey_requested(mac_addr_t mac)
    {
        char *s = mactoa(mac);
        uint32_t passkey = config_ble_passkey_get(s);

        ESP_LOGI(TAG, "Initiating pairing with %s using the passkey %" PRIu32, s,
                 passkey);

        return passkey;
    }

    /* BLE2MQTT Task and event callbacks */
    typedef enum
    {
        EVENT_TYPE_HEARTBEAT_TIMER,
        EVENT_TYPE_NETWORK_CONNECTED,
        EVENT_TYPE_NETWORK_DISCONNECTED,
        EVENT_TYPE_OTA_MQTT,
        EVENT_TYPE_OTA_COMPLETED,
        EVENT_TYPE_MANAGEMENT_RESTART_MQTT,
        EVENT_TYPE_MQTT_CONNECTED,
        EVENT_TYPE_MQTT_DISCONNECTED,
        EVENT_TYPE_BLE_BROADCASTER_DISCOVERED,
        EVENT_TYPE_BLE_DEVICE_DISCOVERED,
        EVENT_TYPE_BLE_DEVICE_CONNECTED,
        EVENT_TYPE_BLE_DEVICE_DISCONNECTED,
        EVENT_TYPE_BLE_DEVICE_SERVICES_DISCOVERED,
        EVENT_TYPE_BLE_DEVICE_CHARACTERISTIC_VALUE,
        EVENT_TYPE_BLE_MQTT_CONNECTED,
        EVENT_TYPE_BLE_MQTT_GET,
        EVENT_TYPE_BLE_MQTT_SET,
        EVENT_TYPE_NETWORK_CONNECTING
    } event_type_t;

    typedef struct
    {
        event_type_t type;
        union
        {

            struct
            {
                char *topic;
                uint8_t *payload;
                size_t len;
                void *ctx;
            } mqtt_message;
            struct
            {
                mac_addr_t mac;
                uint8_t *adv_data;
                size_t adv_data_len;
                int rssi;
                broadcaster_ops_t *ops;
            } ble_broadcaster_discovered;
            struct
            {
                mac_addr_t mac;
                char *name;
                int rssi;
            } ble_device_discovered;
            struct
            {
                mac_addr_t mac;
            } ble_device_connected;
            struct
            {
                mac_addr_t mac;
            } ble_device_disconnected;
            struct
            {
                mac_addr_t mac;
            } ble_device_services_discovered;
            struct
            {
                mac_addr_t mac;
                ble_uuid_t service;
                ble_uuid_t characteristic;
                uint8_t index;
                uint8_t *value;
                size_t value_len;
            } ble_device_characteristic_value;
        };
    } event_t;

    static QueueHandle_t event_queue;

    static void ble2mqtt_handle_event(event_t *event)
    {
        switch (event->type)
        {
        case EVENT_TYPE_HEARTBEAT_TIMER:
            uptime_publish();
            break;
        case EVENT_TYPE_NETWORK_CONNECTED:
            network_on_connected();
            break;
        case EVENT_TYPE_NETWORK_CONNECTING:
            network_on_connecting();
            break;
        case EVENT_TYPE_OTA_MQTT:
            break;
        case EVENT_TYPE_OTA_COMPLETED:
            break;
        case EVENT_TYPE_MANAGEMENT_RESTART_MQTT:
            management_on_restart_mqtt(event->mqtt_message.topic,
                                       event->mqtt_message.payload, event->mqtt_message.len,
                                       event->mqtt_message.ctx);
            free(event->mqtt_message.topic);
            free(event->mqtt_message.payload);
            break;
        case EVENT_TYPE_NETWORK_DISCONNECTED:
            network_on_disconnected();
            break;
        case EVENT_TYPE_MQTT_CONNECTED:
            mqtt_on_connected();
            break;
        case EVENT_TYPE_MQTT_DISCONNECTED:
            mqtt_on_disconnected();
            break;
        case EVENT_TYPE_BLE_BROADCASTER_DISCOVERED:
            ble_on_broadcaster_discovered(event->ble_broadcaster_discovered.mac,
                                          event->ble_broadcaster_discovered.adv_data,
                                          event->ble_broadcaster_discovered.adv_data_len,
                                          event->ble_broadcaster_discovered.rssi,
                                          event->ble_broadcaster_discovered.ops);
            free(event->ble_broadcaster_discovered.adv_data);
            break;
        case EVENT_TYPE_BLE_DEVICE_DISCOVERED:
            ble_on_device_discovered(event->ble_device_discovered.mac, event->ble_device_discovered.name,
                                     event->ble_device_discovered.rssi);
            break;
        case EVENT_TYPE_BLE_DEVICE_CONNECTED:
            ble_on_device_connected(event->ble_device_connected.mac);
            break;
        case EVENT_TYPE_BLE_DEVICE_DISCONNECTED:
            ble_on_device_disconnected(event->ble_device_disconnected.mac);
            break;
        case EVENT_TYPE_BLE_DEVICE_SERVICES_DISCOVERED:
            ble_on_device_services_discovered(
                event->ble_device_services_discovered.mac);
            break;
        case EVENT_TYPE_BLE_DEVICE_CHARACTERISTIC_VALUE:
            ble_on_device_characteristic_value(
                event->ble_device_characteristic_value.mac,
                event->ble_device_characteristic_value.service,
                event->ble_device_characteristic_value.characteristic,
                event->ble_device_characteristic_value.index,
                event->ble_device_characteristic_value.value,
                event->ble_device_characteristic_value.value_len);
            free(event->ble_device_characteristic_value.value);
            break;
        case EVENT_TYPE_BLE_MQTT_CONNECTED:
            ble_on_mqtt_connected_cb(event->mqtt_message.topic, event->mqtt_message.payload,
                                     event->mqtt_message.len, event->mqtt_message.ctx);
            free(event->mqtt_message.topic);
            free(event->mqtt_message.payload);
            break;
        case EVENT_TYPE_BLE_MQTT_GET:
            ble_on_mqtt_get(event->mqtt_message.topic, event->mqtt_message.payload,
                            event->mqtt_message.len, event->mqtt_message.ctx);
            free(event->mqtt_message.topic);
            free(event->mqtt_message.payload);
            break;
        case EVENT_TYPE_BLE_MQTT_SET:
            ble_on_mqtt_set(event->mqtt_message.topic, event->mqtt_message.payload,
                            event->mqtt_message.len, event->mqtt_message.ctx);
            free(event->mqtt_message.topic);
            free(event->mqtt_message.payload);
            break;
        }

        free(event);
    }

    static void ble2mqtt_task(void *pvParameter)
    {
        event_t *event;

        while (1)
        {
            if (xQueueReceive(event_queue, &event, portMAX_DELAY) != pdTRUE)
                continue;

            ble2mqtt_handle_event(event);
        }

        vTaskDelete(NULL);
    }

    static void heartbeat_timer_cb(TimerHandle_t xTimer)
    {
        event_t *event = (event_t *)malloc(sizeof(*event));

        event->type = EVENT_TYPE_HEARTBEAT_TIMER;

        ESP_LOGD(TAG, "Queuing event HEARTBEAT_TIMER");
        xQueueSend(event_queue, &event, portMAX_DELAY);
    }

    static int start_ble2mqtt_task(void)
    {
        TimerHandle_t hb_timer;

        if (!(event_queue = xQueueCreate(10, sizeof(event_t *))))
            return -1;

        if (xTaskCreatePinnedToCore(ble2mqtt_task, "ble2mqtt_task", 4096 * 3, NULL, 5,
                                    NULL, 1) != pdPASS)
        {
            return -1;
        }

        hb_timer = xTimerCreate("heartbeat", pdMS_TO_TICKS(60 * 1000), pdTRUE,
                                NULL, heartbeat_timer_cb);
        xTimerStart(hb_timer, 0);

        return 0;
    }

    static void _mqtt_on_message(event_type_t type, const char *topic,
                                 const uint8_t *payload, size_t len, void *ctx)
    {
        event_t *event = (event_t *)malloc(sizeof(*event));

        event->type = type;
        event->mqtt_message.topic = strdup(topic);
        event->mqtt_message.payload = (uint8_t *)malloc(len);
        memcpy(event->mqtt_message.payload, payload, len);
        event->mqtt_message.len = len;
        event->mqtt_message.ctx = ctx;

        ESP_LOGD(TAG, "Queuing event MQTT message %d (%s, %p, %u, %p)", type, topic,
                 payload, len, ctx);
        xQueueSend(event_queue, &event, portMAX_DELAY);
    }

    static void _network_on_connected(void)
    {
        event_t *event = (event_t *)malloc(sizeof(*event));

        event->type = EVENT_TYPE_NETWORK_CONNECTED;

        ESP_LOGD(TAG, "Queuing event NETWORK_CONNECTED");
        xQueueSend(event_queue, &event, portMAX_DELAY);
    }

    static void _network_on_connecting(void)
    {
        event_t *event = (event_t *)malloc(sizeof(*event));

        event->type = EVENT_TYPE_NETWORK_CONNECTING;

        ESP_LOGD(TAG, "Queuing event NETWORK_CONNECTING");
        xQueueSend(event_queue, &event, portMAX_DELAY);
    }
    static void _network_on_disconnected(void)
    {
        event_t *event = (event_t *)malloc(sizeof(*event));

        event->type = EVENT_TYPE_NETWORK_DISCONNECTED;

        ESP_LOGD(TAG, "Queuing event NETWORK_DISCONNECTED");
        xQueueSend(event_queue, &event, portMAX_DELAY);
    }

    static void _management_on_restart_mqtt(const char *topic,
                                            const uint8_t *payload, size_t len, void *ctx)
    {
        _mqtt_on_message(EVENT_TYPE_MANAGEMENT_RESTART_MQTT, topic, payload, len,
                         ctx);
    }

    static void _mqtt_on_connected(void)
    {
        event_t *event = (event_t *)malloc(sizeof(*event));

        event->type = EVENT_TYPE_MQTT_CONNECTED;

        ESP_LOGD(TAG, "Queuing event MQTT_CONNECTED");
        xQueueSend(event_queue, &event, portMAX_DELAY);
    }

    static void _mqtt_on_disconnected(void)
    {
        event_t *event = (event_t *)malloc(sizeof(*event));

        event->type = EVENT_TYPE_MQTT_DISCONNECTED;

        ESP_LOGD(TAG, "Queuing event MQTT_DISCONNECTED");
        xQueueSend(event_queue, &event, portMAX_DELAY);
    }

    static void _ble_on_broadcaster_discovered(mac_addr_t mac, uint8_t *adv_data,
                                               size_t adv_data_len, int rssi, broadcaster_ops_t *ops)
    {
        event_t *event = (event_t *)malloc(sizeof(*event));

        event->type = EVENT_TYPE_BLE_BROADCASTER_DISCOVERED;
        memcpy(event->ble_broadcaster_discovered.mac, mac, sizeof(mac_addr_t));
        event->ble_broadcaster_discovered.adv_data = (uint8_t *)malloc(adv_data_len);
        memcpy(event->ble_broadcaster_discovered.adv_data, adv_data, adv_data_len);
        event->ble_broadcaster_discovered.adv_data_len = adv_data_len;
        event->ble_broadcaster_discovered.rssi = rssi;
        event->ble_broadcaster_discovered.ops = ops;

        ESP_LOGD(TAG, "Queuing event BLE_BROADCASTER_DISCOVERED (" MAC_FMT ", "
                      "%p, %u, %d)",
                 MAC_PARAM(mac), adv_data, adv_data_len, rssi);
        xQueueSend(event_queue, &event, portMAX_DELAY);
    }

    static void _ble_on_device_discovered(mac_addr_t mac, char *name, int rssi)
    {
        event_t *event = (event_t *)malloc(sizeof(*event));

        event->type = EVENT_TYPE_BLE_DEVICE_DISCOVERED;
        memcpy(event->ble_device_discovered.mac, mac, sizeof(mac_addr_t));
        event->ble_device_discovered.name = strdup(name);
        event->ble_device_discovered.rssi = rssi;

        ESP_LOGD(TAG, "Queuing event BLE_DEVICE_DISCOVERED (" MAC_FMT ", %d)",
                 MAC_PARAM(mac), rssi);
        xQueueSend(event_queue, &event, portMAX_DELAY);
    }

    static void _ble_on_device_connected(mac_addr_t mac)
    {
        event_t *event = (event_t *)malloc(sizeof(*event));

        event->type = EVENT_TYPE_BLE_DEVICE_CONNECTED;
        memcpy(event->ble_device_connected.mac, mac, sizeof(mac_addr_t));

        ESP_LOGD(TAG, "Queuing event BLE_DEVICE_CONNECTED (" MAC_FMT ")",
                 MAC_PARAM(mac));
        xQueueSend(event_queue, &event, portMAX_DELAY);
    }

    static void _ble_on_device_disconnected(mac_addr_t mac)
    {
        event_t *event = (event_t *)malloc(sizeof(*event));

        event->type = EVENT_TYPE_BLE_DEVICE_DISCONNECTED;
        memcpy(event->ble_device_disconnected.mac, mac, sizeof(mac_addr_t));

        ESP_LOGD(TAG, "Queuing event BLE_DEVICE_DISCONNECTED (" MAC_FMT ")",
                 MAC_PARAM(mac));
        xQueueSend(event_queue, &event, portMAX_DELAY);
    }

    static void _ble_on_device_services_discovered(mac_addr_t mac)
    {
        event_t *event = (event_t *)malloc(sizeof(*event));

        event->type = EVENT_TYPE_BLE_DEVICE_SERVICES_DISCOVERED;
        memcpy(event->ble_device_services_discovered.mac, mac, sizeof(mac_addr_t));

        ESP_LOGD(TAG, "Queuing event BLE_DEVICE_SERVICES_DISCOVERED (" MAC_FMT ")",
                 MAC_PARAM(mac));
        xQueueSend(event_queue, &event, portMAX_DELAY);
    }

    static void _ble_on_device_characteristic_value(mac_addr_t mac,
                                                    ble_uuid_t service, ble_uuid_t characteristic, uint8_t index,
                                                    uint8_t *value, size_t value_len)
    {
        event_t *event = (event_t *)malloc(sizeof(*event));

        event->type = EVENT_TYPE_BLE_DEVICE_CHARACTERISTIC_VALUE;
        memcpy(event->ble_device_characteristic_value.mac, mac, sizeof(mac_addr_t));
        memcpy(event->ble_device_characteristic_value.service, service,
               sizeof(ble_uuid_t));
        memcpy(event->ble_device_characteristic_value.characteristic,
               characteristic, sizeof(ble_uuid_t));
        event->ble_device_characteristic_value.value = (uint8_t *)malloc(value_len);
        memcpy(event->ble_device_characteristic_value.value, value, value_len);
        event->ble_device_characteristic_value.value_len = value_len;
        event->ble_device_characteristic_value.index = index;

        ESP_LOGD(TAG, "Queuing event BLE_DEVICE_CHARACTERISTIC_VALUE (" MAC_FMT ", " UUID_FMT ", %p, %u)", MAC_PARAM(mac), UUID_PARAM(characteristic), value,
                 value_len);
        xQueueSend(event_queue, &event, portMAX_DELAY);
    }

    static void _ble_on_mqtt_connected_cb(const char *topic, const uint8_t *payload,
                                          size_t len, void *ctx)
    {
        _mqtt_on_message(EVENT_TYPE_BLE_MQTT_CONNECTED, topic, payload, len, ctx);
    }

    static void _ble_on_mqtt_get(const char *topic, const uint8_t *payload,
                                 size_t len, void *ctx)
    {
        _mqtt_on_message(EVENT_TYPE_BLE_MQTT_GET, topic, payload, len, ctx);
    }

    static void _ble_on_mqtt_set(const char *topic, const uint8_t *payload,
                                 size_t len, void *ctx)
    {
        _mqtt_on_message(EVENT_TYPE_BLE_MQTT_SET, topic, payload, len, ctx);
    }
} //
  // handler for the button
  //     [BUTTON_PRESSED] = "pressed",
  // [BUTTON_RELEASED] = "released",
  //     [BUTTON_CLICKED] = "clicked",
  //     [BUTTON_PRESSED_LONG] = "pressed long",
double randfrom(double min, double max)
{
    double range = (max - min);
    double div = RAND_MAX / range;
    return min + (rand() / div);
}
void task3(void *arg)
{

    vTaskDelay(2000 / portTICK_PERIOD_MS);
    double temp = randfrom(1.0, 40.0);
    ESP_LOGI(TAG, "%.1f °C", temp);
    ShowTemp(temp);
}
void task1(void *arg)
{

    vTaskDelay(10000 / portTICK_PERIOD_MS);
    Sleep();
    vTaskSuspend(task1_t);
}

static void on_button(button_t *btn, button_state_t state)
{
    ESP_LOGI(TAG, "button %s", states[state]);
    if (state == BUTTON_PRESSED)
    {

        // ble_disconnect_all();
        // ble_scan_stop();
        // ble_clear_bonding_info();
        // vTaskDelay(2000 / portTICK_PERIOD_MS);
        // ble_scan_start();
        if (!isPass)
        {
            isPass = !isPass;
            wifi_disconnect();
            /* Start by connecting to network */
            xTaskCreatePinnedToCore(task1, "task1", 8 * 1024, NULL, 5,
                                    &task1_t, 1);
            ShowStatus("กำลังเปิดโหมต Access Point", 0xffffff, 0x000000);
            wifi_start_ap(device_name_get(), NULL);
            isAPMode = true;
        }
        else
        {

            isPass = !isPass;
            TaskHandle_t task = xTaskGetCurrentTaskHandle();
            if (task1_t == task)
            {
                ESP_LOGI(TAG, "task");
                vTaskSuspend(task1_t);
            }
            isAPMode = false;
            //
            //  xTaskCreatePinnedToCore(task2, "task2", 8 * 1024, NULL, 5,
            //                          &task2_t, 1);
            //  // xTaskCreate(task1, "task1", 8 * 1024, NULL, 5, NULL);
            //  ShowStatus("pressed");
            Wakeup();

            vTaskDelay(2000 / portTICK_PERIOD_MS);
            /* Start by connecting to network */
            isConnect = false;
            wifi_reconnect();
            // ShowStatus("กำลังเชื่อมต่อ Wifi");
            // last_message = "กำลังเชื่อมต่อ Wifi";
            // wifi_connect(config_network_wifi_ssid_get(), config_network_wifi_password_get(),
            //              wifi_eap_atomethod(config_eap_method_get()),
            //              config_eap_identity_get(),
            //              config_eap_username_get(), config_eap_password_get(),
            //              config_eap_ca_cert_get(), config_eap_client_cert_get(),
            //              config_eap_client_key_get());
        }
    }
    else if (state == BUTTON_PRESSED_LONG)
    {
        esp_restart();
    }
}
extern "C" void app_main(void)
{
    ESP_LOGI(TAG, "setting");

    ui_init();
    ShowStatus("กรุณารอสักครู่....", 0xfcdce2, 0x000000);
    last_message = "กรุณารอสักครู่....";
    int config_failed;

    /* Initialize NVS */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_LOGI(TAG, "Version: %s", BLE2MQTT_VER);

    /* Init configuration */
    config_failed = config_initialize();

    /* Init remote logging */
    ESP_ERROR_CHECK(log_initialize());

    /* Init OTA */
    ESP_ERROR_CHECK(ota_initialize());

    /* Init Wi-Fi */
    ESP_ERROR_CHECK(wifi_initialize());
    wifi_hostname_set(device_name_get());
    wifi_set_on_connected_cb(_network_on_connected);
    wifi_set_on_disconnected_cb(_network_on_disconnected);

    /* Init mDNS */
    ESP_ERROR_CHECK(mdns_init());
    mdns_hostname_set(device_name_get());

    /* Init name resolver */
    ESP_ERROR_CHECK(resolve_initialize());

    /* Init MQTT */
    ESP_ERROR_CHECK(mqtt_initialize());
    mqtt_set_on_connected_cb(_mqtt_on_connected);
    mqtt_set_on_disconnected_cb(_mqtt_on_disconnected);

    /* Init BLE */
    ESP_ERROR_CHECK(ble_initialize());
    ble_set_on_broadcaster_discovered_cb(_ble_on_broadcaster_discovered);
    ble_set_on_device_discovered_cb(_ble_on_device_discovered);
    ble_set_on_device_connected_cb(_ble_on_device_connected);
    ble_set_on_device_disconnected_cb(_ble_on_device_disconnected);
    ble_set_on_device_services_discovered_cb(
        _ble_on_device_services_discovered);
    ble_set_on_device_characteristic_value_cb(
        _ble_on_device_characteristic_value);
    ble_set_on_passkey_requested_cb(ble_on_passkey_requested);

    ESP_ERROR_CHECK(esp_ble_gatts_register_callback(gatts_event_handler));
    ESP_ERROR_CHECK(esp_ble_gatts_app_register(PROFILE_A_APP_ID));
    ESP_ERROR_CHECK(esp_ble_gatts_app_register(PROFILE_B_APP_ID));

    /* Init web server */
    ESP_ERROR_CHECK(httpd_initialize());
    // httpd_set_on_ota_completed_cb(_ota_on_completed);

    /* Start BLE2MQTT task */
    ESP_ERROR_CHECK(start_ble2mqtt_task());

    /* Failed to load configuration or it wasn't set, create access point */
    // if (config_failed || !strcmp(config_network_wifi_ssid_get() ?: "", "MY_SSID"))
    // {
    //     wifi_start_ap(device_name_get(), NULL);
    //     return;
    // }
    //
    // Push button connected to GPIO0:
    //
    btn.gpio = (gpio_num_t)BUTTON_GPIO;
    btn.pressed_level = 0;
    btn.internal_pull = true;
    btn.autorepeat = false;
    btn.callback = on_button;

    ESP_ERROR_CHECK(button_init(&btn));
    ShowStatus("กำลังเชื่อมต่อ Wifi", 0xfcdce2, 0x000000);
    last_message = "กำลังเชื่อมต่อ Wifi";
    /* Start by connecting to network */
    isConnect = false;
    wifi_connect(config_network_wifi_ssid_get(), config_network_wifi_password_get(),
                 wifi_eap_atomethod(config_eap_method_get()),
                 config_eap_identity_get(),
                 config_eap_username_get(), config_eap_password_get(),
                 config_eap_ca_cert_get(), config_eap_client_cert_get(),
                 config_eap_client_key_get());

    // xTaskCreatePinnedToCore(task3, "task3", 4024, NULL, 5,
    //                         NULL, 1);
    // ShowTempText("10.5 C");
    // vTaskDelay(2000 / portTICK_PERIOD_MS);
    // ShowTempText("12.5 C");
    // vTaskDelay(2000 / portTICK_PERIOD_MS);
    // ShowTempText("22.5 C");
    // vTaskDelay(2000 / portTICK_PERIOD_MS);
    // ShowTempText("14.6 C");
    // vTaskDelay(2000 / portTICK_PERIOD_MS);
    // ShowTempText("16.5 C");
    // vTaskDelay(2000 / portTICK_PERIOD_MS);
    // ShowTempText("18.5 C");
    // double temp = randfrom(1.0, 40.0);
    // ShowTemp(temp);
    // vTaskDelay(2000 / portTICK_PERIOD_MS);
    // temp = randfrom(30.0, 40.0);
    // ShowTemp(temp);
    // vTaskDelay(2000 / portTICK_PERIOD_MS);
    // temp = randfrom(30.0, 40.0);
    // ShowTemp(temp);
    // vTaskDelay(2000 / portTICK_PERIOD_MS);
    // temp = randfrom(30.0, 40.0);
    // ShowTemp(temp);
    // vTaskDelay(2000 / portTICK_PERIOD_MS);
    // temp = randfrom(30.0, 40.0);
    // ShowTemp(temp);
    // vTaskDelay(2000 / portTICK_PERIOD_MS);
    // temp = randfrom(30.0, 40.0);
    // ShowTemp(temp);
    // vTaskDelay(2000 / portTICK_PERIOD_MS);
    // temp = randfrom(30.0, 40.0);
    // ShowTemp(temp);
}
