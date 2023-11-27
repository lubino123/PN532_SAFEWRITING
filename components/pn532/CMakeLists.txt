set(COMPONENT_ADD_INCLUDEDIRS .)

set(COMPONENT_SRCS "pn532.c")

idf_component_register(SRC_DIRS "."
    REQUIRES driver
    INCLUDE_DIRS ".")


#register_component()

