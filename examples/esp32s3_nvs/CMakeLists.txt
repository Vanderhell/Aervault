cmake_minimum_required(VERSION 3.16)
set(EXTRA_COMPONENT_DIRS
    "${CMAKE_CURRENT_LIST_DIR}/../../components/aervault_core"
    "${CMAKE_CURRENT_LIST_DIR}/../../components/aervault_counter"
    "${CMAKE_CURRENT_LIST_DIR}/../../components/aervault_nvs"
    "${CMAKE_CURRENT_LIST_DIR}/../../components/aervault_nor"
    "${CMAKE_CURRENT_LIST_DIR}/../../components/microflash"
    "${CMAKE_CURRENT_LIST_DIR}/../../components/aervault_journal"
    "${CMAKE_CURRENT_LIST_DIR}/../../components/aervault_seal"
    "${CMAKE_CURRENT_LIST_DIR}/../../components/aervault_pair"
    "${CMAKE_CURRENT_LIST_DIR}/../../components/aervault_pair_x25519"
    "${CMAKE_CURRENT_LIST_DIR}/../../components/microcrypt"
    "${CMAKE_CURRENT_LIST_DIR}/../../components/microdh")
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(aervault_esp32s3_nvs)
