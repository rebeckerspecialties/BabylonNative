if(BABYLON_NATIVE_PLUGIN_NATIVEINPUT)
    add_subdirectory(NativeInput)
endif()

if(BABYLON_NATIVE_PLUGIN_NATIVEWEBGPU)
    add_subdirectory(NativeWebGPU)
endif()

if(BABYLON_NATIVE_PLUGIN_SHADERCOMPILER)
    add_subdirectory(ShaderCompiler)
endif()

if(BABYLON_NATIVE_PLUGIN_SHADERTOOL)
    if(BABYLON_NATIVE_PLUGIN_SHADERCACHE AND BABYLON_NATIVE_PLUGIN_SHADERCOMPILER)
        add_subdirectory(ShaderTool)
    else()
        message(WARNING "ShaderTool requires both ShaderCache and ShaderCompiler plugins to be enabled.")
    endif()
endif()

if(BABYLON_NATIVE_PLUGIN_TESTUTILS)
    add_subdirectory(TestUtils)
endif()

