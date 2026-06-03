add_library(PluginBasedPlugin INTERFACE
    IAppPlugin.h
)
target_include_directories(PluginBasedPlugin INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(PluginBasedPlugin INTERFACE Qt6::Core)
