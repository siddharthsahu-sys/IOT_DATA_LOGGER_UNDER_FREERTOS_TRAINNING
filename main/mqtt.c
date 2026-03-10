// #include "mqtt.h"
// #include "mqtt_client.h"

// esp_err_t esp_mqtt_client_start(esp_mqtt_client_handle_t client)
// {
//     if (!client) {
//         ESP_LOGE(TAG, "Client was not initialized");
//         return ESP_ERR_INVALID_ARG;
//     }
//     MQTT_API_LOCK(client);
//     if (client->state != MQTT_STATE_INIT && client->state != MQTT_STATE_DISCONNECTED) {
//         ESP_LOGE(TAG, "Client has started");
//         MQTT_API_UNLOCK(client);
//         return ESP_FAIL;
//     }
//     esp_err_t err = ESP_OK;
// #if MQTT_CORE_SELECTION_ENABLED
//     ESP_LOGD(TAG, "Core selection enabled on %u", MQTT_TASK_CORE);
//     if (xTaskCreatePinnedToCore(esp_mqtt_task, "mqtt_task", client->config->task_stack, client, client->config->task_prio, &client->task_handle, MQTT_TASK_CORE) != pdTRUE) {
//         ESP_LOGE(TAG, "Error create mqtt task");
//         err = ESP_FAIL;
//     }
// #else
//     ESP_LOGD(TAG, "Core selection disabled");
//     if (xTaskCreate(esp_mqtt_task, "mqtt_task", client->config->task_stack, client, client->config->task_prio, &client->task_handle) != pdTRUE) {
//         ESP_LOGE(TAG, "Error create mqtt task");
//         err = ESP_FAIL;
//     }
// #endif
//     MQTT_API_UNLOCK(client);
//     return err;
// }
