/**************************************************************************/
/*                                                                        */
/*       Copyright (c) Microsoft Corporation. All rights reserved.        */
/*                                                                        */
/*       This software is licensed under the Microsoft Software License   */
/*       Terms for Microsoft Azure RTOS. Full text of the license can be  */
/*       found in the LICENSE file at https://aka.ms/AzureRTOS_EULA       */
/*       and in the root directory of this software.                      */
/*                                                                        */
/**************************************************************************/

#include "tx_api.h"
#include "tx_thread.h"

#include "tx_port.h"

#include "esp_task.h"
#include "esp_log.h"

static const char *TAG = "cpu_start";

/**
 * @brief A variable is used to keep track of the critical section nesting.
 * @note This variable has to be stored as part of the task context and must be initialized to a non zero value
 *       to ensure interrupts don't inadvertently become unmasked before the scheduler starts.
 *       As it is stored as part of the task context it will automatically be set to 0 when the first task is started.
 */
static UINT uxSavedInterruptState = 0;

// ------------------ Critical Sections --------------------

void vPortEnterCritical(void)
{
    UINT state = portSET_INTERRUPT_MASK_FROM_ISR();
    _tx_thread_preempt_disable++;

    if (_tx_thread_preempt_disable == 1u) {
        uxSavedInterruptState = state;
    }
}

void vPortExitCritical(void)
{
    if(_tx_thread_preempt_disable == 0u) {
        TX_FREERTOS_ASSERT_FAIL();
    }
    
    if (_tx_thread_preempt_disable > 0u) {
        _tx_thread_preempt_disable--;
        if (_tx_thread_preempt_disable == 0u) {
            portCLEAR_INTERRUPT_MASK_FROM_ISR(uxSavedInterruptState);
        }
    }
}

void esp_startup_start_app_common(void)
{
#if CONFIG_ESP_INT_WDT
    esp_int_wdt_init();
    //Initialize the interrupt watch dog for CPU0.
    esp_int_wdt_cpu_init();
#endif

    esp_crosscore_int_init();

#ifdef CONFIG_ESP_SYSTEM_GDBSTUB_RUNTIME
    esp_gdbstub_init();
#endif // CONFIG_ESP_SYSTEM_GDBSTUB_RUNTIME

    portBASE_TYPE res = xTaskCreatePinnedToCore(&main_task, "main",
                                                ESP_TASK_MAIN_STACK, NULL,
                                                ESP_TASK_MAIN_PRIO, NULL, ESP_TASK_MAIN_CORE);
    assert(res == pdTRUE);
    (void)res;
}

static void main_task(void* args)
{
#if !CONFIG_FREERTOS_UNICORE
    // Wait for FreeRTOS initialization to finish on APP CPU, before replacing its startup stack
    while (port_xSchedulerRunning[1] == 0) {
        ;
    }
#endif

    // [refactor-todo] check if there is a way to move the following block to esp_system startup
    heap_caps_enable_nonos_stack_heaps();

    // Now we have startup stack RAM available for heap, enable any DMA pool memory
#if CONFIG_SPIRAM_MALLOC_RESERVE_INTERNAL
    if (g_spiram_ok) {
        esp_err_t r = esp_spiram_reserve_dma_pool(CONFIG_SPIRAM_MALLOC_RESERVE_INTERNAL);
        if (r != ESP_OK) {
            ESP_EARLY_LOGE(TAG, "Could not reserve internal/DMA pool (error 0x%x)", r);
            abort();
        }
    }
#endif

    //Initialize task wdt if configured to do so
#ifdef CONFIG_ESP_TASK_WDT_PANIC
    ESP_ERROR_CHECK(esp_task_wdt_init(CONFIG_ESP_TASK_WDT_TIMEOUT_S, true));
#elif CONFIG_ESP_TASK_WDT
    ESP_ERROR_CHECK(esp_task_wdt_init(CONFIG_ESP_TASK_WDT_TIMEOUT_S, false));
#endif

    //Add IDLE 0 to task wdt
#ifdef CONFIG_ESP_TASK_WDT_CHECK_IDLE_TASK_CPU0
    TaskHandle_t idle_0 = xTaskGetIdleTaskHandleForCPU(0);
    if(idle_0 != NULL){
        ESP_ERROR_CHECK(esp_task_wdt_add(idle_0));
    }
#endif
    //Add IDLE 1 to task wdt
#ifdef CONFIG_ESP_TASK_WDT_CHECK_IDLE_TASK_CPU1
    TaskHandle_t idle_1 = xTaskGetIdleTaskHandleForCPU(1);
    if(idle_1 != NULL){
        ESP_ERROR_CHECK(esp_task_wdt_add(idle_1));
    }
#endif

    app_main();
    vTaskDelete(NULL);
}

/* App Start-up */
void esp_startup_start_app(void)
{
    esp_startup_start_app_common();

    ESP_LOGI(TAG, "Starting scheduler.");

    /* TODO we might not need this since normal ThreadX app starts from tx_enter_kernel() in main() */
    vTaskStartScheduler();
}
