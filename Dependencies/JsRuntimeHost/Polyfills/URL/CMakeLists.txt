set(SOURCES 
    ${SOURCES}
    "Include/Babylon/Polyfills/URL.h"
    "InternalInclude/Babylon/Polyfills/DataUrl.h"
    "Source/DataUrl.cpp"
    "Source/URL.cpp"
    "Source/URL.h"
    "Source/URLSearchParams.cpp"
    "Source/URLSearchParams.h")

add_library(URL ${SOURCES})

target_include_directories(URL PUBLIC "Include")
target_include_directories(URL PRIVATE "InternalInclude")

target_link_libraries(URL
    PRIVATE BlobInternal
    PRIVATE UrlLib
    PUBLIC JsRuntime)

set_property(TARGET URL PROPERTY FOLDER Polyfills)
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${SOURCES})

# Internal decoder shared with Fetch, without extending the public polyfill API.
add_library(URLInternal INTERFACE)
target_include_directories(URLInternal INTERFACE "InternalInclude")
target_link_libraries(URLInternal INTERFACE URL)
