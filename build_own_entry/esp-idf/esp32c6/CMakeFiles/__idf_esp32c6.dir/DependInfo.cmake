
# Consider dependencies only in project.
set(CMAKE_DEPENDS_IN_PROJECT_ONLY OFF)

# The set of languages for which implicit dependencies are needed:
set(CMAKE_DEPENDS_LANGUAGES
  "ASM"
  )
# The set of files for implicit dependencies of each language:
set(CMAKE_DEPENDS_CHECK_ASM
  "/Users/aaronjosserand-austin/Projects/reflex-os/kernel/reflex_portasm.S" "/Users/aaronjosserand-austin/Projects/reflex-os/build_own_entry/esp-idf/esp32c6/CMakeFiles/__idf_esp32c6.dir/__/__/kernel/reflex_portasm.S.obj"
  "/Users/aaronjosserand-austin/Projects/reflex-os/kernel/reflex_vectors.S" "/Users/aaronjosserand-austin/Projects/reflex-os/build_own_entry/esp-idf/esp32c6/CMakeFiles/__idf_esp32c6.dir/__/__/kernel/reflex_vectors.S.obj"
  )
set(CMAKE_ASM_COMPILER_ID "GNU")

# Preprocessor definitions for this target.
set(CMAKE_TARGET_DEFINITIONS_ASM
  "ESP_PLATFORM"
  "IDF_VER=\"v5.5\""
  "REFLEX_RTC_DATA_ATTR=__attribute__((section(\".rtc.data\")))"
  "SOC_MMU_PAGE_SIZE=CONFIG_MMU_PAGE_SIZE"
  "SOC_XTAL_FREQ_MHZ=CONFIG_XTAL_FREQ"
  "_GLIBCXX_HAVE_POSIX_SEMAPHORE"
  "_GLIBCXX_USE_POSIX_SEMAPHORE"
  "_GNU_SOURCE"
  "_POSIX_READER_WRITER_LOCKS"
  )

