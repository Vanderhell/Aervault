idf_component_register(SRCS "../../src/pair_x25519.c"
                       INCLUDE_DIRS "../../include"
                       REQUIRES aervault_pair microdh)
target_compile_features(${COMPONENT_LIB} PUBLIC c_std_99)
target_compile_definitions(${COMPONENT_LIB} PUBLIC AV_PAIR_HAS_X25519=1)
