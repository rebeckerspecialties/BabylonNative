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

set(CARGO_COMMAND)
find_program(CARGO_EXECUTABLE cargo)
if(CARGO_EXECUTABLE)
    set(CARGO_COMMAND "${CARGO_EXECUTABLE}")
else()
    find_program(RUSTUP_EXECUTABLE rustup)
    if(NOT RUSTUP_EXECUTABLE)
        message(FATAL_ERROR "WGPU backend requires cargo in PATH or rustup in PATH.")
    endif()

    execute_process(
        COMMAND "${RUSTUP_EXECUTABLE}" show active-toolchain
        OUTPUT_VARIABLE RUSTUP_ACTIVE_TOOLCHAIN_RAW
        OUTPUT_STRIP_TRAILING_WHITESPACE
        RESULT_VARIABLE RUSTUP_ACTIVE_TOOLCHAIN_RESULT)

    if(NOT RUSTUP_ACTIVE_TOOLCHAIN_RESULT EQUAL 0 OR RUSTUP_ACTIVE_TOOLCHAIN_RAW STREQUAL "")
        message(FATAL_ERROR "Failed to resolve active rustup toolchain.")
    endif()

    string(REGEX MATCH "^[^ ]+" RUSTUP_ACTIVE_TOOLCHAIN "${RUSTUP_ACTIVE_TOOLCHAIN_RAW}")
    if(RUSTUP_ACTIVE_TOOLCHAIN STREQUAL "")
        message(FATAL_ERROR "Failed to parse active rustup toolchain from: ${RUSTUP_ACTIVE_TOOLCHAIN_RAW}")
    endif()

    execute_process(
        COMMAND "${RUSTUP_EXECUTABLE}" which --toolchain "${RUSTUP_ACTIVE_TOOLCHAIN}" cargo
        OUTPUT_VARIABLE RUSTUP_CARGO_EXECUTABLE
        OUTPUT_STRIP_TRAILING_WHITESPACE
        RESULT_VARIABLE RUSTUP_CARGO_RESULT)
    execute_process(
        COMMAND "${RUSTUP_EXECUTABLE}" which --toolchain "${RUSTUP_ACTIVE_TOOLCHAIN}" rustc
        OUTPUT_VARIABLE RUSTUP_RUSTC_EXECUTABLE
        OUTPUT_STRIP_TRAILING_WHITESPACE
        RESULT_VARIABLE RUSTUP_RUSTC_RESULT)

    if(NOT RUSTUP_CARGO_RESULT EQUAL 0 OR RUSTUP_CARGO_EXECUTABLE STREQUAL "")
        message(FATAL_ERROR "Failed to resolve cargo path for toolchain ${RUSTUP_ACTIVE_TOOLCHAIN}.")
    endif()
    if(NOT RUSTUP_RUSTC_RESULT EQUAL 0 OR RUSTUP_RUSTC_EXECUTABLE STREQUAL "")
        message(FATAL_ERROR "Failed to resolve rustc path for toolchain ${RUSTUP_ACTIVE_TOOLCHAIN}.")
    endif()

    get_filename_component(RUSTUP_TOOLCHAIN_BIN_DIR "${RUSTUP_RUSTC_EXECUTABLE}" DIRECTORY)
    if(WIN32)
        set(RUSTUP_ENV_PATH "${RUSTUP_TOOLCHAIN_BIN_DIR};$ENV{PATH}")
    else()
        set(RUSTUP_ENV_PATH "${RUSTUP_TOOLCHAIN_BIN_DIR}:$ENV{PATH}")
    endif()

    set(CARGO_COMMAND
        "${CMAKE_COMMAND}"
        "-E"
        "env"
        "PATH=${RUSTUP_ENV_PATH}"
        "RUSTC=${RUSTUP_RUSTC_EXECUTABLE}"
        "${RUSTUP_CARGO_EXECUTABLE}")
endif()

