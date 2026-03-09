#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdio.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

static const char *TAG = "STATS_DEMO";

/**
 * Task that simply consumes some CPU cycles to show up in the stats
 */
void dummy_task(void *pvParameters) {
    while (1) {
        // Do some "work"
        for (volatile int i = 0; i < 100000; i++) {
            __asm__("nop");
        }
        vTaskDelay(pdMS_TO_TICKS(100)); // Sleep for 100ms
    }
}


void stats_task(void *pvParameters) {
    char *stats_buffer = malloc(2048);

    while (1) {
        // 1. Print Runtime Stats (CPU)
        printf("\n--- CPU Usage ---\n");
        printf("\n--- Task Runtime Statistics ---\n");
        printf("Task Name       Abs Time        Time %%\n");
        printf("----------------------------------------------\n");

        /* * This fills the buffer with a table of:
         * [Task Name] [Ticks/Cycles Spent] [Percentage of total run time]
         */
        vTaskGetRunTimeStats(stats_buffer);
        printf("%s", stats_buffer);
        printf("----------------------------------------------\n");
        // 2. Print Task List (Memory/State)
        // You must enable configUSE_TRACE_FACILITY & configUSE_STATS_FORMATTING_FUNCTIONS
        printf("\n--- Task State & Stack ---\n");
        printf("Task Name\tState\tPrio\tStack\tNum\n");
        vTaskList(stats_buffer);
        printf("%s", stats_buffer);
        
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
    free(stats_buffer);
}
void app_main(void) {
    // Create a dummy task to give us something interesting to look at
    xTaskCreate(dummy_task, "Work_Task_1", 2048, NULL, 5, NULL);
    xTaskCreate(dummy_task, "Work_Task_2", 2048, NULL, 5, NULL);

    // Create the monitor task
    xTaskCreate(stats_task, "Stats_Monitor", 4096, NULL, 1, NULL);
}