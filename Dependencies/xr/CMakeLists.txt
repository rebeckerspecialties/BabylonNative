cmake_minimum_required(VERSION 3.12)

include(FetchContent)

# --------------------------------------------------
# Declarations
# --------------------------------------------------
FetchContent_Declare(ios-cmake
    GIT_REPOSITORY https://github.com/leetal/ios-cmake.git
    GIT_TAG 4.4.1)
if(ANDROID AND BABYLON_NATIVE_XR_OPENXR)
    FetchContent_Declare(OpenXR-SDK-Source
        GIT_REPOSITORY https://github.com/KhronosGroup/OpenXR-SDK-Source.git
        GIT_TAG release-1.1.61)
endif()
# --------------------------------------------------

if(IOS)
    FetchContent_MakeAvailable_With_Message(ios-cmake)
    set(CMAKE_TOOLCHAIN_FILE "${ios-cmake_SOURCE_DIR}/ios.toolchain.cmake" CACHE PATH "")
    set(PLATFORM "OS64COMBINED" CACHE STRING "")
    set(DEPLOYMENT_TARGET "13" CACHE STRING "")
endif()

project(xr)

set(DYNAMIC_LOADER OFF CACHE BOOL "Build the loader as a .dll library")

set(SOURCES "Include/XR.h"
            "Source/Shared/XRHelpers.h")

if (ANDROID AND BABYLON_NATIVE_XR_OPENXR)
    set(BUILD_API_LAYERS OFF CACHE BOOL "" FORCE)
    set(BUILD_CONFORMANCE_TESTS OFF CACHE BOOL "" FORCE)
    set(BUILD_LOADER ON CACHE BOOL "" FORCE)
    set(BUILD_SDK_TESTS OFF CACHE BOOL "" FORCE)
    set(BUILD_TESTS OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(OpenXR-SDK-Source)
    set(SOURCES ${SOURCES}
        "Source/OpenXR/XR.cpp")
elseif(ANDROID)
    set(SOURCES ${SOURCES}
        "Source/ARCore/Include/IXrContextARCore.h"
        "Source/ARCore/XR.cpp")
elseif(IOS OR BABYLON_NATIVE_BUILD_SOURCETREE)
    set(SOURCES ${SOURCES}
        "Source/ARKit/Include/IXrContextARKit.h"
        "Source/ARKit/XR.mm")
endif()

add_library(xr ${SOURCES})
add_library(xrInternal INTERFACE)

target_compile_definitions(xr PRIVATE _CRT_SECURE_NO_WARNINGS)

if (APPLE)
    enable_objc_arc(xr)
endif()

if (ANDROID AND BABYLON_NATIVE_XR_OPENXR)
    target_compile_definitions(xr
        PUBLIC BABYLON_NATIVE_XR_OPENXR
        PRIVATE XR_USE_GRAPHICS_API_VULKAN XR_USE_PLATFORM_ANDROID)
    target_link_libraries(xr PRIVATE OpenXR::openxr_loader AndroidExtensions log)
elseif(ANDROID)
    add_library(arcore SHARED IMPORTED)
    set_target_properties(arcore PROPERTIES IMPORTED_LOCATION
                          ${ARCORE_LIBPATH}/${ANDROID_ABI}/libarcore_sdk_c.so
                          INTERFACE_INCLUDE_DIRECTORIES ${arcore-android-sdk_SOURCE_DIR}/libraries/include)

    add_library(glm INTERFACE)
    set_target_properties(glm PROPERTIES INTERFACE_INCLUDE_DIRECTORIES ${arcore-android-sdk_SOURCE_DIR}/third_party/glm)

    target_link_libraries(xr PRIVATE glm arcore AndroidExtensions)
elseif (IOS)
    target_link_libraries(xr PRIVATE "-framework ARKit")
endif()

target_link_libraries(xr
    PUBLIC arcana)

target_include_directories(xr PUBLIC "Include")
target_include_directories(xr PRIVATE "Source/Shared")

source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${SOURCES})

target_include_directories(xrInternal INTERFACE "Source")
target_link_libraries(xrInternal
    INTERFACE xr)