set(RUST_CRATE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/Rust")
set(RUST_TARGET_DIR "${CMAKE_BINARY_DIR}/cargo")
get_filename_component(BABYLON_NATIVE_REPO_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
get_property(BABYLON_NATIVE_MULTI_CONFIG GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
set(RUST_PROFILE_DIR "debug")
set(RUST_BUILD_FLAG "")
set(RUST_TARGET_TRIPLE "")
set(RUST_TARGET_FLAG "")
set(RUST_PLATFORM_ENV)
set(RUST_PLATFORM_RUSTFLAGS "")
set(RUST_COMMON_ENV "CARGO_NET_GIT_FETCH_WITH_CLI=true")
set(RUST_PROFILE_ENV)
set(RUST_PROFILE_RUSTFLAGS "")
set(RUST_RELEASE_RUSTFLAGS "")
set(RUST_RELEASE_BUILD_STD_ARGS "")

if(CMAKE_BUILD_TYPE MATCHES "^[Rr]el")
    # Match Release, RelWithDebInfo, and RelMinSize to the Rust release profile.
    set(RUST_PROFILE_DIR "release")
    set(RUST_BUILD_FLAG "--release")
    list(APPEND RUST_PROFILE_ENV
        "CARGO_PROFILE_RELEASE_CODEGEN_UNITS=1")
    if(BABYLON_NATIVE_LTO_BITCODE_STATIC_LIBS)
        list(APPEND RUST_PROFILE_ENV
            "RUSTC_BOOTSTRAP=1"
            "CARGO_PROFILE_RELEASE_PANIC=abort")
        set(RUST_RELEASE_BUILD_STD_ARGS "-Z" "build-std=std,panic_abort")
    endif()
endif()

function(_babylon_rust_output_lib out_var target_dir target_triple profile lib_basename)
    if(WIN32)
        set(_filename "${lib_basename}.lib")
    else()
        set(_filename "lib${lib_basename}.a")
    endif()

    if(target_triple STREQUAL "")
        set(_path "${target_dir}/${profile}/${_filename}")
    else()
        set(_path "${target_dir}/${target_triple}/${profile}/${_filename}")
    endif()

    set(${out_var} "${_path}" PARENT_SCOPE)
endfunction()

if(ANDROID)
    if(ANDROID_ABI STREQUAL "arm64-v8a")
        set(RUST_TARGET_TRIPLE "aarch64-linux-android")
    elseif(ANDROID_ABI STREQUAL "armeabi-v7a")
        set(RUST_TARGET_TRIPLE "armv7-linux-androideabi")
    elseif(ANDROID_ABI STREQUAL "x86")
        set(RUST_TARGET_TRIPLE "i686-linux-android")
    elseif(ANDROID_ABI STREQUAL "x86_64")
        set(RUST_TARGET_TRIPLE "x86_64-linux-android")
    endif()

    set(ANDROID_PLATFORM_LEVEL "")
    if(DEFINED ANDROID_PLATFORM AND NOT ANDROID_PLATFORM STREQUAL "")
        string(REGEX REPLACE "^android-" "" ANDROID_PLATFORM_LEVEL "${ANDROID_PLATFORM}")
    endif()
    if(ANDROID_PLATFORM_LEVEL STREQUAL "")
        set(ANDROID_PLATFORM_LEVEL "24")
    endif()

    set(ANDROID_NDK_ROOT "${ANDROID_NDK}")
    if(ANDROID_NDK_ROOT STREQUAL "" AND DEFINED CMAKE_ANDROID_NDK)
        set(ANDROID_NDK_ROOT "${CMAKE_ANDROID_NDK}")
    endif()

    if(ANDROID_NDK_ROOT STREQUAL "")
        message(FATAL_ERROR "Unable to resolve Android NDK root for Rust linker setup.")
    endif()

    if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Darwin")
        set(ANDROID_HOST_TAG "darwin-x86_64")
    elseif(CMAKE_HOST_SYSTEM_NAME STREQUAL "Linux")
        set(ANDROID_HOST_TAG "linux-x86_64")
    elseif(CMAKE_HOST_SYSTEM_NAME STREQUAL "Windows")
        set(ANDROID_HOST_TAG "windows-x86_64")
    else()
        message(FATAL_ERROR "Unsupported host system for Android Rust linker setup: ${CMAKE_HOST_SYSTEM_NAME}")
    endif()

    set(RUST_ANDROID_LINKER_TRIPLE "${RUST_TARGET_TRIPLE}")
    if(RUST_TARGET_TRIPLE STREQUAL "armv7-linux-androideabi")
        set(RUST_ANDROID_LINKER_TRIPLE "armv7a-linux-androideabi")
    endif()

    set(ANDROID_LLVM_BIN_DIR "${ANDROID_NDK_ROOT}/toolchains/llvm/prebuilt/${ANDROID_HOST_TAG}/bin")
    set(RUST_ANDROID_LINKER "${ANDROID_LLVM_BIN_DIR}/${RUST_ANDROID_LINKER_TRIPLE}${ANDROID_PLATFORM_LEVEL}-clang")
    if(NOT EXISTS "${RUST_ANDROID_LINKER}")
        message(FATAL_ERROR "Unable to locate Android Rust linker: ${RUST_ANDROID_LINKER}")
    endif()

    string(TOUPPER "${RUST_TARGET_TRIPLE}" RUST_TARGET_TRIPLE_ENV)
    string(REPLACE "-" "_" RUST_TARGET_TRIPLE_ENV "${RUST_TARGET_TRIPLE_ENV}")
    set(ANDROID_SYSROOT "${ANDROID_NDK_ROOT}/toolchains/llvm/prebuilt/${ANDROID_HOST_TAG}/sysroot")
    list(APPEND RUST_PLATFORM_ENV
        "CARGO_TARGET_${RUST_TARGET_TRIPLE_ENV}_LINKER=${RUST_ANDROID_LINKER}"
        "CC_${RUST_TARGET_TRIPLE_ENV}=${RUST_ANDROID_LINKER}"
        "AR_${RUST_TARGET_TRIPLE_ENV}=${ANDROID_LLVM_BIN_DIR}/llvm-ar"
        "BINDGEN_EXTRA_CLANG_ARGS=--sysroot=${ANDROID_SYSROOT} --target=${RUST_ANDROID_LINKER_TRIPLE}${ANDROID_PLATFORM_LEVEL}")
elseif(IOS)
    set(BABYLON_IOS_SIMULATOR_BUILD OFF)
    if(PLATFORM MATCHES "SIMULATOR")
        set(BABYLON_IOS_SIMULATOR_BUILD ON)
    elseif(CMAKE_OSX_SYSROOT MATCHES "iphonesimulator")
        set(BABYLON_IOS_SIMULATOR_BUILD ON)
    endif()

    if(BABYLON_IOS_SIMULATOR_BUILD)
        if(PLATFORM MATCHES "SIMULATORARM64" OR CMAKE_OSX_ARCHITECTURES MATCHES "(^|;)arm64(;|$)")
            set(RUST_TARGET_TRIPLE "aarch64-apple-ios-sim")
        else()
            set(RUST_TARGET_TRIPLE "x86_64-apple-ios")
        endif()
        list(APPEND RUST_PLATFORM_ENV
            "IPHONEOS_DEPLOYMENT_TARGET=${DEPLOYMENT_TARGET}")
        string(APPEND RUST_PLATFORM_RUSTFLAGS " -C link-arg=-mios-simulator-version-min=${DEPLOYMENT_TARGET}")
    else()
        set(RUST_TARGET_TRIPLE "aarch64-apple-ios")
        list(APPEND RUST_PLATFORM_ENV
            "IPHONEOS_DEPLOYMENT_TARGET=${DEPLOYMENT_TARGET}")
        string(APPEND RUST_PLATFORM_RUSTFLAGS " -C link-arg=-miphoneos-version-min=${DEPLOYMENT_TARGET}")
    endif()
elseif(VISIONOS)
    set(BABYLON_VISIONOS_SIMULATOR_BUILD OFF)
    if(PLATFORM MATCHES "SIMULATOR")
        set(BABYLON_VISIONOS_SIMULATOR_BUILD ON)
    elseif(CMAKE_OSX_SYSROOT MATCHES "xrsimulator")
        set(BABYLON_VISIONOS_SIMULATOR_BUILD ON)
    endif()

    if(BABYLON_VISIONOS_SIMULATOR_BUILD)
        set(RUST_TARGET_TRIPLE "aarch64-apple-xros-sim")
    else()
        set(RUST_TARGET_TRIPLE "aarch64-apple-xros")
    endif()
elseif(APPLE)
    if(CMAKE_OSX_ARCHITECTURES MATCHES "(^|;)x86_64(;|$)")
        set(RUST_TARGET_TRIPLE "x86_64-apple-darwin")
    else()
        set(RUST_TARGET_TRIPLE "aarch64-apple-darwin")
    endif()
elseif(WIN32)
    if(CMAKE_SIZEOF_VOID_P EQUAL 8)
        set(RUST_TARGET_TRIPLE "x86_64-pc-windows-msvc")
    else()
        set(RUST_TARGET_TRIPLE "i686-pc-windows-msvc")
    endif()
endif()

if(NOT RUST_TARGET_TRIPLE STREQUAL "")
    set(RUST_TARGET_FLAG "--target" "${RUST_TARGET_TRIPLE}")
endif()

set(RUSTFLAGS_ENV_NAME "RUSTFLAGS")
if(NOT RUST_TARGET_TRIPLE STREQUAL "")
    string(TOUPPER "${RUST_TARGET_TRIPLE}" RUST_TARGET_TRIPLE_ENV_NAME)
    string(REPLACE "-" "_" RUST_TARGET_TRIPLE_ENV_NAME "${RUST_TARGET_TRIPLE_ENV_NAME}")
    set(RUSTFLAGS_ENV_NAME "CARGO_TARGET_${RUST_TARGET_TRIPLE_ENV_NAME}_RUSTFLAGS")
endif()

set(RUST_RELEASE_RUSTFLAGS "${RUST_PLATFORM_RUSTFLAGS}")
if(BABYLON_NATIVE_LTO_BITCODE_STATIC_LIBS)
    string(APPEND RUST_RELEASE_RUSTFLAGS " -C linker-plugin-lto -C embed-bitcode=yes -C codegen-units=1")
endif()
if(CMAKE_BUILD_TYPE MATCHES "^[Rr]el")
    set(RUST_PROFILE_RUSTFLAGS "${RUST_RELEASE_RUSTFLAGS}")
endif()
string(STRIP "${RUST_PLATFORM_RUSTFLAGS}" RUST_PLATFORM_RUSTFLAGS)
string(STRIP "${RUST_RELEASE_RUSTFLAGS}" RUST_RELEASE_RUSTFLAGS)
string(STRIP "${RUST_PROFILE_RUSTFLAGS}" RUST_PROFILE_RUSTFLAGS)

_babylon_rust_output_lib(RUST_OUTPUT_LIB_DEBUG "${RUST_TARGET_DIR}" "${RUST_TARGET_TRIPLE}" "debug" "babylon_graphics_backend")
_babylon_rust_output_lib(RUST_OUTPUT_LIB_RELEASE "${RUST_TARGET_DIR}" "${RUST_TARGET_TRIPLE}" "release" "babylon_graphics_backend")

if(BABYLON_NATIVE_MULTI_CONFIG)
    set(RUST_OUTPUT_LIBS
        "${RUST_OUTPUT_LIB_DEBUG}"
        "${RUST_OUTPUT_LIB_RELEASE}")
else()
    if(RUST_PROFILE_DIR STREQUAL "release")
        set(RUST_OUTPUT_LIB "${RUST_OUTPUT_LIB_RELEASE}")
    else()
        set(RUST_OUTPUT_LIB "${RUST_OUTPUT_LIB_DEBUG}")
    endif()
    set(RUST_OUTPUT_LIBS "${RUST_OUTPUT_LIB}")
endif()

set(UPSTREAM_WGPU_NATIVE_TARGET "")

if(BABYLON_NATIVE_WGPU_USE_UPSTREAM_NATIVE)
    if(NOT DEFINED wgpu_native_SOURCE_DIR OR wgpu_native_SOURCE_DIR STREQUAL "")
        message(FATAL_ERROR "BABYLON_NATIVE_WGPU_USE_UPSTREAM_NATIVE requires wgpu_native_SOURCE_DIR.")
    endif()
    if(NOT DEFINED webgpu_headers_SOURCE_DIR OR webgpu_headers_SOURCE_DIR STREQUAL "")
        message(FATAL_ERROR "BABYLON_NATIVE_WGPU_USE_UPSTREAM_NATIVE requires webgpu_headers_SOURCE_DIR.")
    endif()
    if(BABYLON_NATIVE_WGPU_NATIVE_RLIB)
        if(NOT DEFINED wgpu_native_rlib_SOURCE_DIR OR wgpu_native_rlib_SOURCE_DIR STREQUAL "")
            message(FATAL_ERROR "BABYLON_NATIVE_WGPU_NATIVE_RLIB requires wgpu_native_rlib_SOURCE_DIR.")
        endif()
        set(WGPU_NATIVE_CARGO_PATCH_CONFIG
            "--config"
            "patch.\"https://github.com/gfx-rs/wgpu-native.git\".wgpu-native.path=\"${wgpu_native_rlib_SOURCE_DIR}\"")
        set(WGPU_NATIVE_CARGO_FEATURES "--features" "wgpu-native-rlib")
    endif()
endif()

if(BABYLON_NATIVE_MULTI_CONFIG)
    set(RUST_BACKEND_ENV_DEBUG ${RUST_COMMON_ENV} ${RUST_PLATFORM_ENV})
    set(RUST_BACKEND_ENV_RELEASE ${RUST_COMMON_ENV} ${RUST_PLATFORM_ENV} "CARGO_PROFILE_RELEASE_CODEGEN_UNITS=1")
    if(BABYLON_NATIVE_LTO_BITCODE_STATIC_LIBS)
        list(APPEND RUST_BACKEND_ENV_RELEASE
            "RUSTC_BOOTSTRAP=1"
            "CARGO_PROFILE_RELEASE_PANIC=abort")
        set(RUST_RELEASE_BUILD_STD_ARGS "-Z" "build-std=std,panic_abort")
    endif()
    if(NOT RUST_PLATFORM_RUSTFLAGS STREQUAL "")
        list(APPEND RUST_BACKEND_ENV_DEBUG "${RUSTFLAGS_ENV_NAME}=${RUST_PLATFORM_RUSTFLAGS}")
    endif()
    if(NOT RUST_RELEASE_RUSTFLAGS STREQUAL "")
        list(APPEND RUST_BACKEND_ENV_RELEASE "${RUSTFLAGS_ENV_NAME}=${RUST_RELEASE_RUSTFLAGS}")
    endif()
    if(BABYLON_NATIVE_WGPU_USE_UPSTREAM_NATIVE)
        list(APPEND RUST_BACKEND_ENV_DEBUG
            "BABYLON_WGPU_NATIVE_FFI_DIR=${wgpu_native_SOURCE_DIR}/ffi"
            "BABYLON_WEBGPU_HEADERS_DIR=${webgpu_headers_SOURCE_DIR}")
        list(APPEND RUST_BACKEND_ENV_RELEASE
            "BABYLON_WGPU_NATIVE_FFI_DIR=${wgpu_native_SOURCE_DIR}/ffi"
            "BABYLON_WEBGPU_HEADERS_DIR=${webgpu_headers_SOURCE_DIR}")
    endif()

    add_custom_command(
        OUTPUT
            "${RUST_OUTPUT_LIB_DEBUG}"
            "${RUST_OUTPUT_LIB_RELEASE}"
        COMMAND ${CMAKE_COMMAND} -E env ${RUST_BACKEND_ENV_DEBUG} ${CARGO_COMMAND} build --manifest-path "${RUST_CRATE_DIR}/Cargo.toml" --target-dir "${RUST_TARGET_DIR}" ${RUST_TARGET_FLAG} ${WGPU_NATIVE_CARGO_PATCH_CONFIG} ${WGPU_NATIVE_CARGO_FEATURES}
        COMMAND ${CMAKE_COMMAND} -E env ${RUST_BACKEND_ENV_RELEASE} ${CARGO_COMMAND} build ${RUST_RELEASE_BUILD_STD_ARGS} --manifest-path "${RUST_CRATE_DIR}/Cargo.toml" --target-dir "${RUST_TARGET_DIR}" --release ${RUST_TARGET_FLAG} ${WGPU_NATIVE_CARGO_PATCH_CONFIG} ${WGPU_NATIVE_CARGO_FEATURES}
        WORKING_DIRECTORY "${RUST_CRATE_DIR}"
        DEPENDS
            "${BABYLON_NATIVE_REPO_ROOT}/Cargo.toml"
            "${BABYLON_NATIVE_REPO_ROOT}/Cargo.lock"
            "${RUST_CRATE_DIR}/Cargo.toml"
            "${RUST_CRATE_DIR}/build.rs"
            "${RUST_CRATE_DIR}/src/lib.rs"
            "${RUST_CRATE_DIR}/src/upstream_wgpu_native/compute.rs"
            "${BABYLON_NATIVE_REPO_ROOT}/Polyfills/CanvasWgpu/Rust/src/lib.rs"
        COMMENT "Building Rust WebGPU backend (debug + release)"
        VERBATIM)
else()
    set(RUST_BACKEND_ENV ${RUST_COMMON_ENV} ${RUST_PLATFORM_ENV} ${RUST_PROFILE_ENV})
    if(RUST_PROFILE_DIR STREQUAL "release")
        if(NOT RUST_PROFILE_RUSTFLAGS STREQUAL "")
            list(APPEND RUST_BACKEND_ENV "${RUSTFLAGS_ENV_NAME}=${RUST_PROFILE_RUSTFLAGS}")
        endif()
    elseif(NOT RUST_PLATFORM_RUSTFLAGS STREQUAL "")
        list(APPEND RUST_BACKEND_ENV "${RUSTFLAGS_ENV_NAME}=${RUST_PLATFORM_RUSTFLAGS}")
    endif()
    if(BABYLON_NATIVE_WGPU_USE_UPSTREAM_NATIVE)
        list(APPEND RUST_BACKEND_ENV
            "BABYLON_WGPU_NATIVE_FFI_DIR=${wgpu_native_SOURCE_DIR}/ffi"
            "BABYLON_WEBGPU_HEADERS_DIR=${webgpu_headers_SOURCE_DIR}")
    endif()

    add_custom_command(
        OUTPUT "${RUST_OUTPUT_LIB}"
        COMMAND ${CMAKE_COMMAND} -E env ${RUST_BACKEND_ENV} ${CARGO_COMMAND} build ${RUST_RELEASE_BUILD_STD_ARGS} --manifest-path "${RUST_CRATE_DIR}/Cargo.toml" --target-dir "${RUST_TARGET_DIR}" ${RUST_BUILD_FLAG} ${RUST_TARGET_FLAG} ${WGPU_NATIVE_CARGO_PATCH_CONFIG} ${WGPU_NATIVE_CARGO_FEATURES}
        WORKING_DIRECTORY "${RUST_CRATE_DIR}"
        DEPENDS
            "${BABYLON_NATIVE_REPO_ROOT}/Cargo.toml"
            "${BABYLON_NATIVE_REPO_ROOT}/Cargo.lock"
            "${RUST_CRATE_DIR}/Cargo.toml"
            "${RUST_CRATE_DIR}/build.rs"
            "${RUST_CRATE_DIR}/src/lib.rs"
            "${RUST_CRATE_DIR}/src/upstream_wgpu_native/compute.rs"
            "${BABYLON_NATIVE_REPO_ROOT}/Polyfills/CanvasWgpu/Rust/src/lib.rs"
        COMMENT "Building Rust WebGPU backend"
        VERBATIM)
endif()

add_custom_target(babylon_graphics_backend_rust DEPENDS ${RUST_OUTPUT_LIBS})
if(UPSTREAM_WGPU_NATIVE_TARGET)
    add_dependencies(babylon_graphics_backend_rust ${UPSTREAM_WGPU_NATIVE_TARGET})
endif()

add_library(babylon_graphics_backend STATIC IMPORTED GLOBAL)
if(BABYLON_NATIVE_MULTI_CONFIG)
    set_target_properties(babylon_graphics_backend PROPERTIES
        IMPORTED_LOCATION_DEBUG "${RUST_OUTPUT_LIB_DEBUG}"
        IMPORTED_LOCATION_RELEASE "${RUST_OUTPUT_LIB_RELEASE}"
        IMPORTED_LOCATION_RELWITHDEBINFO "${RUST_OUTPUT_LIB_RELEASE}"
        IMPORTED_LOCATION_MINSIZEREL "${RUST_OUTPUT_LIB_RELEASE}")
else()
    set_target_properties(babylon_graphics_backend PROPERTIES IMPORTED_LOCATION "${RUST_OUTPUT_LIB}")
endif()
add_dependencies(babylon_graphics_backend babylon_graphics_backend_rust)

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
    INTERFACE "InternalInclude")
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
