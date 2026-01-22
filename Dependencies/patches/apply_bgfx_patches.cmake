cmake_minimum_required(VERSION 3.21)

if(NOT DEFINED BABYLON_BGFX_SOURCE_DIR)
    message(FATAL_ERROR "BABYLON_BGFX_SOURCE_DIR must be defined before including apply_bgfx_patches.cmake.")
endif()

set(_bgfx_renderer_gl "${BABYLON_BGFX_SOURCE_DIR}/bgfx/src/renderer_gl.cpp")
if(NOT EXISTS "${_bgfx_renderer_gl}")
    message(FATAL_ERROR "Expected bgfx renderer file '${_bgfx_renderer_gl}' not found.")
endif()

find_program(_patch_executable patch)
find_program(_git_executable git)

if(NOT _patch_executable AND NOT _git_executable)
    message(FATAL_ERROR "Neither 'patch' nor 'git' was found; cannot apply bgfx patches.")
endif()

function(_babylon_apply_bgfx_patch patch_file)
    if(NOT EXISTS "${patch_file}")
        message(FATAL_ERROR "bgfx patch '${patch_file}' not found.")
    endif()

    if(_patch_executable)
        execute_process(
            COMMAND "${_patch_executable}" --batch --forward -p1 -i "${patch_file}"
            WORKING_DIRECTORY "${BABYLON_BGFX_SOURCE_DIR}"
            RESULT_VARIABLE _bgfx_patch_result
        )
    else()
        execute_process(
            COMMAND "${_git_executable}" apply --whitespace=nowarn "${patch_file}"
            WORKING_DIRECTORY "${BABYLON_BGFX_SOURCE_DIR}"
            RESULT_VARIABLE _bgfx_patch_result
        )
    endif()

    if(NOT _bgfx_patch_result EQUAL 0)
        message(FATAL_ERROR "Failed to apply bgfx patch '${patch_file}' (error code ${_bgfx_patch_result}).")
    endif()
endfunction()

set(_bgfx_patch_probe "${CMAKE_CURRENT_LIST_DIR}/bgfx-android-emulator-silent-gl-probing.patch")
set(_bgfx_patch_internalformativ "${CMAKE_CURRENT_LIST_DIR}/bgfx-android-emulator-skip-internalformativ.patch")
set(_bgfx_patch_debuglabels "${CMAKE_CURRENT_LIST_DIR}/bgfx-android-emulator-disable-debug-labels.patch")
set(_bgfx_patch_disable_naming "${CMAKE_CURRENT_LIST_DIR}/bgfx-android-emulator-disable-resource-naming.patch")

file(READ "${_bgfx_renderer_gl}" _bgfx_renderer_gl_contents)
if(NOT _bgfx_renderer_gl_contents MATCHES "isTextureFormatValidPerSpecAndroid")
    message(STATUS "Applying bgfx Android emulator probe patch...")
    _babylon_apply_bgfx_patch("${_bgfx_patch_probe}")
    file(READ "${_bgfx_renderer_gl}" _bgfx_renderer_gl_contents)
endif()

if(NOT _bgfx_renderer_gl_contents MATCHES "shouldQueryInternalFormats")
    message(STATUS "Applying bgfx Android emulator internal format patch...")
    _babylon_apply_bgfx_patch("${_bgfx_patch_internalformativ}")
    file(READ "${_bgfx_renderer_gl}" _bgfx_renderer_gl_contents)
endif()

if(NOT _bgfx_renderer_gl_contents MATCHES "Disable debug labels on Android emulators")
    message(STATUS "Applying bgfx Android emulator debug label patch...")
    _babylon_apply_bgfx_patch("${_bgfx_patch_debuglabels}")
    file(READ "${_bgfx_renderer_gl}" _bgfx_renderer_gl_contents)
endif()

if(NOT _bgfx_renderer_gl_contents MATCHES "Disable resource naming on Android emulators")
    message(STATUS "Applying bgfx Android emulator resource naming patch...")
    _babylon_apply_bgfx_patch("${_bgfx_patch_disable_naming}")
    file(READ "${_bgfx_renderer_gl}" _bgfx_renderer_gl_contents)
endif()

message(STATUS "bgfx patches applied successfully.")
