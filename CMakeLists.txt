cmake_minimum_required(VERSION 3.21)

if(POLICY CMP0135)
    cmake_policy(SET CMP0135 NEW)
endif()

include(FetchContent)

# --------------------------------------------------
# Declarations
# --------------------------------------------------

# EXCLUDE_FROM_ALL is used to prevent FetchContent from automatically adding
# targets to the ALL_BUILD target.  We do this for all dependencies except
# JsRuntimeHost because JsRuntimeHost has top-level targets (e.g., AppRuntime)
# that are part of the Babylon Native contract. When adding dependencies using
# FetchContent, add the EXCLUDE_FROM_ALL option except where there are top-level
# targets that are part of the Babylon Native contract.

FetchContent_Declare(AndroidExtensions
    GIT_REPOSITORY https://github.com/BabylonJS/AndroidExtensions.git
    GIT_TAG 5fe60c8634dd9a5df76888e73e69202875e894f0
    EXCLUDE_FROM_ALL)
FetchContent_Declare(arcana.cpp
    GIT_REPOSITORY https://github.com/microsoft/arcana.cpp.git
    GIT_TAG 0b9f9b6d761909fbca9d3ab1f2d8ff1c3d25ed3d
    EXCLUDE_FROM_ALL)
FetchContent_Declare(arcore-android-sdk
    GIT_REPOSITORY https://github.com/google-ar/arcore-android-sdk.git
    GIT_TAG 7b1e4927914e741e11d1aee9eb549b7ba829197b
    EXCLUDE_FROM_ALL)
FetchContent_Declare(base-n
    GIT_REPOSITORY https://github.com/azawadzki/base-n.git
    GIT_TAG 7573e77c0b9b0e8a5fb63d96dbde212c921993b4
    EXCLUDE_FROM_ALL)
FetchContent_Declare(CMakeExtensions
    GIT_REPOSITORY https://github.com/BabylonJS/CMakeExtensions.git
    GIT_TAG 631780e42886e5f12bfd1a5568c7395f1d657f43
    EXCLUDE_FROM_ALL)
FetchContent_Declare(glslang
    GIT_REPOSITORY https://github.com/BabylonJS/glslang.git
    GIT_TAG 284e4301e5a6b44b279635276588db7cdd942624
    EXCLUDE_FROM_ALL)
FetchContent_Declare(ios-cmake
    GIT_REPOSITORY https://github.com/leetal/ios-cmake.git
    GIT_TAG 4.5.0
    EXCLUDE_FROM_ALL)
FetchContent_Declare(JsRuntimeHost
    GIT_REPOSITORY https://github.com/BabylonJS/JsRuntimeHost.git
    GIT_TAG 7239f910a1e707107036c14f9c4dff902dab2272)
FetchContent_Declare(libwebp
    GIT_REPOSITORY https://github.com/webmproject/libwebp.git
    GIT_TAG baff2c8980970f58e2aa88622c6575bdee2f876a
    EXCLUDE_FROM_ALL)
FetchContent_Declare(webgpu_headers
    URL https://github.com/webgpu-native/webgpu-headers/archive/673658bc2bd70ec39fc55ebe6bb0173cf6d0a603.tar.gz
    URL_HASH SHA256=6754c6c2465c79a954f7c04c7890c9f00b7ecce8addbe01b5056e860ba3933e6
    EXCLUDE_FROM_ALL)
FetchContent_Declare(wgpu_native
    GIT_REPOSITORY https://github.com/gfx-rs/wgpu-native.git
    GIT_TAG v29.0.0.0
    GIT_SHALLOW TRUE
    EXCLUDE_FROM_ALL)
FetchContent_Declare(metal-cpp
    GIT_REPOSITORY https://github.com/bkaradzic/metal-cpp.git
    GIT_TAG 9a8bc57c0318b84863bec2faf6605594877c5f5d
    EXCLUDE_FROM_ALL)
FetchContent_Declare(SPIRV-Cross
    GIT_REPOSITORY https://github.com/BabylonJS/SPIRV-Cross.git
    GIT_TAG e1d29b3617de7fb27fc085eb565a790b31679e54
    EXCLUDE_FROM_ALL)

# --------------------------------------------------

FetchContent_MakeAvailable(CMakeExtensions)

