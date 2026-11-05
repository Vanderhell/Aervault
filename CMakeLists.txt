cmake_minimum_required(VERSION 3.15)
project(aervault LANGUAGES C)
add_library(aervault_core STATIC src/aervault.c)
add_library(aervault::core ALIAS aervault_core)
add_library(aervault_counter STATIC src/counter.c)
add_library(aervault::counter ALIAS aervault_counter)
target_include_directories(aervault_core PUBLIC include PRIVATE src)
target_include_directories(aervault_counter PUBLIC include PRIVATE src)
target_link_libraries(aervault_counter PUBLIC aervault_core)
add_library(aervault INTERFACE)
target_link_libraries(aervault INTERFACE aervault_core aervault_counter)
set_target_properties(aervault_core aervault_counter PROPERTIES C_STANDARD 99 C_STANDARD_REQUIRED YES C_EXTENSIONS NO)

add_executable(aervault_link_core EXCLUDE_FROM_ALL tests/link_core.c)
target_link_libraries(aervault_link_core PRIVATE aervault_core)
add_executable(aervault_link_counter EXCLUDE_FROM_ALL tests/link_counter.c)
target_link_libraries(aervault_link_counter PRIVATE aervault_counter)
option(AV_TESTS "Build host fault-injection tests" OFF)
if(AV_TESTS)
    enable_testing()
    add_library(aervault_ram_test STATIC tests/ram_backend.c)
    target_include_directories(aervault_ram_test PUBLIC include tests)
    set_target_properties(aervault_ram_test PROPERTIES C_STANDARD 99 C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
    add_executable(aervault_test tests/fault.c)
    target_link_libraries(aervault_test PRIVATE aervault aervault_ram_test)
    set_target_properties(aervault_test PROPERTIES C_STANDARD 99 C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
    add_test(NAME aervault_fault COMMAND aervault_test)

    add_executable(aervault_nvs_router_test
        tests/nvs_router.c
        tests/nvs_stubs/nvs_stubs.c
        components/aervault_nvs/aervault_nvs.c)
    target_include_directories(aervault_nvs_router_test PRIVATE
        tests/nvs_stubs components/aervault_nvs/include)
    target_link_libraries(aervault_nvs_router_test PRIVATE aervault_core)
    set_target_properties(aervault_nvs_router_test PROPERTIES
        C_STANDARD 99 C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
    add_test(NAME aervault_nvs_router COMMAND aervault_nvs_router_test)
endif()
