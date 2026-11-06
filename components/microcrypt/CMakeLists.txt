include(FetchContent)
set(AV_MICROCRYPT_SOURCE_DIR "" CACHE PATH "Existing pinned microcrypt source checkout")
if(DEFINED ENV{AV_MICROCRYPT_SOURCE_DIR})
    set(AV_MICROCRYPT_SOURCE_DIR "$ENV{AV_MICROCRYPT_SOURCE_DIR}")
endif()
if(AV_MICROCRYPT_SOURCE_DIR)
    set(_av_microcrypt_src "${AV_MICROCRYPT_SOURCE_DIR}")
else()
    FetchContent_Declare(av_microcrypt_src
        GIT_REPOSITORY https://github.com/Vanderhell/microcrypt.git
        GIT_TAG a492e7ea36d77bb7ab89ee9b4c1dfc8c4d017124)
    FetchContent_GetProperties(av_microcrypt_src)
    if(NOT av_microcrypt_src_POPULATED)
        FetchContent_Populate(av_microcrypt_src)
    endif()
    set(_av_microcrypt_src "${av_microcrypt_src_SOURCE_DIR}")
endif()
file(TO_CMAKE_PATH "${_av_microcrypt_src}" _av_microcrypt_src_norm)
idf_component_register(SRCS "${_av_microcrypt_src_norm}/src/mcrypt.c"
                       INCLUDE_DIRS "${_av_microcrypt_src_norm}/include")
target_compile_features(${COMPONENT_LIB} PUBLIC c_std_99)
target_compile_definitions(${COMPONENT_LIB} PUBLIC
    MICROCRYPT_ENABLE_AES_CBC=0
    MICROCRYPT_ENABLE_AES_GCM=$<BOOL:${CONFIG_AV_FOOTPRINT_SEAL}>)
