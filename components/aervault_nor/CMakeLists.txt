idf_component_register(SRCS "aervault_nor.c"
                       INCLUDE_DIRS "../../include"
                       REQUIRES aervault_journal microflash)
target_compile_features(${COMPONENT_LIB} PUBLIC c_std_99)