# BABYLON_NATIVE_BUILD_SOURCETREE flag is used by BabylonReactNative NPM build process.
# CMake script is run twice. At package creation, BABYLON_NATIVE_BUILD_SOURCETREE is ON
# which makes all dependencies to be fetched and packaged.
# At build time, cmake is run again on BabylonNative root but FETCHCONTENT_FULLY_DISCONNECTED is ON
# and FetchContent_MakeAvailable_With_Message will use provided local folder provided to cmake command line
# like '-DFETCHCONTENT_SOURCE_DIR_JSRUNTIMEHOST=../shared/Babylon/....'

if(VISIONOS OR IOS OR BABYLON_NATIVE_BUILD_SOURCETREE)
    FetchContent_MakeAvailable_With_Message(ios-cmake)
    if(NOT BABYLON_NATIVE_BUILD_SOURCETREE)
        set(CMAKE_TOOLCHAIN_FILE "${ios-cmake_SOURCE_DIR}/ios.toolchain.cmake" CACHE PATH "")
    endif()
    set(ENABLE_ARC OFF CACHE STRING "Enables or disables ARC support.")
endif()

if(VISIONOS)
    set(PLATFORM "VISIONOSCOMBINED" CACHE STRING "")
    set(DEPLOYMENT_TARGET "1.0" CACHE STRING "")
elseif(IOS)
    set(PLATFORM "OS64COMBINED" CACHE STRING "")
    set(DEPLOYMENT_TARGET "13" CACHE STRING "")
endif()

# Keep deployment targeting consistent across all generated Xcode targets.
# Without this, transitive static libraries may default to the host SDK version
# and become unloadable on older simulator runtimes.
if(IOS)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "${DEPLOYMENT_TARGET}" CACHE STRING "" FORCE)
    set(CMAKE_XCODE_ATTRIBUTE_IPHONEOS_DEPLOYMENT_TARGET "${DEPLOYMENT_TARGET}")
elseif(VISIONOS)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "${DEPLOYMENT_TARGET}" CACHE STRING "" FORCE)
endif()

project(BabylonNative)

enable_testing()

set_property(GLOBAL PROPERTY USE_FOLDERS ON)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Suppress "Performing Test ..." output from check_c_compiler_flag() and similar macros.
set(CMAKE_REQUIRED_QUIET TRUE)

# --------------------------------------------------
# Options
# --------------------------------------------------

# General
option(BABYLON_NATIVE_BUILD_APPS "Build Babylon Native apps." ${PROJECT_IS_TOP_LEVEL})
option(BABYLON_NATIVE_INSTALL "Include the install target." ${PROJECT_IS_TOP_LEVEL})
option(BABYLON_DEBUG_TRACE "Enable debug trace." OFF)

# WARNING: This is experimental. Only use it if you can ensure that your application will properly handle thread affinity.
option(BABYLON_NATIVE_CHECK_THREAD_AFFINITY "Checks thread safety in the graphics device calls. It can be removed if hosting application ensures thread coherence." ON)

# Plugins
# wgpu-path plugins (hardwired ON below)
option(BABYLON_NATIVE_PLUGIN_NATIVEINPUT "Include Babylon Native Plugin NativeInput." ON)
option(BABYLON_NATIVE_BUILD_WEBGPU_TESTS "Build NativeWebGPU async bridge gtests on desktop hosts." OFF)
option(BABYLON_NATIVE_ENABLE_WEBGPU_DEVELOPER_FEATURES "Expose non-standard WebGPU developer hooks (Chromium/WebKit developer-features analog)." OFF)
option(BABYLON_NATIVE_ENABLE_UNSAFE_WEBGPU "Expose non-standard unsafe WebGPU hooks (Chromium enable-unsafe-webgpu analog)." OFF)
option(BABYLON_NATIVE_LTO_BITCODE_STATIC_LIBS "Compile native static libraries as LLVM bitcode and use Rust linker-plugin LTO for final whole-program optimization." OFF)

