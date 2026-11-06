idf_component_register(SRCS "main.c" INCLUDE_DIRS "." REQUIRES ${AERVAULT_FOOTPRINT_REQUIRES})
target_compile_features(${COMPONENT_LIB} PUBLIC c_std_99)
