idf_component_register(SRCS "../../src/journal.c"
                       INCLUDE_DIRS "../../include")
target_compile_features(${COMPONENT_LIB} PUBLIC c_std_99)