# The include file search paths:
set(CMAKE_ASM_TARGET_INCLUDE_PATH
  "config"
  "/Users/aaronjosserand-austin/Projects/reflex-os/include"
  "/Users/aaronjosserand-austin/Projects/reflex-os/kernel"
  "/Users/aaronjosserand-austin/Projects/esp-idf/components/newlib/platform_include"
  "/Users/aaronjosserand-austin/Projects/esp-idf/components/freertos/config/include"
  "/Users/aaronjosserand-austin/Projects/esp-idf/components/freertos/config/include/freertos"
  "/Users/aaronjosserand-austin/Projects/esp-idf/components/freertos/config/riscv/include"
  "/Users/aaronjosserand-austin/Projects/esp-idf/components/freertos/FreeRTOS-Kernel/include"
  "/Users/aaronjosserand-austin/Projects/esp-idf/components/freertos/FreeRTOS-Kernel/portable/riscv/include"
  "/Users/aaronjosserand-austin/Projects/esp-idf/components/freertos/FreeRTOS-Kernel/portable/riscv/include/freertos"
  "/Users/aaronjosserand-austin/Projects/esp-idf/components/freertos/esp_additions/include"
  "/Users/aaronjosserand-austin/Projects/esp-idf/components/esp_hw_support/include"
  "/Users/aaronjosserand-austin/Projects/esp-idf/components/esp_hw_support/include/soc"
  "/Users/aaronjosserand-austin/Projects/esp-idf/components/esp_hw_support/include/soc/esp32c6"
  "/Users/aaronjosserand-austin/Projects/esp-idf/components/esp_hw_support/dma/include"
  "/Users/aaronjosserand-austin/Projects/esp-idf/components/esp_hw_support/ldo/include"
  "/Users/aaronjosserand-austin/Projects/esp-idf/components/esp_hw_support/debug_probe/include"
  "/Users/aaronjosserand-austin/Projects/esp-idf/components/esp_hw_support/mspi_timing_tuning/include"
  "/Users/aaronjosserand-austin/Projects/esp-idf/components/esp_hw_support/mspi_timing_tuning/tuning_scheme_impl/include"
  "/Users/aaronjosserand-austin/Projects/esp-idf/components/esp_hw_support/power_supply/include"
  "/Users/aaronjosserand-austin/Projects/esp-idf/components/esp_hw_support/port/esp32c6/."
  "/Users/aaronjosserand-austin/Projects/esp-idf/components/esp_hw_support/port/esp32c6/include"
  "/Users/aaronjosserand-austin/Projects/esp-idf/components/esp_hw_support/port/esp32c6/private_include"
  "/Users/aaronjosserand-austin/Projects/esp-idf/components/heap/include"
  "/Users/aaronjosserand-austin/Projects/esp-idf/components/heap/tlsf"
  "/Users/aaronjosserand-austin/Projects/esp-idf/components/log/include"
  "/Users/aaronjosserand-austin/Projects/esp-idf/components/soc/include"
  "/Users/aaronjosserand-austin/Projects/esp-idf/components/soc/esp32c6"
  "/Users/aaronjosserand-austin/Projects/esp-idf/components/soc/esp32c6/include"
  "/Users/aaronjosserand-austin/Projects/esp-idf/components/soc/esp32c6/register"
  "/Users/aaronjosserand-austin/Projects/esp-idf/components/hal/platform_port/include"
  "/Users/aaronjosserand-austin/Projects/esp-idf/components/hal/esp32c6/include"
  "/Users/aaronjosserand-austin/Projects/esp-idf/components/hal/include"
  "/Users/aaronjosserand-austin/Projects/esp-idf/components/esp_rom/include"
  "/Users/aaronjosserand-austin/Projects/esp-idf/components/esp_rom/esp32c6/include"
  "/Users/aaronjosserand-austin/Projects/esp-idf/components/esp_rom/esp32c6/include/esp32c6"
  "/Users/aaronjosserand-austin/Projects/esp-idf/components/esp_rom/esp32c6"
  "/Users/aaronjosserand-austin/Projects/esp-idf/components/esp_common/include"
  "/Users/aaronjosserand-austin/Projects/esp-idf/components/esp_system/include"
  "/Users/aaronjosserand-austin/Projects/esp-idf/components/esp_system/port/soc"
  "/Users/aaronjosserand-austin/Projects/esp-idf/components/esp_system/port/include/riscv"
  "/Users/aaronjosserand-austin/Projects/esp-idf/components/esp_system/port/include/private"
  "/Users/aaronjosserand-austin/Projects/esp-idf/components/riscv/include"
  "/Users/aaronjosserand-austin/Projects/esp-idf/components/lwip/include"
  "/Users/aaronjosserand-austin/Projects/esp-idf/components/lwip/include/apps"
  "/Users/aaronjosserand-austin/Projects/esp-idf/components/lwip/include/apps/sntp"
  "/Users/aaronjosserand-austin/Projects/esp-idf/components/lwip/lwip/src/include"
  "/Users/aaronjosserand-austin/Projects/esp-idf/components/lwip/port/include"
  "/Users/aaronjosserand-austin/Projects/esp-idf/components/lwip/port/freertos/include"
  "/Users/aaronjosserand-austin/Projects/esp-idf/components/lwip/port/esp32xx/include"
  "/Users/aaronjosserand-austin/Projects/esp-idf/components/lwip/port/esp32xx/include/arch"
  "/Users/aaronjosserand-austin/Projects/esp-idf/components/lwip/port/esp32xx/include/sys"
  "/Users/aaronjosserand-austin/Projects/esp-idf/components/ieee802154/include"
  "/Users/aaronjosserand-austin/Projects/esp-idf/components/esp_coex/include"
  "/Users/aaronjosserand-austin/Projects/esp-idf/components/nvs_flash/include"
  "/Users/aaronjosserand-austin/Projects/esp-idf/components/esp_partition/include"
  "/Users/aaronjosserand-austin/Projects/esp-idf/components/esp_wifi/include"
  "/Users/aaronjosserand-austin/Projects/esp-idf/components/esp_wifi/include/local"
  "/Users/aaronjosserand-austin/Projects/esp-idf/components/esp_wifi/wifi_apps/include"
  "/Users/aaronjosserand-austin/Projects/esp-idf/components/esp_wifi/wifi_apps/nan_app/include"
  "/Users/aaronjosserand-austin/Projects/esp-idf/components/esp_event/include"
  "/Users/aaronjosserand-austin/Projects/esp-idf/components/esp_phy/include"
  "/Users/aaronjosserand-austin/Projects/esp-idf/components/esp_phy/esp32c6/include"
  "/Users/aaronjosserand-austin/Projects/esp-idf/components/esp_netif/include"
  )

