if(NOT TARGET bx)
    # WebGPU-only builds still need bx's reviewed radix sort, but not bgfx or bimg.
    FetchContent_MakeAvailable_With_Message(bx)
    FetchContent_GetProperties(bx SOURCE_DIR BX_DIR)
    set(BGFX_INSTALL OFF)
    include("${CMAKE_CURRENT_LIST_DIR}/../../Dependencies/bgfx.cmake/cmake/bx/bx.cmake")
    set_target_properties(bx PROPERTIES POSITION_INDEPENDENT_CODE ON FOLDER "Dependencies/bx")
endif()

set(SOURCES
    "Include/Babylon/Plugins/NativeOptimizations.h"
    "Source/NativeOptimizations.cpp")

add_library(NativeOptimizations ${SOURCES})
warnings_as_errors(NativeOptimizations)

if(APPLE)
    enable_objc_arc(NativeOptimizations)
endif()

target_include_directories(NativeOptimizations PUBLIC "Include")

target_link_libraries(NativeOptimizations
    PUBLIC napi
    PRIVATE bx
    PRIVATE JsRuntimeInternal)

set_property(TARGET NativeOptimizations PROPERTY FOLDER Plugins)
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${SOURCES})
