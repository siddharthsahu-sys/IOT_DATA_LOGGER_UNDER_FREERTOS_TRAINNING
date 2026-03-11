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
#include "protocol_examples_common.h"
#include "macros.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#define TASK_STACK_SIZE_LARGE   4096
#define TASK_STACK_SIZE_MEDIUM   3072
#define TASK_STACK_SIZE_SMALL    2048

#define MAX_QUERIES             10
#define MAX_QUERY_LEN           20
#define MQTT_PACKET_SIZE        500
#define UART_PORT_NUM           UART_NUM_1
#define UART_TX_PIN             17
#define UART_RX_PIN             16
#define QUEUE_SIZE              50

static const char *TAG = "IOT_LOGGER";

#if CONFIG_BROKER_CERTIFICATE_OVERRIDDEN == 1
static const uint8_t mqtt_pem_start[] = "-----BEGIN CERTIFICATE-----\n" CONFIG_BROKER_CERTIFICATE_OVERRIDE "\n-----END CERTIFICATE-----";
#else
extern const uint8_t mqtt_pem_start[] asm("_binary_mqtt_eclipseprojects_io_pem_start");
#endif

typedef struct __attribute__((packed)) {
    uint32_t publish_freq;
    uint8_t is_query_based;
    uint8_t num_queries;
    uint8_t query_lengths[MAX_QUERIES];
    uint8_t queries[MAX_QUERIES][MAX_QUERY_LEN];
} device_config_t;

typedef struct {
    uint8_t data[MQTT_PACKET_SIZE];
    uint16_t packet_length;
} mqtt_queue_item_t;

static device_config_t global_config;
static QueueHandle_t mqtt_queue = NULL;
static esp_mqtt_client_handle_t global_client = NULL;
static char base_topic[64];
static char config_topic[64];

static TaskHandle_t sampler_task_handle = NULL;
static TaskHandle_t mqtt_task_handle = NULL;

static void hex_string_to_bytes(const char* src, uint8_t* dst, int byte_len)
{
    for (int i = 0; i < byte_len; i++)
    {
        sscanf(src + (i * 2), "%02hhx", &dst[i]);
    }
}

static void uart_sampler_task(void *pvParameters)
{
    uint8_t rx_buffer[MAX_QUERY_LEN];
    mqtt_queue_item_t queue_item;
    
    while (1)
    {
        memset(&queue_item, 0, sizeof(mqtt_queue_item_t));
        int position = 0;

        if (global_config.is_query_based)
        {
            for (int i = 0; i < global_config.num_queries; i++)
            {
                uart_write_bytes(UART_PORT_NUM, 
                                (const char*)global_config.queries[i], 
                                global_config.query_lengths[i]);
                
                int length = uart_read_bytes(UART_PORT_NUM, 
                                            rx_buffer, 
                                            MAX_QUERY_LEN, 
                                            pdMS_TO_TICKS(100));

                if (position < MQTT_PACKET_SIZE - 2)
                {
                    queue_item.data[position++] = '~';
                    
                    if (length > 0)
                    {
                        memcpy(&queue_item.data[position], rx_buffer, length);
                        position += length;
                    }
                    
                    queue_item.data[position++] = '#';
                }
            }
            
            queue_item.packet_length = position;
        }
        else
        {
            int length = uart_read_bytes(UART_PORT_NUM, 
                                        rx_buffer, 
                                        MQTT_PACKET_SIZE - 2, 
                                        pdMS_TO_TICKS(200));
            
            if (length > 0)
            {
                queue_item.data[0] = '~';
                memcpy(&queue_item.data[1], rx_buffer, length);
                queue_item.data[length + 1] = '#';
                queue_item.packet_length = length + 2;
            }
            else
            {
                queue_item.data[0] = '~';
                queue_item.data[1] = '#';
                queue_item.packet_length = 2;
            }
        }

        if (queue_item.packet_length > 0)
        {
            if (xQueueSend(mqtt_queue, &queue_item, 0) != pdPASS)
            {
                ESP_LOGE(TAG, "Queue Full! Sample Dropped.");
            }
        }
        
        vTaskDelay(pdMS_TO_TICKS(global_config.publish_freq * 1000));
    }
}

