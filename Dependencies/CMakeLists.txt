set(CMAKE_POLICY_DEFAULT_CMP0077 NEW)

# --------------------------------------------------
# AndroidExtensions
# --------------------------------------------------
if(ANDROID OR BABYLON_NATIVE_BUILD_SOURCETREE)
    FetchContent_MakeAvailable_With_Message(AndroidExtensions)
    if(ENABLE_SANITIZERS AND TARGET AndroidExtensions)
        # AndroidExtensions/JNI VM bridge can trip ASAN on API 31 (tagged-pointer JNI internals).
        # Keep strict sanitizers for the engine, but exclude this bridge target.
        target_compile_options(AndroidExtensions PRIVATE -fno-sanitize=address)
    endif()
endif()

# --------------------------------------------------
# arcana.cpp
# --------------------------------------------------
FetchContent_MakeAvailable_With_Message(arcana.cpp)

# --------------------------------------------------
# arcore-android-sdk
# --------------------------------------------------
if(ANDROID OR BABYLON_NATIVE_BUILD_SOURCETREE)
    FetchContent_MakeAvailable_With_Message(arcore-android-sdk)
endif()

# --------------------------------------------------
# base-n
# --------------------------------------------------
FetchContent_MakeAvailable_With_Message(base-n)

add_library(base-n INTERFACE)
target_include_directories(base-n INTERFACE "${base-n_SOURCE_DIR}/include")

# --------------------------------------------------
# glslang
# --------------------------------------------------
set(BUILD_EXTERNAL OFF)
set(ENABLE_SPVREMAPPER OFF)
set(ENABLE_GLSLANG_BINARIES OFF)
set(ENABLE_HLSL OFF)
set(ENABLE_OPT OFF)
set(ENABLE_CTEST OFF)
set(SKIP_GLSLANG_INSTALL ON)
# BABYLON_NATIVE_DISABLE_WEBMIN is used to disable WebMin in both glslang and SPIRV-Cross.
# This flag should be removed when the code coverage in these library corresponds to exactly what we use in Babylon.js
# and webGL2 glsl.
if(BABYLON_NATIVE_DISABLE_WEBMIN)
    set(ENABLE_GLSLANG_WEBMIN OFF)
else()
    set(ENABLE_GLSLANG_WEBMIN ON)
endif()
set(ENABLE_GLSLANG_WEBMIN_DEVEL OFF)

if(NOT TARGET glslang)
    FetchContent_MakeAvailable_With_Message(glslang)

    set_property(TARGET GenericCodeGen PROPERTY FOLDER Dependencies/glslang)
    set_property(TARGET glslang PROPERTY FOLDER Dependencies/glslang)
    set_property(TARGET glslang-default-resource-limits PROPERTY FOLDER Dependencies/glslang)
    set_property(TARGET OGLCompiler PROPERTY FOLDER Dependencies/glslang)
    set_property(TARGET OSDependent PROPERTY FOLDER Dependencies/glslang)
    set_property(TARGET MachineIndependent PROPERTY FOLDER Dependencies/glslang)
    set_property(TARGET SPIRV PROPERTY FOLDER Dependencies/glslang)
endif()

# --------------------------------------------------
# JsRuntimeHost
# --------------------------------------------------
#
# BabylonNative already applies sanitizer compile/link flags globally from the
# root CMakeLists. JsRuntimeHost has its own sanitizer block keyed on the same
# ENABLE_SANITIZERS option and currently appends Android fdsan unconditionally.
# Temporarily disable that local block while fetching JsRuntimeHost to avoid
# duplicate/incompatible flags; targets still inherit the root sanitizer flags.
if(ENABLE_SANITIZERS)
    set(_BABYLON_NATIVE_ENABLE_SANITIZERS_SAVED "${ENABLE_SANITIZERS}")
    set(ENABLE_SANITIZERS OFF CACHE BOOL "" FORCE)
endif()

FetchContent_MakeAvailable_With_Message(JsRuntimeHost)

if(DEFINED _BABYLON_NATIVE_ENABLE_SANITIZERS_SAVED)
    set(ENABLE_SANITIZERS "${_BABYLON_NATIVE_ENABLE_SANITIZERS_SAVED}" CACHE BOOL "" FORCE)
    unset(_BABYLON_NATIVE_ENABLE_SANITIZERS_SAVED)
