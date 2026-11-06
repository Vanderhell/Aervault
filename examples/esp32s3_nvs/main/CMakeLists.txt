idf_component_register(SRCS "main.c"
                                "../../../src/wal.c"
                                "../../../src/queue.c"
                                "../../../src/history.c"
                                "../../../src/diag.c"
                                "../../../src/budget.c"
                       INCLUDE_DIRS "."
                       REQUIRES aervault_core aervault_counter aervault_nvs
                                aervault_nor aervault_journal aervault_seal
                                aervault_pair_x25519 microcrypt microdh nvs_flash
                                esp_partition esp_system esp_driver_usb_serial_jtag)