# bgfx-era plugins (declared for structural completeness; forced OFF on wgpu branch)
option(BABYLON_NATIVE_PLUGIN_EXTERNALTEXTURE "Include Babylon Native Plugin ExternalTexture." ON)
option(BABYLON_NATIVE_PLUGIN_NATIVECAMERA "Include Babylon Native Plugin NativeCamera." ON)
option(BABYLON_NATIVE_PLUGIN_NATIVECAPTURE "Include Babylon Native Plugin NativeCapture." ON)
option(BABYLON_NATIVE_PLUGIN_NATIVEENCODING "Include Babylon Native Plugin NativeEncoding." ON)
option(BABYLON_NATIVE_PLUGIN_NATIVEENGINE "Include Babylon Native Plugin NativeEngine." ON)
option(BABYLON_NATIVE_PLUGIN_NATIVEENGINE_WEBP "Include Babylon Native Plugin NativeEngine - WebP." ON)
option(BABYLON_NATIVE_PLUGIN_NATIVEENGINE_LOAD_IMAGES "Enable image loading in NativeEngine (LoadTexture, image bitmap, bimg, WebP)." ON)
if(NOT BABYLON_NATIVE_PLUGIN_NATIVEENGINE_LOAD_IMAGES)
    # WebP support relies on image loading code paths; force it off when image loading is disabled.
    set(BABYLON_NATIVE_PLUGIN_NATIVEENGINE_WEBP OFF CACHE BOOL "Include Babylon Native Plugin NativeEngine - WebP." FORCE)
endif()
option(BABYLON_NATIVE_PLUGIN_NATIVEENGINE_COMPILESHADERS "Include Babylon Native Plugin NativeEngine - Compile Shaders." ON)
option(BABYLON_NATIVE_PLUGIN_NATIVEOPTIMIZATIONS "Include Babylon Native Plugin NativeOptimizations." ON)
option(BABYLON_NATIVE_PLUGIN_NATIVETRACING "Include Babylon Native Plugin NativeTracing." ON)
option(BABYLON_NATIVE_PLUGIN_NATIVEXR "Include Babylon Native Plugin XR." ON)
# ShaderCache: On the bgfx path this caches compiled vertex+fragment binaries
# (BgfxShaderInfo) keyed by GLSL source hash.  The wgpu path uses WGSL→naga and
# has no bgfx consumer.  Equivalent functionality will come from wgpu's
# PipelineCache API once it stabilises in wgpu-native.
# TODO(wgpu-pipeline-cache): Implement wgpu-native pipeline cache behind the same
# Enable/Disable/Save/Load public API surface once wgpu::PipelineCache is stable.
option(BABYLON_NATIVE_PLUGIN_SHADERCACHE "Include Babylon Native Plugin ShaderCache." ON)
option(BABYLON_NATIVE_PLUGIN_SHADERCOMPILER "Include Babylon Native Plugin ShaderCompiler." ON)
option(BABYLON_NATIVE_PLUGIN_SHADERTOOL "Include Babylon Native Plugin ShaderTool." ON)
option(BABYLON_NATIVE_PLUGIN_TESTUTILS "Include Babylon Native Plugin TestUtils." ON)

# Polyfills
option(BABYLON_NATIVE_POLYFILL_WINDOW "Include Babylon Native Polyfill Window." ON)

# Sanitizers
option(ENABLE_SANITIZERS "Enable AddressSanitizer and UBSan" OFF)

if(ENABLE_SANITIZERS)
    include(CheckCCompilerFlag)
    include(CheckCXXCompilerFlag)
    set(ENABLE_RTTI ON CACHE BOOL "" FORCE)
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
        # Android app processes require explicit sanitizer runtime packaging.
        # Prefer ASan there for actionable heap safety checks with manageable
        # signal-to-noise in emulator/device runs.
        if(ANDROID)
            set(SANITIZERS "address")
        else()
            set(SANITIZERS "address,undefined")
        endif()
        # Check for Clang since vptr and fdsan are Clang-specific
        if (CMAKE_CXX_COMPILER_ID MATCHES "Clang")
            # vptr depends on UBSan runtime, and Windows does not support it.
            if(NOT ANDROID AND NOT WIN32)
                list(APPEND SANITIZERS "vptr")
            endif()
            # FDSan only works on Android builds with Clang
            if (ANDROID)
                check_c_compiler_flag("-fsanitize=fdsan" BABYLON_NATIVE_HAS_C_FDSAN)
                check_cxx_compiler_flag("-fsanitize=fdsan" BABYLON_NATIVE_HAS_CXX_FDSAN)
                if(BABYLON_NATIVE_HAS_C_FDSAN AND BABYLON_NATIVE_HAS_CXX_FDSAN)
                    list(APPEND SANITIZERS "fdsan")
                else()
                    message(STATUS "Skipping fdsan sanitizer: compiler does not support -fsanitize=fdsan")
                endif()
            endif()
        endif()

        string(JOIN "," SANITIZER_FLAGS ${SANITIZERS})
        # Export for wrapper launch targets (e.g. Android JNI host) that are
        # created outside this directory scope.
        set(BABYLON_NATIVE_SANITIZER_FLAGS "${SANITIZER_FLAGS}" CACHE INTERNAL "Babylon Native sanitizer flag list" FORCE)

        # -fno-sanitize-recover=all makes UBSan findings fatal. Without it, UBSan
        # only prints a diagnostic to stderr and execution continues, so CI stays
        # green while latent UB goes unreported.
        add_compile_options(-fsanitize=${SANITIZER_FLAGS} -fno-omit-frame-pointer -fno-sanitize-recover=all)
        add_link_options(-fsanitize=${SANITIZER_FLAGS})
    elseif(MSVC)
        # MSVC only supports AddressSanitizer
        add_compile_options(/fsanitize=address /Zi /Od)
    else()
        message(FATAL_ERROR "Sanitizers not supported on this compiler.")
    endif()
