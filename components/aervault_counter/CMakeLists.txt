idf_component_register(SRCS "../../src/counter.c"
                       INCLUDE_DIRS "../../include"
                       PRIV_INCLUDE_DIRS "../../src"
                       REQUIRES aervault_core)
target_compile_features(${COMPONENT_LIB} PUBLIC c_std_99)
