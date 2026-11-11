set(SOURCES
    "Source/Device.cpp"
    "Source/DeviceContext.cpp"
    "Source/DeviceImpl.cpp"
    "Source/DeviceImpl.h"
    "Source/SafeTimespanGuarantor.cpp"
    "Source/WgpuNative.cpp"
    "Source/WgpuNative.h"
    "InternalInclude/Babylon/Graphics/continuation_scheduler.h"
    "InternalInclude/Babylon/Graphics/DeviceContext.h"
    "InternalInclude/Babylon/Graphics/WgpuInterop.h"
    "InternalInclude/Babylon/Graphics/SafeTimespanGuarantor.h")

# GraphicsWgpu owns native render backend lifetime (surface/device/render loop)
# and exports the shared Rust ABI target `babylon_graphics_backend`.
# NativeWebGPU and CanvasWgpu both link this same target so the binary carries
# one wgpu-backed runtime graph.

set(RUST_CRATE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/Rust")
set(WGPU_NATIVE_CARGO_ARGS)
set(WGPU_NATIVE_CARGO_ENV)

if(BABYLON_NATIVE_WGPU_USE_UPSTREAM_NATIVE)
    if(NOT DEFINED wgpu_native_SOURCE_DIR OR wgpu_native_SOURCE_DIR STREQUAL "")
        message(FATAL_ERROR "BABYLON_NATIVE_WGPU_USE_UPSTREAM_NATIVE requires wgpu_native_SOURCE_DIR.")
    endif()
    if(NOT DEFINED webgpu_headers_SOURCE_DIR OR webgpu_headers_SOURCE_DIR STREQUAL "")
        message(FATAL_ERROR "BABYLON_NATIVE_WGPU_USE_UPSTREAM_NATIVE requires webgpu_headers_SOURCE_DIR.")
    endif()
    list(APPEND WGPU_NATIVE_CARGO_ENV
        "BABYLON_WGPU_NATIVE_FFI_DIR=${wgpu_native_SOURCE_DIR}/ffi"
        "BABYLON_WEBGPU_HEADERS_DIR=${webgpu_headers_SOURCE_DIR}")
    if(BABYLON_NATIVE_WGPU_NATIVE_RLIB)
        # The rlib patch itself is applied workspace-wide by BabylonRustCrate.cmake.
        list(APPEND WGPU_NATIVE_CARGO_ARGS "--features" "wgpu-native-rlib")
    endif()
endif()

babylon_add_rust_static_crate(babylon_graphics_backend
    CRATE_DIR "${RUST_CRATE_DIR}"
    LIB_NAME babylon_graphics_backend
    SOURCES
        "${RUST_CRATE_DIR}/build.rs"
        "${RUST_CRATE_DIR}/src/lib.rs"
        "${RUST_CRATE_DIR}/src/error_bridge.rs"
        "${RUST_CRATE_DIR}/src/upstream_wgpu_native/compute.rs"
        "${BABYLON_RUST_WORKSPACE_DIR}/Polyfills/CanvasWgpu/Rust/src/lib.rs"
        ${WGPU_NATIVE_RUNTIME_INPUTS}
    CARGO_ARGS ${WGPU_NATIVE_CARGO_ARGS}
    CARGO_ENV ${WGPU_NATIVE_CARGO_ENV}
    COMMENT "Building Rust WebGPU backend")

add_library(Graphics ${SOURCES})
warnings_as_errors(Graphics)

# Reuse existing public graphics contracts and platform type headers.
target_include_directories(Graphics
    PRIVATE "${CMAKE_CURRENT_LIST_DIR}/../Graphics/Include/Shared"
    PRIVATE "${CMAKE_CURRENT_LIST_DIR}/../Graphics/Include/Platform/${BABYLON_NATIVE_PLATFORM}"
    PRIVATE "${CMAKE_CURRENT_LIST_DIR}/../Graphics/Include/RendererType/${GRAPHICS_API}"
    PRIVATE "${CMAKE_CURRENT_LIST_DIR}/InternalInclude/Babylon/Graphics")

target_link_libraries(Graphics
    PRIVATE arcana
    PRIVATE JsRuntimeInternal
    PRIVATE babylon_graphics_backend)

if(APPLE)
    target_link_libraries(Graphics
        PRIVATE metal-cpp)
endif()

target_compile_definitions(Graphics
    PRIVATE NOMINMAX)

set_property(TARGET Graphics PROPERTY FOLDER Core)
set_property(TARGET Graphics PROPERTY UNITY_BUILD false)

source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${SOURCES})

if(APPLE)
    # The shared platform headers use Objective-C declarations on Apple targets.
    target_compile_options(Graphics PUBLIC "SHELL:-x objective-c++")
endif()

add_library(GraphicsDevice INTERFACE)
target_include_directories(GraphicsDevice
    INTERFACE "${CMAKE_CURRENT_LIST_DIR}/../Graphics/Include/Shared"
    INTERFACE "${CMAKE_CURRENT_LIST_DIR}/../Graphics/Include/Platform/${BABYLON_NATIVE_PLATFORM}"
    INTERFACE "${CMAKE_CURRENT_LIST_DIR}/../Graphics/Include/RendererType/${GRAPHICS_API}")
target_link_libraries(GraphicsDevice
    INTERFACE Graphics
    INTERFACE JsRuntime)

if(APPLE)
    target_link_libraries(GraphicsDevice
        INTERFACE metal-cpp)
endif()

add_library(GraphicsDeviceContext INTERFACE)
target_include_directories(GraphicsDeviceContext
    INTERFACE "InternalInclude"
    INTERFACE "${CMAKE_CURRENT_LIST_DIR}/../Graphics/InternalInclude")
target_link_libraries(GraphicsDeviceContext
    INTERFACE Graphics
    INTERFACE JsRuntimeInternal
    INTERFACE arcana)

if(APPLE)
    target_link_libraries(GraphicsDeviceContext
        INTERFACE metal-cpp)
endif()

target_compile_definitions(GraphicsDeviceContext
    INTERFACE NOMINMAX)
