cmake_minimum_required(VERSION 3.15)
project(aervault VERSION 0.1.0 LANGUAGES C)
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)
option(AV_TESTS "Build host fault-injection tests" OFF)
option(AV_SANITIZERS "Instrument Aervault and host tests with ASan/UBSan" OFF)
if(POLICY CMP0169)
    cmake_policy(SET CMP0169 OLD)
endif()
add_library(aervault_core STATIC src/aervault.c)
add_library(aervault::core ALIAS aervault_core)
add_library(aervault_counter STATIC src/counter.c)
add_library(aervault::counter ALIAS aervault_counter)
target_include_directories(aervault_core
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    PRIVATE src)
target_include_directories(aervault_counter
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    PRIVATE src)
target_link_libraries(aervault_counter PUBLIC aervault_core)
add_library(aervault INTERFACE)
target_link_libraries(aervault INTERFACE aervault_core aervault_counter)
set_target_properties(aervault_core PROPERTIES
    C_STANDARD 99 C_STANDARD_REQUIRED YES C_EXTENSIONS NO EXPORT_NAME core)
set_target_properties(aervault_counter PROPERTIES
    C_STANDARD 99 C_STANDARD_REQUIRED YES C_EXTENSIONS NO EXPORT_NAME counter)
set_target_properties(aervault PROPERTIES EXPORT_NAME aervault)

install(TARGETS aervault_core aervault_counter aervault
    EXPORT aervaultTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(FILES include/aervault.h DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(FILES LICENSE NOTICE
    DESTINATION ${CMAKE_INSTALL_DATADIR}/licenses/aervault)
install(EXPORT aervaultTargets
    FILE aervaultTargets.cmake
    NAMESPACE aervault::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/aervault)
configure_package_config_file(cmake/aervaultConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/aervaultConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/aervault)
write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/aervaultConfigVersion.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion)
install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/aervaultConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/aervaultConfigVersion.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/aervault)