static void mqtt_publishing_task(void *pvParameters)
{
    mqtt_queue_item_t received_item;
    int burst_counter = 0;

    while (1)
    {
        if (xQueueReceive(mqtt_queue, &received_item, portMAX_DELAY) == pdTRUE)
        {
            burst_counter++;
            
            if (burst_counter % 15 == 0)
            {
                ESP_LOGW(TAG, ">>> WIFI BURST SIMULATED: Core 0 blocked for 4 seconds...");
                vTaskDelay(pdMS_TO_TICKS(4000));
            }

            if (global_client != NULL && received_item.packet_length > 0)
            {
                int msg_id = esp_mqtt_client_publish(global_client, 
                                                     base_topic, 
                                                     (char*)received_item.data, 
                                                     received_item.packet_length, 
                                                     1, 0);
                
                if (msg_id >= 0)
                {
                    ESP_LOGI(TAG, "Data Published. Queue Waiting: %d/%d", 
                             uxQueueMessagesWaiting(mqtt_queue), QUEUE_SIZE);
                }
                else
                {
                    ESP_LOGE(TAG, "Failed to publish message");
                }
            }
        }
    }
}

static void mqtt_event_handler(void *handler_args, 
                                esp_event_base_t base, 
                                int32_t event_id, 
                                void *event_data)
{
    esp_mqtt_event_handle_t event = event_data;
    
    switch ((esp_mqtt_event_id_t)event_id)
    {
        case MQTT_EVENT_CONNECTED:
        {
            int msg_id = esp_mqtt_client_subscribe(event->client, config_topic, 1);
            if (msg_id >= 0)
            {
                ESP_LOGI(TAG, "Connected & Subscribed to: %s", config_topic);
            }
            else
            {
                ESP_LOGE(TAG, "Failed to subscribe to topic: %s", config_topic);
            }
            break;
        }
        
        case MQTT_EVENT_DATA:
        {
            if (strncmp(event->topic, config_topic, event->topic_len) == 0)
            {
                if (event->data_len == sizeof(device_config_t) * 2)
                {
                    hex_string_to_bytes(event->data, (uint8_t*)&global_config, sizeof(device_config_t));
                    
                    nvs_handle_t nvs_handle;
                    esp_err_t ret = nvs_open("storage", NVS_READWRITE, &nvs_handle);
                    
                    if (ret == ESP_OK)
                    {
                        ret = nvs_set_blob(nvs_handle, "device_cfg", &global_config, sizeof(device_config_t));
                        if (ret == ESP_OK)
                        {
                            nvs_commit(nvs_handle);
                        }
                        nvs_close(nvs_handle);
                    }
                    
                    int msg_id = esp_mqtt_client_publish(event->client, config_topic, "SUCCESS", 7, 1, 0);
                    if (msg_id < 0)
                    {
                        ESP_LOGE(TAG, "Failed to publish success message");
                    }
                    
                    vTaskDelay(pdMS_TO_TICKS(500));
                    esp_restart();
                }
                else
                {
                    int msg_id = esp_mqtt_client_publish(event->client, config_topic, "FAIL_LEN", 8, 1, 0);
                    if (msg_id < 0)
                    {
                        ESP_LOGE(TAG, "Failed to publish failure message");
                    }
                }
            }
            break;
        }
        
        case MQTT_EVENT_ERROR:
        {
            ESP_LOGE(TAG, "MQTT error occurred");
            break;
        }
        
        default:
            break;
    }
}

static void mqtt_app_start(void)
{
    const esp_mqtt_client_config_t mqtt_config = {
        .broker = {
            .address.uri = DEFAULT_MQTTURL,
            .verification.certificate = (const char *)mqtt_pem_start
        },
        .credentials = {
            .username = DEFAULT_USERNAME,
            .authentication.password = DEFAULT_PASSWORD
        }
    };
    
    global_client = esp_mqtt_client_init(&mqtt_config);
    
    if (global_client == NULL)
    {
        ESP_LOGE(TAG, "Failed to initialize MQTT client");
        return;
    }
    
    esp_err_t ret = esp_mqtt_client_register_event(global_client, 
                                                    ESP_EVENT_ANY_ID, 
                                                    mqtt_event_handler, 
                                                    NULL);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to register MQTT event handler: %s", esp_err_to_name(ret));
        return;
    }
    
    ret = esp_mqtt_client_start(global_client);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to start MQTT client: %s", esp_err_to_name(ret));
        return;
    }
}