endif()

# UrlLib enables ARC only through an Xcode target attribute in its own CMake.
# Add explicit ARC flags so Unix Makefiles/Ninja builds on Apple also work.
if(APPLE AND TARGET UrlLib)
    target_compile_options(UrlLib PRIVATE -fobjc-arc)
endif()

set_property(TARGET JsRuntime PROPERTY UNITY_BUILD false)
if(TARGET AppRuntime)
    set_property(TARGET AppRuntime PROPERTY UNITY_BUILD false)
endif()

# --------------------------------------------------
# metal-cpp
# --------------------------------------------------
if(APPLE OR BABYLON_NATIVE_BUILD_SOURCETREE)
    FetchContent_MakeAvailable_With_Message(metal-cpp)
endif()

if(APPLE)
    # Generate a single header.
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    set(METAL_CPP_GENERATED_DIR "${CMAKE_BINARY_DIR}/metal-cpp")
    execute_process(
        COMMAND ${Python3_EXECUTABLE}
            "${metal-cpp_SOURCE_DIR}/SingleHeader/MakeSingleHeader.py"
            "${metal-cpp_SOURCE_DIR}/Foundation/Foundation.hpp"
            "${metal-cpp_SOURCE_DIR}/Metal/Metal.hpp"
            "${metal-cpp_SOURCE_DIR}/QuartzCore/QuartzCore.hpp"
            -o "${METAL_CPP_GENERATED_DIR}/Include/Metal/Metal.hpp"
        RESULT_VARIABLE METAL_CPP_RESULT)
    if(NOT METAL_CPP_RESULT EQUAL 0)
        message(FATAL_ERROR "Failed to generate metal-cpp single header")
    endif()

    # Generate source that includes the single header with the necessary implementation defines.
    file(WRITE "${METAL_CPP_GENERATED_DIR}/Source/Metal.cpp"
        "#define NS_PRIVATE_IMPLEMENTATION\n"
        "#define MTL_PRIVATE_IMPLEMENTATION\n"
        "#define CA_PRIVATE_IMPLEMENTATION\n"
        "#include <Metal/Metal.hpp>\n")

    set(SOURCES
        "${METAL_CPP_GENERATED_DIR}/Include/Metal/Metal.hpp"
        "${METAL_CPP_GENERATED_DIR}/Source/Metal.cpp")

    add_library(metal-cpp ${SOURCES})
    target_include_directories(metal-cpp SYSTEM PUBLIC "${METAL_CPP_GENERATED_DIR}/Include")
    target_link_libraries(metal-cpp
        PRIVATE "-framework Foundation"
        PRIVATE "-framework Metal"
        PRIVATE "-framework QuartzCore")

    source_group(TREE "${metal-cpp_SOURCE_DIR}" FILES ${METAL_CPP_HEADERS})
    source_group(TREE "${METAL_CPP_GENERATED_DIR}" FILES ${SOURCES})
    set_property(TARGET metal-cpp PROPERTY FOLDER Dependencies)
endif()

# --------------------------------------------------
# SPIRV-Cross
# --------------------------------------------------
set(SPIRV_CROSS_CLI OFF)
set(SPIRV_CROSS_ENABLE_TESTS OFF)
set(SPIRV_CROSS_ENABLE_CPP OFF)
set(SPIRV_CROSS_ENABLE_REFLECT OFF)
set(SPIRV_CROSS_ENABLE_C_API OFF)
set(SPIRV_CROSS_ENABLE_UTIL OFF)
set(SPIRV_CROSS_SKIP_INSTALL ON)
# BABYLON_NATIVE_DISABLE_WEBMIN : see comment in section above.
if(BABYLON_NATIVE_DISABLE_WEBMIN)
    set(SPIRV_CROSS_ENABLE_WEBMIN OFF)
else()
    set(SPIRV_CROSS_ENABLE_WEBMIN ON)
endif()
if(NOT GRAPHICS_API STREQUAL "OpenGL")
    set(SPIRV_CROSS_ENABLE_GLSL OFF)
endif()
if(NOT GRAPHICS_API STREQUAL "Metal")
    set(SPIRV_CROSS_ENABLE_MSL OFF)
