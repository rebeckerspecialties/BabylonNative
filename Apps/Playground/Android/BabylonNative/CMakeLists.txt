cmake_minimum_required(VERSION 3.18)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

project(BabylonNative)

get_filename_component(PLAYGROUND_DIR "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
get_filename_component(REPO_ROOT_DIR "${PLAYGROUND_DIR}/../.." ABSOLUTE)

add_subdirectory(${REPO_ROOT_DIR} "${CMAKE_CURRENT_BINARY_DIR}/BabylonNative")

add_library(BabylonNativeJNI SHARED
    src/main/cpp/BabylonNativeJNI.cpp
    ${PLAYGROUND_DIR}/Shared/AppContext.cpp
    ${PLAYGROUND_DIR}/Shared/Diagnostics.cpp)

target_include_directories(BabylonNativeJNI
    PRIVATE ${PLAYGROUND_DIR})

target_link_libraries(BabylonNativeJNI
    PRIVATE android
    PRIVATE EGL
    PRIVATE log
    PRIVATE -lz
    PRIVATE AndroidExtensions
    PRIVATE AppRuntime
    PRIVATE Blob
    PRIVATE Canvas
    PRIVATE Console
    PRIVATE GraphicsDevice
    PRIVATE NativeInput
    PRIVATE NativeWebGPU
    PRIVATE Performance
    PRIVATE ScriptLoader
    PRIVATE TextDecoder
    PRIVATE URL
    PRIVATE Window
    PRIVATE XMLHttpRequest)

target_compile_definitions(BabylonNativeJNI
    PRIVATE BABYLON_NATIVE_PLAYGROUND_HAS_CANVAS=1)

if(ENABLE_SANITIZERS AND DEFINED BABYLON_NATIVE_SANITIZER_FLAGS)
    target_compile_options(BabylonNativeJNI
        PRIVATE -fsanitize=${BABYLON_NATIVE_SANITIZER_FLAGS} -fno-omit-frame-pointer)
    target_link_options(BabylonNativeJNI
        PRIVATE -fsanitize=${BABYLON_NATIVE_SANITIZER_FLAGS})

    if(ANDROID)
        # JNI dispatch can fault under ASAN on tagged-pointer ART internals (API 31 emulator).
        # Keep ASAN on backend libraries and turn it off for this JNI entrypoint shim.
        target_compile_options(BabylonNativeJNI PRIVATE -fno-sanitize=address)
    endif()
endif()
