
#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "esp_system.h"
#include "nvs_flash.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_log.h"
#include "mqtt_client.h"
#include "esp_mac.h"
#include "driver/uart.h"
#include "protocol_examples_common.h" // For example_connect()
#include "macros.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"


#define MAX_QUERIES 10
#define MAX_QUERY_LEN 20
#define MQTT_PACKET_SIZE 500
#define UART_PORT_NUM UART_NUM_1
#define UART_TX_PIN 17
#define UART_RX_PIN 16

static const char *TAG = "IOT_LOGGER";

// --- Certificate Handling ---
#if CONFIG_BROKER_CERTIFICATE_OVERRIDDEN == 1
static const uint8_t mqtt_pem_start[] = "-----BEGIN CERTIFICATE-----\n" CONFIG_BROKER_CERTIFICATE_OVERRIDE "\n-----END CERTIFICATE-----";
#else
extern const uint8_t mqtt_pem_start[] asm("_binary_mqtt_eclipseprojects_io_pem_start");
#endif

// --- Configuration Struct ---
typedef struct __attribute__((packed)) {
    uint32_t publish_freq;      
    uint8_t is_query_based;     
    uint8_t num_queries;        
    uint8_t query_lengths[MAX_QUERIES];
    uint8_t queries[MAX_QUERIES][MAX_QUERY_LEN];
} DeviceConfig_t;

// --- Global Variables ---
DeviceConfig_t global_cfg;
QueueHandle_t mqtt_queue;
typedef struct{
    uint8_t arry[MQTT_PACKET_SIZE];
    uint16_t packetLen;
}Mqtt_Queue;

esp_mqtt_client_handle_t client_global = NULL;
char base_topic[64], config_topic[64];

// --- Helpers ---
void hex_string_to_bytes(const char* src, uint8_t* dst, int byte_len) {
    for (int i = 0; i < byte_len; i++) {
        sscanf(src + (i * 2), "%02hhx", &dst[i]);
    }
}

// --- UART Sampler Task (Core 0) ---
void uart_sampler_task(void *pvParameters) {
    uint8_t rx_temp[MAX_QUERY_LEN];
    // uint8_t tx_buffer[MQTT_PACKET_SIZE];
    Mqtt_Queue st_Mqtt_Queue;
    while (1) {
        memset(&st_Mqtt_Queue, 0, sizeof(Mqtt_Queue)); 
        int pos = 0;

        if (global_cfg.is_query_based) {
            for (int i = 0; i < global_cfg.num_queries; i++) {
                uart_write_bytes(UART_PORT_NUM, (const char*)global_cfg.queries[i], global_cfg.query_lengths[i]);
                int len = uart_read_bytes(UART_PORT_NUM, rx_temp, MAX_QUERY_LEN, pdMS_TO_TICKS(100));

                if (pos < MQTT_PACKET_SIZE - 2) {
                    st_Mqtt_Queue.arry[pos++] = '~';
                    if (len > 0) {
                        memcpy(&st_Mqtt_Queue.arry[pos], rx_temp, len);
                        pos += len;
                    }
                    st_Mqtt_Queue.arry[pos++] = '#';
                }
                // vTaskDelay(pdMS_TO_TICKS(1000)); 
            }
        st_Mqtt_Queue.packetLen=pos;
        } else {
            int len = uart_read_bytes(UART_PORT_NUM, rx_temp, MQTT_PACKET_SIZE - 2, pdMS_TO_TICKS(200));
            if (len > 0) {
                st_Mqtt_Queue.arry[0] = '~';
                memcpy(&st_Mqtt_Queue.arry[1], rx_temp, len);
                st_Mqtt_Queue.arry[len + 1] = '#';
                st_Mqtt_Queue.packetLen = len + 2;
            } else {
                st_Mqtt_Queue.arry[0] = '~';
                st_Mqtt_Queue.arry[1] = '#';
                st_Mqtt_Queue.packetLen = 2;
            }
        }

        // Send pointer to the queue item
        if (st_Mqtt_Queue.packetLen > 0) {
            xQueueSend(mqtt_queue, &st_Mqtt_Queue, portMAX_DELAY);
        }
        vTaskDelay(pdMS_TO_TICKS(global_cfg.publish_freq * 1000));
    }
}

