add_library(media_sdk_core STATIC
    src/Player.cpp
)

add_library(media_sdk::core ALIAS media_sdk_core)

target_compile_features(media_sdk_core PUBLIC cxx_std_20)
set_target_properties(media_sdk_core PROPERTIES
    CXX_EXTENSIONS OFF
    AUTOMOC OFF
    AUTOUIC OFF
    AUTORCC OFF
)

target_include_directories(media_sdk_core PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
    "$<INSTALL_INTERFACE:include>"
)