endif()

if(BABYLON_NATIVE_LTO_BITCODE_STATIC_LIBS)
    if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        message(FATAL_ERROR "BABYLON_NATIVE_LTO_BITCODE_STATIC_LIBS requires a Clang-compatible compiler.")
    endif()

    add_compile_options(-flto=full)
    add_link_options(-flto=full)
endif()

# Hardwired for wgpu branch: these are not user-configurable until bgfx is fully removed.
# TODO(bgfx-removal): Remove FORCE overrides and restore as user-facing option() toggles.
set(BABYLON_NATIVE_INSTALL OFF CACHE BOOL "Include the install target." FORCE)
set(BABYLON_NATIVE_PLUGIN_NATIVEINPUT ON CACHE BOOL "Include Babylon Native Plugin NativeInput." FORCE)
set(BABYLON_NATIVE_PLUGIN_NATIVEWEBGPU ON CACHE BOOL "Include Babylon Native Plugin NativeWebGPU." FORCE)
set(BABYLON_NATIVE_WGPU_USE_UPSTREAM_NATIVE ON CACHE BOOL "Use upstream wgpu-native as the Rust backend base." FORCE)
set(BABYLON_NATIVE_WGPU_NATIVE_RLIB ON CACHE BOOL "Build wgpu-native in the shared Cargo graph instead of linking a second Rust staticlib." FORCE)
set(BABYLON_NATIVE_POLYFILL_WINDOW ON CACHE BOOL "Include Babylon Native Polyfill Window." FORCE)
set(BABYLON_NATIVE_POLYFILL_CANVAS ON CACHE BOOL "Include Babylon Native Polyfill Canvas." FORCE)
set(BABYLON_NATIVE_POLYFILL_CANVAS_WGPU ON CACHE BOOL "Use the wgpu/femtovg Canvas backend when Canvas polyfill is enabled." FORCE)

# bgfx-dependent plugins: disabled on wgpu branch (no bgfx consumer exists).
# ShaderCache/ShaderCompiler/ShaderTool use BgfxShaderInfo and bgfx shader binaries.
# NativeEngine is the bgfx rendering backend replaced by NativeWebGPU.
# TODO(bgfx-removal): Remove these FORCE overrides when bgfx is fully removed.
set(BABYLON_NATIVE_PLUGIN_EXTERNALTEXTURE OFF CACHE BOOL "" FORCE)
set(BABYLON_NATIVE_PLUGIN_NATIVECAMERA OFF CACHE BOOL "" FORCE)
set(BABYLON_NATIVE_PLUGIN_NATIVECAPTURE OFF CACHE BOOL "" FORCE)
set(BABYLON_NATIVE_PLUGIN_NATIVEENCODING OFF CACHE BOOL "" FORCE)
set(BABYLON_NATIVE_PLUGIN_NATIVEENGINE OFF CACHE BOOL "" FORCE)
set(BABYLON_NATIVE_PLUGIN_NATIVEENGINE_LOAD_IMAGES OFF CACHE BOOL "" FORCE)
set(BABYLON_NATIVE_PLUGIN_NATIVEENGINE_WEBP OFF CACHE BOOL "" FORCE)
set(BABYLON_NATIVE_PLUGIN_NATIVEENGINE_COMPILESHADERS OFF CACHE BOOL "" FORCE)
set(BABYLON_NATIVE_PLUGIN_NATIVEOPTIMIZATIONS OFF CACHE BOOL "" FORCE)
set(BABYLON_NATIVE_PLUGIN_NATIVETRACING OFF CACHE BOOL "" FORCE)
set(BABYLON_NATIVE_PLUGIN_NATIVEXR OFF CACHE BOOL "" FORCE)
set(BABYLON_NATIVE_PLUGIN_SHADERCACHE OFF CACHE BOOL "" FORCE)
set(BABYLON_NATIVE_PLUGIN_SHADERCOMPILER OFF CACHE BOOL "" FORCE)
set(BABYLON_NATIVE_PLUGIN_SHADERTOOL OFF CACHE BOOL "" FORCE)
set(BABYLON_NATIVE_PLUGIN_TESTUTILS OFF CACHE BOOL "" FORCE)

