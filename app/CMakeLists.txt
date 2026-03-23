qt_add_executable(VideoPlayerApp
    main.cpp
    AppController.h
    AppController.cpp
    CrashHandler.h
    CrashHandler.cpp
    ../qml/qml.qrc          # 直接用 qrc，不用 qt_add_qml_module
)

target_include_directories(VideoPlayerApp PRIVATE
    ${CMAKE_SOURCE_DIR}/core
    ${CMAKE_SOURCE_DIR}/plugin
    ${CMAKE_SOURCE_DIR}/logger
)

target_link_libraries(VideoPlayerApp PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Quick
    Qt6::QuickControls2
    Qt6::Multimedia
    VideoPlayerLogger
    VideoPlayerCore
    VideoPlayerPlugin
    ${DUMP_LIBS}
)

if(WIN32)
    target_compile_definitions(VideoPlayerApp PRIVATE PLATFORM_WINDOWS)
elseif(UNIX)
    target_compile_definitions(VideoPlayerApp PRIVATE PLATFORM_LINUX)
endif()

set_target_properties(VideoPlayerApp PROPERTIES
    WIN32_EXECUTABLE TRUE
    MACOSX_BUNDLE    TRUE
)
