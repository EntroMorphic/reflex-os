set(REFLEX_PLATFORM_SRCS "reflex_hal_esp32c6.c" "reflex_kv_esp32c6.c"
                        "reflex_crypto_esp32c6.c")

if(CONFIG_REFLEX_KERNEL_SCHEDULER)
    # Reflex kernel replaces FreeRTOS's task/queue/event implementation.
    # Our compat layer provides all FreeRTOS symbols backed by reflex_sched.
    # reflex_task_esp32c6.c (FreeRTOS backend) is NOT compiled — our
    # scheduler IS the task backend.
    list(APPEND REFLEX_PLATFORM_SRCS "../../kernel/reflex_sched.c"
                                     "../../kernel/reflex_task_kernel.c"
                                     "../../kernel/reflex_freertos_compat.c"
                                     "../../kernel/reflex_freertos_shim.c")
else()
    list(APPEND REFLEX_PLATFORM_SRCS "reflex_task_esp32c6.c")
endif()

set(REFLEX_PLATFORM_REQUIRES driver nvs_flash log)
set(REFLEX_PLATFORM_PRIV_REQUIRES esp_driver_tsens)

# Radio backend selection
if(CONFIG_REFLEX_RADIO_802154)
    list(APPEND REFLEX_PLATFORM_SRCS "reflex_radio_802154.c")
    list(APPEND REFLEX_PLATFORM_REQUIRES ieee802154)
else()
    list(APPEND REFLEX_PLATFORM_SRCS "reflex_radio_esp32c6.c")
    list(APPEND REFLEX_PLATFORM_REQUIRES esp_wifi)
endif()

idf_component_register(SRCS ${REFLEX_PLATFORM_SRCS}
                       INCLUDE_DIRS "../../include" "../../kernel"
                       REQUIRES ${REFLEX_PLATFORM_REQUIRES}
                       PRIV_REQUIRES ${REFLEX_PLATFORM_PRIV_REQUIRES})

target_compile_definitions(${COMPONENT_LIB} PUBLIC "REFLEX_RTC_DATA_ATTR=__attribute__((section(\".rtc.data\")))")

if(CONFIG_REFLEX_KERNEL_SCHEDULER)
    # --wrap forces ALL calls to these functions to go through our
    # __wrap_ implementations, regardless of link order. This is
    # the only reliable way to override FreeRTOS on ESP-IDF.
    target_link_libraries(${COMPONENT_LIB} INTERFACE
        "-Wl,--wrap=xPortStartScheduler"
        "-Wl,--wrap=vTaskStartScheduler"
        "-Wl,--wrap=xTaskCreatePinnedToCore"
        "-Wl,--wrap=xTaskCreateStaticPinnedToCore"
        "-Wl,--wrap=vTaskDelay"
        "-Wl,--wrap=vTaskDelete"
        "-Wl,--wrap=xTaskGetCurrentTaskHandle"
        "-Wl,--wrap=xTaskGetCurrentTaskHandleForCore"
        "-Wl,--wrap=xTaskGetSchedulerState"
        "-Wl,--wrap=xTaskGetTickCount"
        "-Wl,--wrap=xTaskGetTickCountFromISR"
        "-Wl,--wrap=uxTaskGetNumberOfTasks"
        "-Wl,--wrap=pcTaskGetName"
        "-Wl,--wrap=vTaskSuspendAll"
        "-Wl,--wrap=xTaskResumeAll"
        "-Wl,--wrap=xTaskIncrementTick"
        "-Wl,--wrap=vTaskSwitchContext"
        "-Wl,--wrap=vTaskSetTimeOutState"
        "-Wl,--wrap=vTaskInternalSetTimeOutState"
        "-Wl,--wrap=xTaskCheckForTimeOut"
        "-Wl,--wrap=vTaskPlaceOnEventList"
        "-Wl,--wrap=vTaskPlaceOnUnorderedEventList"
        "-Wl,--wrap=xTaskRemoveFromEventList"
        "-Wl,--wrap=vTaskRemoveFromUnorderedEventList"
        "-Wl,--wrap=xTaskPriorityInherit"
        "-Wl,--wrap=xTaskPriorityDisinherit"
        "-Wl,--wrap=pvTaskIncrementMutexHeldCount"
        "-Wl,--wrap=pvTaskGetThreadLocalStoragePointer"
        "-Wl,--wrap=vTaskSetThreadLocalStoragePointerAndDelCallback"
        "-Wl,--wrap=xQueueGenericCreate"
        "-Wl,--wrap=xQueueGenericSend"
        "-Wl,--wrap=xQueueGenericSendFromISR"
        "-Wl,--wrap=xQueueReceive"
        "-Wl,--wrap=xQueueReceiveFromISR"
        "-Wl,--wrap=xQueueGiveFromISR"
        "-Wl,--wrap=xQueueSemaphoreTake"
        "-Wl,--wrap=xQueueCreateMutex"
        "-Wl,--wrap=xQueueGiveMutexRecursive"
        "-Wl,--wrap=xQueueTakeMutexRecursive"
        "-Wl,--wrap=xQueueCreateCountingSemaphore"
        "-Wl,--wrap=xEventGroupCreate"
        "-Wl,--wrap=xEventGroupWaitBits"
        "-Wl,--wrap=xEventGroupSetBits"
        "-Wl,--wrap=xEventGroupClearBits"
        "-Wl,--wrap=vPortEnterCritical"
        "-Wl,--wrap=vPortExitCritical"
        "-Wl,--wrap=pvPortMalloc"
        "-Wl,--wrap=vPortFree"
        "-Wl,--wrap=xPortInIsrContext"
    )
endif()
