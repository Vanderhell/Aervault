idf_component_register(SRCS "../../src/aervault.c"
                       INCLUDE_DIRS "../../include"
                       PRIV_INCLUDE_DIRS "../../src")
target_compile_features(${COMPONENT_LIB} PUBLIC c_std_99)