// --- MQTT Event Handler ---
static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data) {
    esp_mqtt_event_handle_t event = event_data;
    switch ((esp_mqtt_event_id_t)event_id) {
        case MQTT_EVENT_CONNECTED:
            esp_mqtt_client_subscribe(event->client, config_topic, 1);
            ESP_LOGI(TAG, "Subscribed to config: %s", config_topic);
            break;
        case MQTT_EVENT_DATA:
            if (strncmp(event->topic, config_topic, event->topic_len) == 0) {
                if (event->data_len == sizeof(DeviceConfig_t) * 2) {
                    hex_string_to_bytes(event->data, (uint8_t*)&global_cfg, sizeof(DeviceConfig_t));
                    nvs_handle_t h;
                    nvs_open("storage", NVS_READWRITE, &h);
                    nvs_set_blob(h, "device_cfg", &global_cfg, sizeof(DeviceConfig_t));
                    nvs_commit(h);
                    nvs_close(h);
                    esp_mqtt_client_publish(event->client, config_topic, "SUCCESS", 7, 1, 0);
                    vTaskDelay(pdMS_TO_TICKS(500));
                    esp_restart();
                } else {
                    esp_mqtt_client_publish(event->client, config_topic, "FAIL_LEN", 8, 1, 0);
                }
            }
            break;
        default: break;
    }
}

static void mqtt_app_start(void) {
    const esp_mqtt_client_config_t mqtt_cfg = {
        .broker = {
            .address.uri = DEFAULT_MQTTURL,
            .verification.certificate = (const char *)mqtt_pem_start
        },
        .credentials = {
            .username = DEFAULT_USERNAME,
            .authentication.password = DEFAULT_PASSWORD
        }
    };
    client_global = esp_mqtt_client_init(&mqtt_cfg);
    esp_mqtt_client_register_event(client_global, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    esp_mqtt_client_start(client_global);
}

void app_main(void) {
    // 1. Storage
    ESP_ERROR_CHECK(nvs_flash_init());

    // 2. Load Config
    nvs_handle_t h;
    nvs_open("storage", NVS_READWRITE, &h);
    size_t size = sizeof(DeviceConfig_t);
    if (nvs_get_blob(h, "device_cfg", &global_cfg, &size) != ESP_OK) {
        global_cfg.publish_freq = 10;
        global_cfg.is_query_based = 1;
        global_cfg.num_queries = 2;
        global_cfg.query_lengths[0] = 4;
        memcpy(global_cfg.queries[0], "\x01\x02\x03\xFF", 4);
        global_cfg.query_lengths[1] = 4;
        memcpy(global_cfg.queries[1], "\x02\x02\x03\xFF", 4);

    }
    nvs_close(h);

    // 3. Setup Identity & UART
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    sprintf(base_topic, "ESP32toCloud/%02X%02X%02X%02X%02X%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    sprintf(config_topic, "Config/%02X%02X%02X%02X%02X%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    uart_config_t u_cfg = { .baud_rate = 9600, .data_bits = UART_DATA_8_BITS, .parity = UART_PARITY_DISABLE, .stop_bits = UART_STOP_BITS_1, .flow_ctrl = UART_HW_FLOWCTRL_DISABLE, .source_clk = UART_SCLK_DEFAULT };
    uart_param_config(UART_PORT_NUM, &u_cfg);
    uart_set_pin(UART_PORT_NUM, UART_TX_PIN, UART_RX_PIN, -1, -1);
    uart_driver_install(UART_PORT_NUM, 1024, 0, 0, NULL, 0);

    // 4. Queue & Sampling Task (Core 1)
    mqtt_queue = xQueueCreate(5,sizeof(Mqtt_Queue));
    xTaskCreatePinnedToCore(uart_sampler_task, "uart_task", 4096, NULL, 10, NULL, 1);               // Core one configured

    // 5. Connection & MQTT (Core 0) config from sdkconfig
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    ESP_ERROR_CHECK(example_connect());
    mqtt_app_start();

    // 6. Publishing Loop (Core 1) config from sdkconfig
    // uint8_t out_data[MQTT_PACKET_SIZE];
    Mqtt_Queue received_queue;
    
    while (1) {
        // Receive pointer to the queue item
        if (xQueueReceive(mqtt_queue, &received_queue, portMAX_DELAY)) {
            if (client_global && received_queue.packetLen > 0) {
                esp_mqtt_client_publish(client_global, base_topic, (char*)received_queue.arry, received_queue.packetLen, 1, 0);
                ESP_LOGI(TAG, "Published: %.*s", received_queue.packetLen, (char*)received_queue.arry);
            }
        }
    }
}