# The set of dependency files which are needed:
set(CMAKE_DEPENDS_DEPENDENCY_FILES
  "/Users/aaronjosserand-austin/Projects/reflex-os/kernel/reflex_freertos_compat.c" "esp-idf/esp32c6/CMakeFiles/__idf_esp32c6.dir/__/__/kernel/reflex_freertos_compat.c.obj" "gcc" "esp-idf/esp32c6/CMakeFiles/__idf_esp32c6.dir/__/__/kernel/reflex_freertos_compat.c.obj.d"
  "/Users/aaronjosserand-austin/Projects/reflex-os/kernel/reflex_kernel_test.c" "esp-idf/esp32c6/CMakeFiles/__idf_esp32c6.dir/__/__/kernel/reflex_kernel_test.c.obj" "gcc" "esp-idf/esp32c6/CMakeFiles/__idf_esp32c6.dir/__/__/kernel/reflex_kernel_test.c.obj.d"
  "/Users/aaronjosserand-austin/Projects/reflex-os/kernel/reflex_kqueue.c" "esp-idf/esp32c6/CMakeFiles/__idf_esp32c6.dir/__/__/kernel/reflex_kqueue.c.obj" "gcc" "esp-idf/esp32c6/CMakeFiles/__idf_esp32c6.dir/__/__/kernel/reflex_kqueue.c.obj.d"
  "/Users/aaronjosserand-austin/Projects/reflex-os/kernel/reflex_sched.c" "esp-idf/esp32c6/CMakeFiles/__idf_esp32c6.dir/__/__/kernel/reflex_sched.c.obj" "gcc" "esp-idf/esp32c6/CMakeFiles/__idf_esp32c6.dir/__/__/kernel/reflex_sched.c.obj.d"
  "/Users/aaronjosserand-austin/Projects/reflex-os/kernel/reflex_startup.c" "esp-idf/esp32c6/CMakeFiles/__idf_esp32c6.dir/__/__/kernel/reflex_startup.c.obj" "gcc" "esp-idf/esp32c6/CMakeFiles/__idf_esp32c6.dir/__/__/kernel/reflex_startup.c.obj.d"
  "/Users/aaronjosserand-austin/Projects/reflex-os/kernel/reflex_task_kernel.c" "esp-idf/esp32c6/CMakeFiles/__idf_esp32c6.dir/__/__/kernel/reflex_task_kernel.c.obj" "gcc" "esp-idf/esp32c6/CMakeFiles/__idf_esp32c6.dir/__/__/kernel/reflex_task_kernel.c.obj.d"
  "/Users/aaronjosserand-austin/Projects/reflex-os/kernel/reflex_trap.c" "esp-idf/esp32c6/CMakeFiles/__idf_esp32c6.dir/__/__/kernel/reflex_trap.c.obj" "gcc" "esp-idf/esp32c6/CMakeFiles/__idf_esp32c6.dir/__/__/kernel/reflex_trap.c.obj.d"
  "/Users/aaronjosserand-austin/Projects/reflex-os/platform/reflex_crypto.c" "esp-idf/esp32c6/CMakeFiles/__idf_esp32c6.dir/__/reflex_crypto.c.obj" "gcc" "esp-idf/esp32c6/CMakeFiles/__idf_esp32c6.dir/__/reflex_crypto.c.obj.d"
  "/Users/aaronjosserand-austin/Projects/reflex-os/platform/reflex_log_format.c" "esp-idf/esp32c6/CMakeFiles/__idf_esp32c6.dir/__/reflex_log_format.c.obj" "gcc" "esp-idf/esp32c6/CMakeFiles/__idf_esp32c6.dir/__/reflex_log_format.c.obj.d"
  "/Users/aaronjosserand-austin/Projects/reflex-os/platform/esp32c6/reflex_hal_esp32c6.c" "esp-idf/esp32c6/CMakeFiles/__idf_esp32c6.dir/reflex_hal_esp32c6.c.obj" "gcc" "esp-idf/esp32c6/CMakeFiles/__idf_esp32c6.dir/reflex_hal_esp32c6.c.obj.d"
  "/Users/aaronjosserand-austin/Projects/reflex-os/platform/esp32c6/reflex_kv_flash.c" "esp-idf/esp32c6/CMakeFiles/__idf_esp32c6.dir/reflex_kv_flash.c.obj" "gcc" "esp-idf/esp32c6/CMakeFiles/__idf_esp32c6.dir/reflex_kv_flash.c.obj.d"
  "/Users/aaronjosserand-austin/Projects/reflex-os/platform/esp32c6/reflex_radio_802154.c" "esp-idf/esp32c6/CMakeFiles/__idf_esp32c6.dir/reflex_radio_802154.c.obj" "gcc" "esp-idf/esp32c6/CMakeFiles/__idf_esp32c6.dir/reflex_radio_802154.c.obj.d"
  )

# Targets to which this target links which contain Fortran sources.
set(CMAKE_Fortran_TARGET_LINKED_INFO_FILES
  )

# Targets to which this target links which contain Fortran sources.
set(CMAKE_Fortran_TARGET_FORWARD_LINKED_INFO_FILES
  )

# Fortran module output directory.
set(CMAKE_Fortran_TARGET_MODULE_DIR "")