if(BABYLON_NATIVE_WGPU_USE_UPSTREAM_NATIVE)
    FetchContent_MakeAvailable_With_Message(wgpu_native)
    set(webgpu_headers_SOURCE_DIR "${wgpu_native_SOURCE_DIR}/ffi/webgpu-headers" CACHE PATH "webgpu-headers bundled with wgpu-native" FORCE)

    if(BABYLON_NATIVE_WGPU_NATIVE_RLIB)
        set(wgpu_native_rlib_SOURCE_DIR "${CMAKE_BINARY_DIR}/_deps/wgpu_native-rlib-src" CACHE PATH "build-local wgpu-native source patched to expose rlib" FORCE)
        file(REMOVE_RECURSE "${wgpu_native_rlib_SOURCE_DIR}")
        file(COPY "${wgpu_native_SOURCE_DIR}/" DESTINATION "${wgpu_native_rlib_SOURCE_DIR}")
        file(READ "${wgpu_native_rlib_SOURCE_DIR}/Cargo.toml" _wgpu_native_cargo_toml)
        string(REPLACE
            "crate-type = [\"cdylib\", \"staticlib\"]"
            "crate-type = [\"rlib\"]"
            _wgpu_native_cargo_toml_patched
            "${_wgpu_native_cargo_toml}")
        if(_wgpu_native_cargo_toml_patched STREQUAL _wgpu_native_cargo_toml)
            message(FATAL_ERROR "Failed to patch wgpu-native Cargo.toml to expose rlib.")
        endif()
        file(WRITE "${wgpu_native_rlib_SOURCE_DIR}/Cargo.toml" "${_wgpu_native_cargo_toml_patched}")
        unset(_wgpu_native_cargo_toml)
        unset(_wgpu_native_cargo_toml_patched)
    endif()
endif()

# --------------------------------------------------

if(ANDROID)
    set(BABYLON_NATIVE_PLATFORM "Android")
elseif(IOS)
    set(BABYLON_NATIVE_PLATFORM "iOS")
elseif (VISIONOS)
    set(BABYLON_NATIVE_PLATFORM "visionOS")
elseif(APPLE)
    set(BABYLON_NATIVE_PLATFORM "macOS")
elseif(WINDOWS_STORE)
    set(BABYLON_NATIVE_PLATFORM "WinRT")
elseif(WIN32)
    set(BABYLON_NATIVE_PLATFORM "Win32")
elseif(UNIX)
    set(BABYLON_NATIVE_PLATFORM "Unix")
else()
    message(FATAL_ERROR "Unrecognized platform: ${CMAKE_SYSTEM_NAME}")
endif()

if(APPLE)
    set(GRAPHICS_API Metal)
elseif(UNIX)
    if(NOT GRAPHICS_API)
        set(GRAPHICS_API Vulkan)
    else()
        if(NOT GRAPHICS_API STREQUAL Vulkan)
            message(FATAL_ERROR "Unrecognized/Unsupported render API: ${GRAPHICS_API}")
        endif()
    endif()
elseif(WIN32)
    if(NOT GRAPHICS_API)
        set(GRAPHICS_API D3D12)
    else()
        if(NOT GRAPHICS_API STREQUAL Vulkan AND NOT GRAPHICS_API STREQUAL D3D12)
            message(FATAL_ERROR "Unrecognized/Unsupported render API: ${GRAPHICS_API}")
        endif()
    endif()
