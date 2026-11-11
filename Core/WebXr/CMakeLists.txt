# babylon_webxr_core is the specification-level WebXR model shared by both bindings:
# NativeXr links it through the C ABI in Include/Babylon/WebXr/Core.h, and the WASI host
# consumes the same crate behind the generated webidl:webxr WIT.
file(GLOB_RECURSE WEBXR_CORE_RUST_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/Rust/src/*.rs")

babylon_add_rust_static_crate(babylon_webxr_core
    CRATE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/Rust"
    LIB_NAME babylon_webxr_core
    SOURCES ${WEBXR_CORE_RUST_SOURCES}
    INCLUDE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/Include"
    COMMENT "Building Rust WebXR core")
