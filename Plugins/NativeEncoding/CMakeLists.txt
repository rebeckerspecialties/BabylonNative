set(SOURCES
    "Include/Babylon/Plugins/NativeEncoding.h"
    "Source/NativeEncoding.cpp")

add_library(NativeEncoding ${SOURCES})
warnings_as_errors(NativeEncoding)

target_include_directories(NativeEncoding
    PUBLIC "Include")

target_link_libraries(NativeEncoding
    PUBLIC napi
    PRIVATE GraphicsDevice
    PRIVATE GraphicsDeviceContext
    PRIVATE JsRuntimeInternal)

if(APPLE)
    target_link_libraries(NativeEncoding
        PRIVATE "-framework CoreFoundation"
        PRIVATE "-framework CoreGraphics"
        PRIVATE "-framework ImageIO")
else()
    target_link_libraries(NativeEncoding
        PRIVATE bimg
        PRIVATE bimg_encode)
endif()

set_property(TARGET NativeEncoding PROPERTY FOLDER Plugins)
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${SOURCES})