endif()

# Find ANGLE libraries from Edge or Chrome when using OpenGLWindowsDevOnly.
if(BABYLON_NATIVE_OPENGLES_FROM_BROWSER)
    foreach(_browser_exe "msedge.exe" "chrome.exe")
        if(ANGLE_LIBEGL)
            break()
        endif()
        foreach(_reg_root "HKLM" "HKCU")
            if(ANGLE_LIBEGL)
                break()
            endif()
            execute_process(
                COMMAND reg query "${_reg_root}\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\App Paths\\${_browser_exe}" /ve
                OUTPUT_VARIABLE _reg_output
                ERROR_QUIET
                RESULT_VARIABLE _reg_result
                OUTPUT_STRIP_TRAILING_WHITESPACE
            )
            if(NOT _reg_result EQUAL 0)
                continue()
            endif()
            string(REGEX MATCH "REG_SZ[ \t]+([^\r\n]+)" _ "${_reg_output}")
            string(STRIP "${CMAKE_MATCH_1}" _browser_path)
            if(NOT _browser_path)
                continue()
            endif()
            cmake_path(GET _browser_path PARENT_PATH _browser_dir)
            file(GLOB _version_dirs LIST_DIRECTORIES true "${_browser_dir}/[0-9]*")
            list(SORT _version_dirs ORDER DESCENDING)
            foreach(_version_dir ${_version_dirs})
                if(IS_DIRECTORY "${_version_dir}" AND EXISTS "${_version_dir}/libEGL.dll")
                    set(ANGLE_LIBEGL "${_version_dir}/libEGL.dll")
                    set(ANGLE_LIBGLESV2 "${_version_dir}/libGLESv2.dll")
                    message(STATUS "Found ANGLE libraries in: ${_version_dir}")
                    break()
                endif()
            endforeach()
        endforeach()
    endforeach()

    if(NOT ANGLE_LIBEGL)
        message(FATAL_ERROR "OpenGLWindowsDevOnly requires ANGLE libraries (libEGL.dll, libGLESv2.dll) from Edge or Chrome, but none were found.")
    endif()
endif()

if(APPLE)
    set(BABYLON_NATIVE_PLATFORM_IMPL_EXT "mm")
else()
    set(BABYLON_NATIVE_PLATFORM_IMPL_EXT "cpp")
endif()

if(MSVC)
    # __cplusplus value is not in sync with language version used. MVSC needs this flag to update it accordingly
    # https://gitlab.kitware.com/cmake/cmake/-/issues/18837
    add_compile_options(/Zc:__cplusplus)

    # https://devblogs.microsoft.com/cppblog/announcing-full-support-for-a-c-c-conformant-preprocessor-in-msvc/
    add_compile_options(/Zc:preprocessor)

    # default to /W3
    add_compile_options(/W3)

    # Enable multiprocessor compilation for faster builds
    add_compile_options(/MP)
endif()

if(APPLE)
    set(ENABLE_ARC ON CACHE STRING "Enables or disables ARC support." FORCE)
    if(NOT CMAKE_GENERATOR MATCHES "Xcode")
        # Some fetched dependencies gate ARC behind Xcode-only attributes.
        # Apply ARC explicitly for non-Xcode generators (Makefiles/Ninja).
        add_compile_options(-fobjc-arc)
    endif()

    # Without this option on azure pipelines, there is a mismatch with math.h giving a lot of undefined functions on macOS.
    # Only enabled for Apple as there is no issue for Windows.
    set(CMAKE_NO_SYSTEM_FROM_IMPORTED TRUE)
    # Create scheme for installation and other targets
    set(CMAKE_XCODE_GENERATE_SCHEME TRUE)
endif()

if(BABYLON_DEBUG_TRACE)
    add_compile_definitions(BABYLON_DEBUG_TRACE)
endif()

add_subdirectory(Dependencies)
add_subdirectory(Core)
add_subdirectory(Plugins)
add_subdirectory(Polyfills)

if(BABYLON_NATIVE_BUILD_APPS)
    add_subdirectory(Apps)
endif()

# no install for glslang compiled for shaderc
if(BABYLON_NATIVE_INSTALL)
    include(Install/Install.cmake)
endif()
