idf_component_register(SRCS "../../src/seal.c"
                       INCLUDE_DIRS "../../include"
                       REQUIRES aervault_counter microcrypt)
target_compile_features(${COMPONENT_LIB} PUBLIC c_std_99)
