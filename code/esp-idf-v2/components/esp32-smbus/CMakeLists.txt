# For IDF v4.x:
idf_component_register(SRCS "smbus.c" INCLUDE_DIRS include REQUIRES driver)

#set(COMPONENT_ADD_INCLUDEDIRS include)
#set(COMPONENT_SRCS "smbus.c")
#register_component()
