/* ESP32-C6 mask-ROM entry point addresses.
 *
 * The companion to include/reflex_rom_esp32c6.h. Reflex calls fourteen ROM
 * routines; this fragment is what resolves them when the link no longer draws
 * on ESP-IDF's own ROM linker scripts.
 *
 * Not yet wired into the link, and deliberately so: ESP-IDF's esp32c6.rom.ld
 * already provides every one of these while the build is still an ESP-IDF
 * build, and adding a second definition would only risk a conflict for no gain
 * today. It is carried and verified now so the addresses are recorded and
 * checked rather than discovered later, when they would be discovered by a
 * board that jumps somewhere wrong.
 *
 * PROVIDE, not assignment: it defines a symbol only if nothing else has, so
 * this stays inert until it is the only source.
 *
 * `make rom-check` compares every address here against the ROM linker scripts
 * ESP-IDF ships for the C6, so drift is a build failure rather than a silent
 * jump into the wrong routine. Several ESP-IDF names are aliases — esp_rom_printf
 * is ets_printf, esp_rom_delay_us is ets_delay_us — and the underlying name is
 * given alongside each.
 */

PROVIDE ( esp_rom_printf                 = 0x40000028 );  /* ets_printf */
PROVIDE ( ets_install_putc1              = 0x4000002c );
PROVIDE ( esp_rom_delay_us               = 0x40000040 );  /* ets_delay_us */
PROVIDE ( esp_rom_output_tx_wait_idle    = 0x40000078 );  /* uart_tx_wait_idle */
PROVIDE ( esp_rom_output_putc            = 0x40000080 );  /* ets_write_char_uart */
PROVIDE ( software_reset                 = 0x40000090 );
PROVIDE ( esp_rom_spiflash_erase_sector  = 0x40000144 );
PROVIDE ( esp_rom_spiflash_write         = 0x4000014c );
PROVIDE ( esp_rom_spiflash_read          = 0x40000150 );
PROVIDE ( Cache_Disable_ICache           = 0x40000690 );
PROVIDE ( Cache_Enable_ICache            = 0x40000694 );
PROVIDE ( Cache_Suspend_ICache           = 0x40000698 );
PROVIDE ( Cache_Resume_ICache            = 0x4000069c );
PROVIDE ( esp_rom_gpio_connect_out_signal = 0x400006dc ); /* gpio_matrix_out */
