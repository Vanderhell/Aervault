idf_component_register(SRCS "aervault_nvs.c"
                       INCLUDE_DIRS "include"
                       REQUIRES aervault_core nvs_flash)
target_compile_features(${COMPONENT_LIB} PUBLIC c_std_99)
