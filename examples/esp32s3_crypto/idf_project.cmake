cmake_minimum_required(VERSION 3.16)
set(IDF_TARGET esp32s3)
set(AERVAULT_ROOT "${CMAKE_CURRENT_LIST_DIR}/../..")
set(EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/variants/${AV_FOOTPRINT_VARIANT}/main"
    "${AERVAULT_ROOT}/components/aervault_core")
if(AV_FOOTPRINT_VARIANT STREQUAL "auth")
    list(APPEND EXTRA_COMPONENT_DIRS "${AERVAULT_ROOT}/components/aervault_auth"
         "${AERVAULT_ROOT}/components/microcrypt")
elseif(AV_FOOTPRINT_VARIANT STREQUAL "seal")
    list(APPEND EXTRA_COMPONENT_DIRS "${AERVAULT_ROOT}/components/aervault_counter"
         "${AERVAULT_ROOT}/components/aervault_seal" "${AERVAULT_ROOT}/components/microcrypt")
elseif(AV_FOOTPRINT_VARIANT STREQUAL "pair")
    list(APPEND EXTRA_COMPONENT_DIRS "${AERVAULT_ROOT}/components/aervault_pair"
         "${AERVAULT_ROOT}/components/aervault_pair_x25519"
         "${AERVAULT_ROOT}/components/microcrypt" "${AERVAULT_ROOT}/components/microdh")
elseif(NOT AV_FOOTPRINT_VARIANT STREQUAL "core")
    message(FATAL_ERROR "Unknown Aervault footprint variant: ${AV_FOOTPRINT_VARIANT}")
endif()
set(SDKCONFIG_DEFAULTS "${CMAKE_BINARY_DIR}/sdkconfig.defaults")
set(COMPONENTS main freertos esp_system log)
