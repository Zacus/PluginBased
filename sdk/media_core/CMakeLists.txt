add_library(media_sdk_core STATIC
    src/ClockSync.cpp
    src/FrameScheduler.cpp
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

if(BUILD_TESTING)
    add_executable(MediaSdkCorePrimitivesTest
        tests/tst_media_core_primitives.cpp
    )
    target_compile_features(MediaSdkCorePrimitivesTest PRIVATE cxx_std_20)
    set_target_properties(MediaSdkCorePrimitivesTest PROPERTIES
        CXX_EXTENSIONS OFF
        AUTOMOC OFF
        AUTOUIC OFF
        AUTORCC OFF
    )
    target_include_directories(MediaSdkCorePrimitivesTest PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
    )
    target_link_libraries(MediaSdkCorePrimitivesTest PRIVATE media_sdk_core)
    add_test(NAME media_sdk_core_primitives
        COMMAND $<TARGET_FILE:MediaSdkCorePrimitivesTest>
    )

    add_executable(MediaSdkCoreFrameContractTest
        tests/tst_media_core_frame_contract.cpp
    )
    target_compile_features(MediaSdkCoreFrameContractTest PRIVATE cxx_std_20)
    set_target_properties(MediaSdkCoreFrameContractTest PROPERTIES
        CXX_EXTENSIONS OFF
        AUTOMOC OFF
        AUTOUIC OFF
        AUTORCC OFF
    )
    target_include_directories(MediaSdkCoreFrameContractTest PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
    )
    target_link_libraries(MediaSdkCoreFrameContractTest PRIVATE media_sdk_core)
    add_test(NAME media_sdk_core_frame_contract
        COMMAND $<TARGET_FILE:MediaSdkCoreFrameContractTest>
    )
endif()