endif()
if(NOT GRAPHICS_API STREQUAL "D3D11" AND NOT GRAPHICS_API STREQUAL "D3D12")
    set(SPIRV_CROSS_ENABLE_HLSL OFF)
endif()
FetchContent_MakeAvailable_With_Message(SPIRV-Cross)
set_property(TARGET spirv-cross-core PROPERTY FOLDER Dependencies/SPIRV-Cross)
if(TARGET spirv-cross-glsl)
    set_property(TARGET spirv-cross-glsl PROPERTY FOLDER Dependencies/SPIRV-Cross)
endif()
if(TARGET spirv-cross-msl)
    set_property(TARGET spirv-cross-msl PROPERTY FOLDER Dependencies/SPIRV-Cross)
endif()
if(TARGET spirv-cross-hlsl)
    set_property(TARGET spirv-cross-hlsl PROPERTY FOLDER Dependencies/SPIRV-Cross)
endif()

# HACK: Re-enabled disabled warnings in SPIRV-Cross as they cause alerts in BinSkim
foreach(target spirv-cross-core spirv-cross-glsl spirv-cross-msl spirv-cross-hlsl)
    if(TARGET ${target})
        get_target_property(compileOptions ${target} COMPILE_OPTIONS)
        list(REMOVE_ITEM compileOptions /wd4267 /wd4996)
        set_target_properties(${target} PROPERTIES COMPILE_OPTIONS "${compileOptions}")
    endif()
endforeach()

# Disable warnings until unused parameter [-Wunused-parameter] get fixed.
if(TARGET SPIRV)
    disable_warnings(SPIRV)
endif()
if(TARGET MachineIndependent)
    disable_warnings(MachineIndependent)
endif()

# --------------------------------------------------
# WebP
# --------------------------------------------------
if(BABYLON_NATIVE_PLUGIN_NATIVEENGINE_WEBP)
    set(WEBP_BUILD_ANIM_UTILS OFF)
    set(WEBP_BUILD_CWEBP OFF)
    set(WEBP_BUILD_DWEBP OFF)
    set(WEBP_BUILD_GIF2WEBP OFF)
    set(WEBP_BUILD_IMG2WEBP OFF)
    set(WEBP_BUILD_VWEBP OFF)
    set(WEBP_BUILD_WEBPINFO OFF)
    set(WEBP_BUILD_LIBWEBPMUX OFF)
    set(WEBP_BUILD_WEBPMUX OFF)
    set(WEBP_BUILD_EXTRAS OFF)
    FetchContent_MakeAvailable_With_Message(libwebp)

    set_property(TARGET sharpyuv PROPERTY FOLDER Dependencies/libwebp)
    set_property(TARGET webp PROPERTY FOLDER Dependencies/libwebp)
    set_property(TARGET webpdecode PROPERTY FOLDER Dependencies/libwebp)
    set_property(TARGET webpdecoder PROPERTY FOLDER Dependencies/libwebp)
    set_property(TARGET webpdemux PROPERTY FOLDER Dependencies/libwebp)
    set_property(TARGET webpdsp PROPERTY FOLDER Dependencies/libwebp)
    set_property(TARGET webpdspdecode PROPERTY FOLDER Dependencies/libwebp)
    set_property(TARGET webpencode PROPERTY FOLDER Dependencies/libwebp)
    set_property(TARGET webputils PROPERTY FOLDER Dependencies/libwebp)
    set_property(TARGET webputilsdecode PROPERTY FOLDER Dependencies/libwebp)
endif()

# --------------------------------------------------
# WindowsAppSDK
# --------------------------------------------------
if(WINDOWS_STORE)
    add_subdirectory(WindowsAppSDK)
endif()

# --------------------------------------------------
# xr
# --------------------------------------------------
# Currently supported on Android via ARCore, and iOS via ARKit.
if((BABYLON_NATIVE_PLUGIN_NATIVEXR AND (ANDROID OR IOS)) OR BABYLON_NATIVE_BUILD_SOURCETREE)
    add_subdirectory(xr)
    set_property(TARGET xr PROPERTY FOLDER Dependencies/xr)
    warnings_as_errors(xr)
endif()
