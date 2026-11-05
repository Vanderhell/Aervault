idf_component_register(SRCS "main.c" INCLUDE_DIRS "."
                       REQUIRES aervault_core aervault_counter aervault_nvs nvs_flash
                                esp_system esp_driver_usb_serial_jtag)
