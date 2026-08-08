qt_add_executable(PluginGeneratorApp
    main.cpp
)

set_target_properties(PluginGeneratorApp PROPERTIES
    WIN32_EXECUTABLE TRUE
)

qt_add_qml_module(PluginGeneratorApp
    URI     PluginGenerator
    VERSION 1.0
    RESOURCE_PREFIX "/"
    OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/PluginGenerator"
    QML_FILES
        qml/Main.qml
    SOURCES
        PluginGeneratorOptions.h
        PluginScaffoldWriter.h
        PluginScaffoldWriter.cpp
        PluginTemplateGenerator.h
        PluginTemplateGenerator.cpp
        PluginTemplateRenderer.h
        PluginTemplateRenderer.cpp
)

target_link_libraries(PluginGeneratorApp PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Quick
    Qt6::QuickControls2
)

target_compile_definitions(PluginGeneratorApp PRIVATE
    PLUGINBASED_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)

add_executable(PluginGeneratorBackendSmokeTest
    PluginGeneratorBackendSmokeTest.cpp
    PluginGeneratorOptions.h
    PluginScaffoldWriter.h
    PluginScaffoldWriter.cpp
    PluginTemplateGenerator.h
    PluginTemplateGenerator.cpp
    PluginTemplateRenderer.h
    PluginTemplateRenderer.cpp
)

target_link_libraries(PluginGeneratorBackendSmokeTest PRIVATE
    Qt6::Core
    Qt6::Quick
)

target_compile_definitions(PluginGeneratorBackendSmokeTest PRIVATE
    PLUGINBASED_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)

if(BUILD_TESTING)
    pluginbased_enable_test_assertions(PluginGeneratorBackendSmokeTest)
endif()
