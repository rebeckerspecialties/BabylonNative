npm(install)

if(BABYLON_NATIVE_PLUGIN_NATIVEWEBGPU AND NOT ANDROID)
    if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/node_modules/babylonjs/babylon.native-build.json")
        message(FATAL_ERROR "Missing optimized Babylon bundle receipt. Run node Apps/scripts/buildBabylonBundle.mjs --install before configuring.")
    endif()
    set(BABYLON_NATIVE_BABYLON_BUNDLE_VARIANT "optimized" CACHE STRING "Verified Babylon bundle: optimized, or explicit A/B control")
    set_property(CACHE BABYLON_NATIVE_BABYLON_BUNDLE_VARIANT PROPERTY STRINGS optimized control)
    find_program(BABYLON_NATIVE_BUNDLE_NODE_EXECUTABLE NAMES node REQUIRED)
    add_custom_target(BabylonBundleVerified
        COMMAND "${BABYLON_NATIVE_BUNDLE_NODE_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/scripts/verifyBabylonBundle.mjs"
            "${CMAKE_CURRENT_SOURCE_DIR}/node_modules/babylonjs"
            "${BABYLON_NATIVE_BABYLON_BUNDLE_VARIANT}"
        COMMENT "Verifying Babylon bundle optimizations and patch receipt"
        VERBATIM)
endif()

if((WIN32 AND NOT WINDOWS_STORE) AND GRAPHICS_API STREQUAL D3D11)
    add_subdirectory(HeadlessScreenshotApp)
    add_subdirectory(StyleTransferApp)
endif()

if(NOT ANDROID)
    add_subdirectory(Playground)
endif()

if((WIN32 AND NOT WINDOWS_STORE) OR (APPLE AND NOT IOS AND NOT VISIONOS) OR (UNIX AND NOT ANDROID AND NOT APPLE))
    add_subdirectory(UnitTests)
    if(TARGET NativeEngine)
        add_subdirectory(ModuleLoadTest)
    endif()
endif()

if((WIN32 AND NOT WINDOWS_STORE AND GRAPHICS_API STREQUAL D3D11) OR (APPLE AND NOT IOS AND NOT VISIONOS))
    if(BABYLON_NATIVE_PLUGIN_SHADERCACHE AND BABYLON_NATIVE_PLUGIN_SHADERTOOL AND NOT BABYLON_NATIVE_PLUGIN_NATIVEENGINE_COMPILESHADERS)
        add_subdirectory(PrecompiledShaderTest)
    endif()
endif()

if(TARGET BabylonBundleVerified)
    foreach(APP_TARGET Playground UnitTests)
        if(TARGET ${APP_TARGET})
            add_dependencies(${APP_TARGET} BabylonBundleVerified)
        endif()
    endforeach()
endif()
