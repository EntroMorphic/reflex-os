
# Consider dependencies only in project.
set(CMAKE_DEPENDS_IN_PROJECT_ONLY OFF)

# The set of languages for which implicit dependencies are needed:
set(CMAKE_DEPENDS_LANGUAGES
  "ASM"
  )
# The set of files for implicit dependencies of each language:
set(CMAKE_DEPENDS_CHECK_ASM
  "/Users/aaronjosserand-austin/Projects/reflex-os/build_own_entry/goose_ulp.bin.S" "/Users/aaronjosserand-austin/Projects/reflex-os/build_own_entry/esp-idf/goose/CMakeFiles/__idf_goose.dir/__/__/goose_ulp.bin.S.obj"
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
  "/Users/aaronjosserand-austin/Projects/reflex-os/components/goose/include"
  "/Users/aaronjosserand-austin/Projects/reflex-os/include"
  "esp-idf/goose/goose_ulp"
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
  "/Users/aaronjosserand-austin/Projects/reflex-os/kernel"
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
  "/Users/aaronjosserand-austin/Projects/esp-idf/components/ulp/ulp_common/include"
  "/Users/aaronjosserand-austin/Projects/esp-idf/components/ulp/lp_core/include"
  "/Users/aaronjosserand-austin/Projects/esp-idf/components/ulp/lp_core/shared/include"
  "/Users/aaronjosserand-austin/Projects/esp-idf/components/driver/deprecated"
  "/Users/aaronjosserand-austin/Projects/esp-idf/components/driver/i2c/include"
  "/Users/aaronjosserand-austin/Projects/esp-idf/components/driver/touch_sensor/include"
  "/Users/aaronjosserand-austin/Projects/esp-idf/components/driver/twai/include"
  "/Users/aaronjosserand-austin/Projects/esp-idf/components/esp_pm/include"
  "/Users/aaronjosserand-austin/Projects/esp-idf/components/esp_ringbuf/include"
  "/Users/aaronjosserand-austin/Projects/esp-idf/components/esp_driver_gpio/include"
  "/Users/aaronjosserand-austin/Projects/esp-idf/components/esp_driver_pcnt/include"
  "/Users/aaronjosserand-austin/Projects/esp-idf/components/esp_driver_gptimer/include"
  "/Users/aaronjosserand-austin/Projects/esp-idf/components/esp_driver_spi/include"
  "/Users/aaronjosserand-austin/Projects/esp-idf/components/esp_driver_mcpwm/include"
  "/Users/aaronjosserand-austin/Projects/esp-idf/components/esp_driver_ana_cmpr/include"
  "/Users/aaronjosserand-austin/Projects/esp-idf/components/esp_driver_i2s/include"
  "/Users/aaronjosserand-austin/Projects/esp-idf/components/esp_driver_sdmmc/include"
  "/Users/aaronjosserand-austin/Projects/esp-idf/components/sdmmc/include"
  "/Users/aaronjosserand-austin/Projects/esp-idf/components/esp_driver_sdspi/include"
  "/Users/aaronjosserand-austin/Projects/esp-idf/components/esp_driver_sdio/include"
  "/Users/aaronjosserand-austin/Projects/esp-idf/components/esp_driver_dac/include"
  "/Users/aaronjosserand-austin/Projects/esp-idf/components/esp_driver_rmt/include"
  "/Users/aaronjosserand-austin/Projects/esp-idf/components/esp_driver_tsens/include"
  "/Users/aaronjosserand-austin/Projects/esp-idf/components/esp_driver_sdm/include"
  "/Users/aaronjosserand-austin/Projects/esp-idf/components/esp_driver_i2c/include"
  "/Users/aaronjosserand-austin/Projects/esp-idf/components/esp_driver_uart/include"
  "/Users/aaronjosserand-austin/Projects/esp-idf/components/vfs/include"
  "/Users/aaronjosserand-austin/Projects/esp-idf/components/esp_driver_ledc/include"
  "/Users/aaronjosserand-austin/Projects/esp-idf/components/esp_driver_parlio/include"
  "/Users/aaronjosserand-austin/Projects/esp-idf/components/esp_driver_usb_serial_jtag/include"
  "/Users/aaronjosserand-austin/Projects/esp-idf/components/esp_driver_twai/include"
  "/Users/aaronjosserand-austin/Projects/esp-idf/components/esp_adc/include"
  "/Users/aaronjosserand-austin/Projects/esp-idf/components/esp_adc/interface"
  "/Users/aaronjosserand-austin/Projects/esp-idf/components/esp_adc/esp32c6/include"
  "/Users/aaronjosserand-austin/Projects/esp-idf/components/esp_adc/deprecated/include"
  "/Users/aaronjosserand-austin/Projects/reflex-os/drivers/include"
  "/Users/aaronjosserand-austin/Projects/reflex-os/core/include"
  "/Users/aaronjosserand-austin/Projects/reflex-os/vm/include"
  "/Users/aaronjosserand-austin/Projects/reflex-os/vm/programs"
  "/Users/aaronjosserand-austin/Projects/esp-idf/components/esp_timer/include"
  "/Users/aaronjosserand-austin/Projects/reflex-os/storage/include"
  )

