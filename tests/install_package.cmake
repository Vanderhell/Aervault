if(NOT DEFINED AERVAULT_BUILD_DIR OR NOT DEFINED AERVAULT_SOURCE_DIR OR
   NOT DEFINED AERVAULT_TEST_ROOT)
    message(FATAL_ERROR "Install smoke test arguments are incomplete")
endif()

set(_prefix "${AERVAULT_TEST_ROOT}/prefix")
set(_consumer_build "${AERVAULT_TEST_ROOT}/consumer-build")
execute_process(
    COMMAND "${CMAKE_COMMAND}" --install "${AERVAULT_BUILD_DIR}"
            --prefix "${_prefix}"
    RESULT_VARIABLE _install_result
    OUTPUT_VARIABLE _install_out
    ERROR_VARIABLE _install_err)
if(NOT _install_result EQUAL 0)
    message(FATAL_ERROR "Aervault install failed:\n${_install_out}\n${_install_err}")
endif()
foreach(_license LICENSE NOTICE)
    if(NOT EXISTS "${_prefix}/${AERVAULT_INSTALL_DATADIR}/licenses/aervault/${_license}")
        message(FATAL_ERROR "Installed package is missing ${_license}")
    endif()
endforeach()

set(_configure_cmd "${CMAKE_COMMAND}" -S "${AERVAULT_SOURCE_DIR}/tests/package_consumer"
    -B "${_consumer_build}" "-DCMAKE_PREFIX_PATH=${_prefix}")
if(DEFINED AERVAULT_C_COMPILER AND NOT AERVAULT_C_COMPILER STREQUAL "")
    list(APPEND _configure_cmd "-DCMAKE_C_COMPILER=${AERVAULT_C_COMPILER}")
endif()
if(DEFINED AERVAULT_C_STANDARD_LIBRARIES)
    list(APPEND _configure_cmd
        "-DCMAKE_C_STANDARD_LIBRARIES=${AERVAULT_C_STANDARD_LIBRARIES}")
endif()
if(DEFINED AERVAULT_GENERATOR AND NOT AERVAULT_GENERATOR STREQUAL "")
    list(APPEND _configure_cmd -G "${AERVAULT_GENERATOR}")
endif()
if(DEFINED AERVAULT_GENERATOR_PLATFORM AND NOT AERVAULT_GENERATOR_PLATFORM STREQUAL "")
    list(APPEND _configure_cmd -A "${AERVAULT_GENERATOR_PLATFORM}")
endif()
if(DEFINED AERVAULT_GENERATOR_TOOLSET AND NOT AERVAULT_GENERATOR_TOOLSET STREQUAL "")
    list(APPEND _configure_cmd -T "${AERVAULT_GENERATOR_TOOLSET}")
endif()
if(DEFINED AERVAULT_BUILD_TYPE AND NOT AERVAULT_BUILD_TYPE STREQUAL "")
    list(APPEND _configure_cmd "-DCMAKE_BUILD_TYPE=${AERVAULT_BUILD_TYPE}")
endif()
foreach(_config DEBUG MINSIZEREL RELEASE RELWITHDEBINFO)
    if(DEFINED AERVAULT_C_FLAGS_${_config})
        list(APPEND _configure_cmd
            "-DCMAKE_C_FLAGS_${_config}=${AERVAULT_C_FLAGS_${_config}}")
    endif()
endforeach()
execute_process(
    COMMAND ${_configure_cmd}
    RESULT_VARIABLE _configure_result
    OUTPUT_VARIABLE _configure_out
    ERROR_VARIABLE _configure_err)
if(NOT _configure_result EQUAL 0)
    message(FATAL_ERROR "Installed package configure failed:\n${_configure_out}\n${_configure_err}")
endif()

execute_process(
    COMMAND "${CMAKE_COMMAND}" --build "${_consumer_build}"
    RESULT_VARIABLE _build_result
    OUTPUT_VARIABLE _build_out
    ERROR_VARIABLE _build_err)
if(NOT _build_result EQUAL 0)
    message(FATAL_ERROR "Installed package consumer build failed:\n${_build_out}\n${_build_err}")
endif()
message(STATUS "Installed core-only and counter consumers built successfully")
