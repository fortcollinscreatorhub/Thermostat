# For IDF v4.x:
#idf_component_register(SRCS "smbus.c" INCLUDE_DIRS include)

set(COMPONENT_ADD_INCLUDEDIRS include)
set(COMPONENT_SRCS "button.c")
register_component()