# The set of dependency files which are needed:
set(CMAKE_DEPENDS_DEPENDENCY_FILES
  "/Users/aaronjosserand-austin/Projects/reflex-os/components/goose/goose_atlas.c" "esp-idf/goose/CMakeFiles/__idf_goose.dir/goose_atlas.c.obj" "gcc" "esp-idf/goose/CMakeFiles/__idf_goose.dir/goose_atlas.c.obj.d"
  "/Users/aaronjosserand-austin/Projects/reflex-os/components/goose/goose_atmosphere.c" "esp-idf/goose/CMakeFiles/__idf_goose.dir/goose_atmosphere.c.obj" "gcc" "esp-idf/goose/CMakeFiles/__idf_goose.dir/goose_atmosphere.c.obj.d"
  "/Users/aaronjosserand-austin/Projects/reflex-os/components/goose/goose_lattice.c" "esp-idf/goose/CMakeFiles/__idf_goose.dir/goose_lattice.c.obj" "gcc" "esp-idf/goose/CMakeFiles/__idf_goose.dir/goose_lattice.c.obj.d"
  "/Users/aaronjosserand-austin/Projects/reflex-os/components/goose/goose_library.c" "esp-idf/goose/CMakeFiles/__idf_goose.dir/goose_library.c.obj" "gcc" "esp-idf/goose/CMakeFiles/__idf_goose.dir/goose_library.c.obj.d"
  "/Users/aaronjosserand-austin/Projects/reflex-os/components/goose/goose_metabolic.c" "esp-idf/goose/CMakeFiles/__idf_goose.dir/goose_metabolic.c.obj" "gcc" "esp-idf/goose/CMakeFiles/__idf_goose.dir/goose_metabolic.c.obj.d"
  "/Users/aaronjosserand-austin/Projects/reflex-os/components/goose/goose_mmio_sync.c" "esp-idf/goose/CMakeFiles/__idf_goose.dir/goose_mmio_sync.c.obj" "gcc" "esp-idf/goose/CMakeFiles/__idf_goose.dir/goose_mmio_sync.c.obj.d"
  "/Users/aaronjosserand-austin/Projects/reflex-os/components/goose/goose_policy.c" "esp-idf/goose/CMakeFiles/__idf_goose.dir/goose_policy.c.obj" "gcc" "esp-idf/goose/CMakeFiles/__idf_goose.dir/goose_policy.c.obj.d"
  "/Users/aaronjosserand-austin/Projects/reflex-os/components/goose/goose_registry.c" "esp-idf/goose/CMakeFiles/__idf_goose.dir/goose_registry.c.obj" "gcc" "esp-idf/goose/CMakeFiles/__idf_goose.dir/goose_registry.c.obj.d"
  "/Users/aaronjosserand-austin/Projects/reflex-os/components/goose/goose_runtime.c" "esp-idf/goose/CMakeFiles/__idf_goose.dir/goose_runtime.c.obj" "gcc" "esp-idf/goose/CMakeFiles/__idf_goose.dir/goose_runtime.c.obj.d"
  "/Users/aaronjosserand-austin/Projects/reflex-os/components/goose/goose_shadow_atlas.c" "esp-idf/goose/CMakeFiles/__idf_goose.dir/goose_shadow_atlas.c.obj" "gcc" "esp-idf/goose/CMakeFiles/__idf_goose.dir/goose_shadow_atlas.c.obj.d"
  "/Users/aaronjosserand-austin/Projects/reflex-os/components/goose/goose_supervisor.c" "esp-idf/goose/CMakeFiles/__idf_goose.dir/goose_supervisor.c.obj" "gcc" "esp-idf/goose/CMakeFiles/__idf_goose.dir/goose_supervisor.c.obj.d"
  "/Users/aaronjosserand-austin/Projects/reflex-os/components/goose/goose_telemetry.c" "esp-idf/goose/CMakeFiles/__idf_goose.dir/goose_telemetry.c.obj" "gcc" "esp-idf/goose/CMakeFiles/__idf_goose.dir/goose_telemetry.c.obj.d"
  )

# Targets to which this target links which contain Fortran sources.
set(CMAKE_Fortran_TARGET_LINKED_INFO_FILES
  )

# Targets to which this target links which contain Fortran sources.
set(CMAKE_Fortran_TARGET_FORWARD_LINKED_INFO_FILES
  )

# Fortran module output directory.
set(CMAKE_Fortran_TARGET_MODULE_DIR "")