void app_main(void)
{
    BaseType_t task_created;
    esp_err_t ret;
    nvs_handle_t nvs_handle;
    
    ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ret = nvs_flash_erase();
        if (ret != ESP_OK)
        {
            ESP_LOGE(TAG, "Failed to erase NVS: %s", esp_err_to_name(ret));
            return;
        }
        
        ret = nvs_flash_init();
    }
    
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to initialize NVS: %s", esp_err_to_name(ret));
        return;
    }

    ret = nvs_open("storage", NVS_READWRITE, &nvs_handle);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to open NVS: %s", esp_err_to_name(ret));
        return;
    }
    
    size_t config_size = sizeof(device_config_t);
    ret = nvs_get_blob(nvs_handle, "device_cfg", &global_config, &config_size);
    
    if (ret != ESP_OK)
    {
        ESP_LOGI(TAG, "No stored config found, using defaults");
        global_config.publish_freq = 10;
        global_config.is_query_based = 1;
        global_config.num_queries = 2;
        global_config.query_lengths[0] = 4;
        memcpy(global_config.queries[0], "\x01\x02\x03\xFF", 4);
        global_config.query_lengths[1] = 4;
        memcpy(global_config.queries[1], "\x02\x02\x03\xFF", 4);
    }
    
    nvs_close(nvs_handle);

    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    
    sprintf(base_topic, "ESP32toCloud/%02X%02X%02X%02X%02X%02X", 
            mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    sprintf(config_topic, "Config/%02X%02X%02X%02X%02X%02X", 
            mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    uart_config_t uart_config = {
        .baud_rate = 9600,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT
    };
    
    ret = uart_param_config(UART_PORT_NUM, &uart_config);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to configure UART: %s", esp_err_to_name(ret));
        return;
    }
    
    ret = uart_set_pin(UART_PORT_NUM, UART_TX_PIN, UART_RX_PIN, -1, -1);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to set UART pins: %s", esp_err_to_name(ret));
        return;
    }
    
    ret = uart_driver_install(UART_PORT_NUM, 1024, 0, 0, NULL, 0);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to install UART driver: %s", esp_err_to_name(ret));
        return;
    }

    mqtt_queue = xQueueCreate(QUEUE_SIZE, sizeof(mqtt_queue_item_t));
    if (mqtt_queue == NULL)
    {
        ESP_LOGE(TAG, "Failed to create queue");
        return;
    }

    task_created = xTaskCreatePinnedToCore(uart_sampler_task, 
                                           "uart_task", 
                                           TASK_STACK_SIZE_LARGE, 
                                           NULL, 10, 
                                           &sampler_task_handle, 1);
    if (task_created != pdPASS)
    {
        ESP_LOGE(TAG, "Failed to create UART sampler task");
        return;
    }

    ret = esp_netif_init();
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to initialize netif: %s", esp_err_to_name(ret));
        return;
    }
    
    ret = esp_event_loop_create_default();
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to create event loop: %s", esp_err_to_name(ret));
        return;
    }
    
    ret = example_connect();
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to connect to WiFi: %s", esp_err_to_name(ret));
        return;
    }
    
    mqtt_app_start();

    task_created = xTaskCreatePinnedToCore(mqtt_publishing_task, 
                                           "MQTT_Pub", 
                                           TASK_STACK_SIZE_LARGE, 
                                           NULL, 5, 
                                           &mqtt_task_handle, 0);
    if (task_created != pdPASS)
    {
        ESP_LOGE(TAG, "Failed to create MQTT publishing task");
        return;
    }

    while (1)
    {
        printf("\n--- HEALTH CHECK ---\n");
        printf("Free Heap: %u bytes\n", (unsigned int)xPortGetFreeHeapSize());
        
        if (sampler_task_handle != NULL)
        {
            printf("Sampler Stack Left: %u\n", 
                   (unsigned int)uxTaskGetStackHighWaterMark(sampler_task_handle));
        }
        
        if (mqtt_task_handle != NULL)
        {
            printf("MQTT Task Stack Left: %u\n", 
                   (unsigned int)uxTaskGetStackHighWaterMark(mqtt_task_handle));
        }
        
        printf("--------------------\n");
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}