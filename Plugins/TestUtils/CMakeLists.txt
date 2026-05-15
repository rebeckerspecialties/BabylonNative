set(SOURCES
    "Include/Babylon/Plugins/TestUtils.h"
    "Source/TestUtils.h"
    "Source/TestUtils.cpp"
    "Source/TestUtils_${BABYLON_NATIVE_PLATFORM}.${BABYLON_NATIVE_PLATFORM_IMPL_EXT}")

add_library(TestUtils ${SOURCES})
warnings_as_errors(TestUtils)

target_include_directories(TestUtils PUBLIC
    "Include")

target_link_libraries(TestUtils
    PUBLIC napi
    PRIVATE JsRuntimeInternal
    PRIVATE GraphicsDevice
    PRIVATE GraphicsDeviceContext)

if(BABYLON_NATIVE_PLUGIN_NATIVEENGINE_LOAD_IMAGES)
    target_compile_definitions(TestUtils
        PRIVATE BABYLON_NATIVE_PLUGIN_NATIVEENGINE_LOAD_IMAGES)
    target_link_libraries(TestUtils
        PRIVATE bimg
        PRIVATE bimg_encode
        PRIVATE bimg_decode)
endif()

if(TARGET babylon_graphics_backend)
    target_compile_definitions(TestUtils PRIVATE BABYLON_NATIVE_TESTUTILS_WGPU)
endif()

if(APPLE AND TARGET babylon_graphics_backend AND NOT BABYLON_NATIVE_PLUGIN_NATIVEENGINE_LOAD_IMAGES)
    target_compile_definitions(TestUtils PRIVATE BABYLON_NATIVE_TESTUTILS_SYSTEM_IMAGES)
    target_link_libraries(TestUtils
        PRIVATE "-framework CoreFoundation"
        PRIVATE "-framework CoreGraphics"
        PRIVATE "-framework ImageIO")
endif()

target_compile_definitions(TestUtils PRIVATE GRAPHICS_API=${GRAPHICS_API})

if(APPLE)
    enable_objc_arc(TestUtils)
endif()

set_property(TARGET TestUtils PROPERTY FOLDER Plugins)
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${SOURCES})