# Optional adapters are intentionally not part of core/counter. Supply installed
# CMake package targets from the application/toolchain when enabling them.
option(AV_ENABLE_AUTH "Build HMAC-SHA256 tag adapter" OFF)
option(AV_ENABLE_SEAL "Build AES-GCM envelope adapter" OFF)
option(AV_ENABLE_PAIR "Build MCU peer key derivation adapter" OFF)
option(AV_PAIR_X25519 "Link microdh for applications using X25519 pairing" OFF)
option(AV_ENABLE_NOR "Build NOR partition adapter (microflash)" OFF)
option(AV_ENABLE_JOURNAL "Build shared transactional append journal" OFF)
option(AV_ENABLE_WAL "Build write-ahead transaction module" OFF)
option(AV_ENABLE_QUEUE "Build durable item queue module" OFF)
option(AV_ENABLE_HISTORY "Build bounded confirmed-revision history" OFF)
option(AV_ENABLE_BUDGET "Build write budget module" OFF)
option(AV_ENABLE_DIAG "Build persistent diagnostic log" OFF)
if(AV_ENABLE_AUTH OR AV_ENABLE_SEAL OR AV_ENABLE_PAIR)
    include(FetchContent)
    FetchContent_Declare(microcrypt
        GIT_REPOSITORY https://github.com/Vanderhell/microcrypt.git
        GIT_TAG a492e7ea36d77bb7ab89ee9b4c1dfc8c4d017124)
    set(MICROCRYPT_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(MICROCRYPT_BUILD_DIFFERENTIAL_TESTS OFF CACHE BOOL "" FORCE)
    set(MICROCRYPT_ENABLE_AES_CBC OFF CACHE BOOL "" FORCE)
    set(MICROCRYPT_ENABLE_AES_GCM ${AV_ENABLE_SEAL} CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(microcrypt)
endif()
if(AV_ENABLE_AUTH)
    add_library(aervault_auth STATIC src/auth.c)
    add_library(aervault::auth ALIAS aervault_auth)
    target_include_directories(aervault_auth PUBLIC include)
    target_link_libraries(aervault_auth PUBLIC microcrypt::microcrypt)
    set_target_properties(aervault_auth PROPERTIES C_STANDARD 99 C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
endif()
if(AV_ENABLE_SEAL)
    add_library(aervault_seal STATIC src/seal.c)
    add_library(aervault::seal ALIAS aervault_seal)
    target_include_directories(aervault_seal PUBLIC include)
    target_link_libraries(aervault_seal PUBLIC aervault_counter microcrypt::microcrypt)
    set_target_properties(aervault_seal PROPERTIES C_STANDARD 99 C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
endif()
if(AV_ENABLE_PAIR)
    add_library(aervault_pair STATIC src/pair.c)
    add_library(aervault::pair ALIAS aervault_pair)
    target_include_directories(aervault_pair PUBLIC include)
    target_link_libraries(aervault_pair PUBLIC microcrypt::microcrypt)
    if(AV_PAIR_X25519)
        include(FetchContent)
        FetchContent_Declare(microdh
            GIT_REPOSITORY https://github.com/Vanderhell/microdh.git
            GIT_TAG e2b16dee4cdc6888608b6146a67cdf43d36f65ab)
        FetchContent_MakeAvailable(microdh)
        if(TARGET microdh::microdh)
            target_link_libraries(aervault_pair PUBLIC microdh::microdh)
        elseif(TARGET microdh)
            target_link_libraries(aervault_pair PUBLIC microdh)
        else()
            message(FATAL_ERROR "Pinned microdh did not define microdh::microdh or microdh")
        endif()
        target_compile_definitions(aervault_pair PUBLIC AV_PAIR_HAS_X25519=1)
    endif()
    set_target_properties(aervault_pair PROPERTIES C_STANDARD 99 C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
endif()

# Storage modules are opt-in. The shared journal is a small Aervault format
# that can be used by WAL, queue, history and diagnostic targets.
if(AV_ENABLE_NOR)
    include(FetchContent)
    FetchContent_Declare(microflash
        GIT_REPOSITORY https://github.com/Vanderhell/microflash.git
        GIT_TAG 7730387307451db9c86c42e9681263f135949cce)
    FetchContent_GetProperties(microflash)
    if(NOT microflash_POPULATED)
        FetchContent_Populate(microflash)
    endif()
    # Consume only the upstream library source/header; its tests are not added
    # to Aervault's build graph.
    add_library(microflash STATIC ${microflash_SOURCE_DIR}/src/mflash.c)
    add_library(microflash::microflash ALIAS microflash)
    target_include_directories(microflash PUBLIC ${microflash_SOURCE_DIR}/include)
    set_target_properties(microflash PROPERTIES C_STANDARD 99 C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
    add_library(aervault_nor STATIC components/aervault_nor/aervault_nor.c)
    add_library(aervault::nor ALIAS aervault_nor)
    target_include_directories(aervault_nor PUBLIC include)
    target_link_libraries(aervault_nor PUBLIC microflash::microflash)
endif()
if(AV_ENABLE_JOURNAL OR AV_ENABLE_WAL OR AV_ENABLE_QUEUE OR AV_ENABLE_HISTORY OR AV_ENABLE_DIAG)
    add_library(aervault_journal STATIC src/journal.c)
    add_library(aervault::journal ALIAS aervault_journal)
    target_include_directories(aervault_journal PUBLIC include)
    set_target_properties(aervault_journal PROPERTIES C_STANDARD 99 C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
endif()
if(AV_ENABLE_WAL)
    add_library(aervault_wal STATIC src/wal.c)
    add_library(aervault::wal ALIAS aervault_wal)
    target_include_directories(aervault_wal PUBLIC include)
    target_link_libraries(aervault_wal PUBLIC aervault_journal)
endif()
if(AV_ENABLE_QUEUE)
    add_library(aervault_queue STATIC src/queue.c)
    add_library(aervault::queue ALIAS aervault_queue)
    target_include_directories(aervault_queue PUBLIC include)
    target_link_libraries(aervault_queue PUBLIC aervault_journal)
endif()
if(AV_ENABLE_HISTORY)
    add_library(aervault_history STATIC src/history.c)
    add_library(aervault::history ALIAS aervault_history)
    target_include_directories(aervault_history PUBLIC include)
    target_link_libraries(aervault_history PUBLIC aervault_journal aervault_core)
endif()
if(AV_ENABLE_BUDGET)
    add_library(aervault_budget STATIC src/budget.c)
    add_library(aervault::budget ALIAS aervault_budget)
    target_include_directories(aervault_budget PUBLIC include)
    if(AV_ENABLE_NOR)
        target_link_libraries(aervault_budget PUBLIC aervault_nor)
    endif()
endif()
if(AV_ENABLE_DIAG)
    add_library(aervault_diag STATIC src/diag.c)
    add_library(aervault::diag ALIAS aervault_diag)
    target_include_directories(aervault_diag PUBLIC include)
    target_link_libraries(aervault_diag PUBLIC aervault_journal)
endif()

set(_av_embedded_targets)
foreach(_av_t aervault_journal aervault_wal aervault_queue aervault_history aervault_budget aervault_diag aervault_nor)
    if(TARGET ${_av_t})
        list(APPEND _av_embedded_targets ${_av_t})
        set_target_properties(${_av_t} PROPERTIES C_STANDARD 99 C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
        if(MSVC)
            target_compile_options(${_av_t} PRIVATE /W4 /WX)
        else()
            target_compile_options(${_av_t} PRIVATE -Wall -Wextra -Wpedantic -Werror)
        endif()
    endif()
endforeach()
function(av_add_module_link_check target module_id library)
    if(AV_TESTS)
        add_executable(${target} tests/link_storage.c)
    else()
        add_executable(${target} EXCLUDE_FROM_ALL tests/link_storage.c)
    endif()
    target_compile_definitions(${target} PRIVATE AV_LINK_STORAGE_MODULE=${module_id})
    target_link_libraries(${target} PRIVATE ${library})
    set_target_properties(${target} PROPERTIES C_STANDARD 99 C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
endfunction()
if(TARGET aervault_journal)
    av_add_module_link_check(aervault_link_journal 1 aervault_journal)
endif()
if(TARGET aervault_wal)
    av_add_module_link_check(aervault_link_wal 2 aervault_wal)
endif()
if(TARGET aervault_queue)
    av_add_module_link_check(aervault_link_queue 3 aervault_queue)
endif()
if(TARGET aervault_history)
    av_add_module_link_check(aervault_link_history 4 aervault_history)
endif()
if(TARGET aervault_budget)
    av_add_module_link_check(aervault_link_budget 5 aervault_budget)
endif()
if(TARGET aervault_diag)
    av_add_module_link_check(aervault_link_diag 6 aervault_diag)
endif()
if(TARGET aervault_nor)
    av_add_module_link_check(aervault_link_nor 7 aervault_nor)
endif()
add_executable(aervault_link_core EXCLUDE_FROM_ALL tests/link_core.c)
target_link_libraries(aervault_link_core PRIVATE aervault_core)
add_executable(aervault_link_counter EXCLUDE_FROM_ALL tests/link_counter.c)
target_link_libraries(aervault_link_counter PRIVATE aervault_counter)
if(AV_ENABLE_AUTH)
    add_executable(aervault_link_auth EXCLUDE_FROM_ALL tests/link_auth.c)
    target_link_libraries(aervault_link_auth PRIVATE aervault_auth)
endif()
if(AV_ENABLE_SEAL)
    add_executable(aervault_link_seal EXCLUDE_FROM_ALL tests/link_seal.c)
    target_link_libraries(aervault_link_seal PRIVATE aervault_seal)
endif()
if(AV_ENABLE_PAIR)
    add_executable(aervault_link_pair EXCLUDE_FROM_ALL tests/link_pair.c)
    target_link_libraries(aervault_link_pair PRIVATE aervault_pair)
endif()
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
    if(NOT AV_SANITIZERS AND NOT CMAKE_C_FLAGS MATCHES "sanitize")
        add_test(NAME aervault_install_package
            COMMAND ${CMAKE_COMMAND}
                -DAERVAULT_BUILD_DIR=${CMAKE_BINARY_DIR}
                -DAERVAULT_SOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
                -DAERVAULT_TEST_ROOT=${CMAKE_CURRENT_BINARY_DIR}/install-smoke
                -DAERVAULT_INSTALL_DATADIR=${CMAKE_INSTALL_DATADIR}
                -DAERVAULT_GENERATOR=${CMAKE_GENERATOR}
                -DAERVAULT_GENERATOR_PLATFORM=${CMAKE_GENERATOR_PLATFORM}
                -DAERVAULT_GENERATOR_TOOLSET=${CMAKE_GENERATOR_TOOLSET}
                -DAERVAULT_C_COMPILER=${CMAKE_C_COMPILER}
                -DAERVAULT_BUILD_TYPE=${CMAKE_BUILD_TYPE}
                -DAERVAULT_C_STANDARD_LIBRARIES=${CMAKE_C_STANDARD_LIBRARIES}
                -DAERVAULT_C_FLAGS_DEBUG=${CMAKE_C_FLAGS_DEBUG}
                -DAERVAULT_C_FLAGS_MINSIZEREL=${CMAKE_C_FLAGS_MINSIZEREL}
                -DAERVAULT_C_FLAGS_RELEASE=${CMAKE_C_FLAGS_RELEASE}
                -DAERVAULT_C_FLAGS_RELWITHDEBINFO=${CMAKE_C_FLAGS_RELWITHDEBINFO}
                -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/install_package.cmake)
    endif()

    if(AV_ENABLE_AUTH AND AV_ENABLE_SEAL AND AV_ENABLE_PAIR)
        add_executable(aervault_crypto_test tests/crypto.c)
        target_link_libraries(aervault_crypto_test PRIVATE
            aervault_auth aervault_seal aervault_pair)
        set_target_properties(aervault_crypto_test PROPERTIES
            C_STANDARD 99 C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
        add_test(NAME aervault_crypto_adapters COMMAND aervault_crypto_test)
    endif()
    if(AV_ENABLE_JOURNAL AND AV_ENABLE_WAL AND AV_ENABLE_QUEUE AND AV_ENABLE_HISTORY AND AV_ENABLE_BUDGET AND AV_ENABLE_DIAG)
        add_executable(aervault_storage_test tests/storage.c tests/ram_backend.c)
        target_include_directories(aervault_storage_test PRIVATE tests)
        target_link_libraries(aervault_storage_test PRIVATE aervault_wal aervault_queue aervault_history aervault_budget aervault_diag aervault_core)
        if(AV_ENABLE_NOR)
            target_link_libraries(aervault_storage_test PRIVATE aervault_nor)
            target_compile_definitions(aervault_storage_test PRIVATE AV_TEST_NOR=1)
        endif()
        set_target_properties(aervault_storage_test PROPERTIES C_STANDARD 99 C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
        add_test(NAME aervault_storage COMMAND aervault_storage_test)
        add_executable(aervault_campaign tests/campaign.c)
        target_link_libraries(aervault_campaign PRIVATE aervault_journal aervault_queue aervault_diag aervault_budget)
        set_target_properties(aervault_campaign PROPERTIES C_STANDARD 99 C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
        if(MSVC)
            target_compile_options(aervault_campaign PRIVATE /W4 /WX)
        else()
            target_compile_options(aervault_campaign PRIVATE -Wall -Wextra -Wpedantic -Werror)
        endif()
        add_test(NAME aervault_campaign COMMAND aervault_campaign 10000 "${CMAKE_CURRENT_SOURCE_DIR}/tests/campaign-results.json")
        if(AV_ENABLE_NOR)
            add_executable(aervault_module_faults tests/module_faults.c)
            target_link_libraries(aervault_module_faults PRIVATE
                aervault_queue aervault_history aervault_diag aervault_budget
                aervault_nor aervault_core)
            set_target_properties(aervault_module_faults PROPERTIES
                C_STANDARD 99 C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
            if(MSVC)
                target_compile_options(aervault_module_faults PRIVATE /W4 /WX)
            else()
                target_compile_options(aervault_module_faults PRIVATE -Wall -Wextra -Wpedantic -Werror)
            endif()
            add_test(NAME aervault_module_faults COMMAND aervault_module_faults
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/module-fault-results.json")
        endif()
    endif()
    foreach(_av_check aervault_link_journal aervault_link_wal aervault_link_queue aervault_link_history aervault_link_budget aervault_link_diag aervault_link_nor)
        if(TARGET ${_av_check})
            add_test(NAME ${_av_check} COMMAND ${_av_check})
        endif()
    endforeach()
endif()

if(AV_SANITIZERS)
    if(NOT CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        message(FATAL_ERROR "AV_SANITIZERS requires GCC or Clang")
    endif()
    function(av_instrument_target target)
        if(TARGET ${target})
            get_target_property(_av_type ${target} TYPE)
            if(_av_type STREQUAL "EXECUTABLE")
                target_compile_options(${target} PRIVATE
                    -fsanitize=address,undefined -fno-omit-frame-pointer)
                target_link_options(${target} PRIVATE -fsanitize=address,undefined)
            elseif(_av_type STREQUAL "STATIC_LIBRARY")
                target_compile_options(${target} PRIVATE
                    -fsanitize=address,undefined -fno-omit-frame-pointer)
            endif()
        endif()
    endfunction()
    foreach(_av_sanitized_target
            aervault_core aervault_counter aervault_auth aervault_seal aervault_pair
            aervault_journal aervault_wal aervault_queue aervault_history
            aervault_budget aervault_diag aervault_nor aervault_ram_test
            aervault_test aervault_nvs_router_test aervault_crypto_test
            aervault_storage_test aervault_campaign aervault_module_faults
            aervault_link_core aervault_link_counter aervault_link_auth
            aervault_link_seal aervault_link_pair aervault_link_journal
            aervault_link_wal aervault_link_queue aervault_link_history
            aervault_link_budget aervault_link_diag aervault_link_nor)
        av_instrument_target(${_av_sanitized_target})
    endforeach()
endif()
