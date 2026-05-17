set(SOURCES
    "Include/Babylon/Polyfills/Canvas.h"
    "Source/Canvas.cpp"
    "Source/Canvas.h"
    "Source/Colors.h"
    "Source/Font.cpp"
    "Source/Font.h"
    "Source/Gradient.cpp"
    "Source/Gradient.h"
    "Source/Image.cpp"
    "Source/Image.h"
    "Source/ImageData.cpp"
    "Source/ImageData.h"
    "Source/LineCaps.h"
    "Source/MeasureText.cpp"
    "Source/MeasureText.h"
    "Source/Path2D.cpp"
    "Source/Path2D.h"
    "Source/Context.cpp"
    "Source/Context.h"
    "Source/nanosvg.h"
    # CanvasWgpu keeps the historical nvg* C API names for incremental
    # migration. These headers declare Babylon's compatibility ABI implemented
    # by Rust/femtovg (not upstream NanoVG and not bgfx).
    "Source/nanovg/nanovg.h"
    "Source/nanovg/nanovg_filterstack.h")

if(APPLE)
    set(CMAKE_Swift_LANGUAGE_VERSION 6)
    enable_language(Swift)
    find_library(COREGRAPHICS_LIBRARY CoreGraphics REQUIRED)
    find_library(CORETEXT_LIBRARY CoreText REQUIRED)
    find_library(IMAGEIO_LIBRARY ImageIO REQUIRED)

    add_library(CanvasAppleImageDecoder STATIC "Source/ImageDecoderApple.swift")
    set_property(TARGET CanvasAppleImageDecoder PROPERTY Swift_LANGUAGE_VERSION 6)
    target_compile_options(CanvasAppleImageDecoder
        PRIVATE
            "SHELL:-strict-concurrency=complete"
            "SHELL:-warn-concurrency"
            "SHELL:-warnings-as-errors")
    if(DEFINED BABYLON_NATIVE_SANITIZER_FLAGS AND NOT BABYLON_NATIVE_SANITIZER_FLAGS STREQUAL "")
        string(REPLACE "," ";" _canvas_swift_sanitizers "${BABYLON_NATIVE_SANITIZER_FLAGS}")
        foreach(_canvas_swift_sanitizer IN LISTS _canvas_swift_sanitizers)
            if(_canvas_swift_sanitizer STREQUAL "address" OR
                _canvas_swift_sanitizer STREQUAL "thread" OR
                _canvas_swift_sanitizer STREQUAL "undefined")
                target_compile_options(CanvasAppleImageDecoder
                    PRIVATE "SHELL:-sanitize=${_canvas_swift_sanitizer}")
            endif()
        endforeach()
        unset(_canvas_swift_sanitizer)
        unset(_canvas_swift_sanitizers)
    endif()
    target_link_libraries(CanvasAppleImageDecoder
        PRIVATE ${COREGRAPHICS_LIBRARY}
        PRIVATE ${IMAGEIO_LIBRARY})
    set_property(TARGET CanvasAppleImageDecoder PROPERTY FOLDER Polyfills)
endif()

if(ANDROID)
    list(APPEND SOURCES "Source/ImageDecoderAndroid.cpp")
endif()

if(NOT TARGET babylon_graphics_backend)
    message(FATAL_ERROR "CanvasWgpu requires the GraphicsWgpu Rust backend target (babylon_graphics_backend).")
endif()

add_library(babylon_canvas_wgpu_backend INTERFACE)
target_link_libraries(babylon_canvas_wgpu_backend INTERFACE babylon_graphics_backend)

add_library(Canvas ${SOURCES})
set_property(TARGET Canvas PROPERTY LINKER_LANGUAGE CXX)
warnings_as_errors(Canvas)
target_compile_options(Canvas PRIVATE
    $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wno-unused-parameter -Wno-unused-variable>)
target_compile_definitions(Canvas PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:_CRT_SECURE_NO_WARNINGS>)

target_include_directories(Canvas
    PUBLIC "Include"
    PRIVATE "Source")

target_link_libraries(Canvas
    PUBLIC napi
    PRIVATE base-n
    PRIVATE JsRuntimeInternal
    PRIVATE GraphicsDeviceContext
    PRIVATE UrlLib
    PRIVATE babylon_canvas_wgpu_backend)

if(APPLE)
    target_link_libraries(Canvas
        PRIVATE CanvasAppleImageDecoder
        PRIVATE ${CORETEXT_LIBRARY})
endif()

if(ANDROID)
    target_link_libraries(Canvas
        PRIVATE AndroidExtensions
        PRIVATE dl
        PRIVATE jnigraphics)
endif()

set_property(TARGET Canvas PROPERTY FOLDER Polyfills)
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${SOURCES})
