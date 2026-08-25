target_sources(${MODULE_NAME} PRIVATE osal.cc mem.cc)
target_link_libraries(${MODULE_NAME} PRIVATE soc)
