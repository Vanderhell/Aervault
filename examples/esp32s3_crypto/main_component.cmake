get_filename_component(_av_common_main "${CMAKE_CURRENT_LIST_DIR}/main" ABSOLUTE)
idf_component_register(SRCS "${_av_common_main}/main.c"
                       INCLUDE_DIRS "${_av_common_main}"
                       REQUIRES ${_av_main_requires})
target_compile_features(${COMPONENT_LIB} PUBLIC c_std_99)
