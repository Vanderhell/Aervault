idf_component_register(SRCS "../../src/auth.c"
                       INCLUDE_DIRS "../../include"
                       REQUIRES microcrypt)
target_compile_features(${COMPONENT_LIB} PUBLIC c_std_99)
