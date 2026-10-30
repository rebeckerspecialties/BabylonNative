# Cargo orchestration shared by the Rust crates this tree builds into native libraries.
#
# Including this file resolves the Rust toolchain and the cargo configuration of the
# platform being built (target triple, Android linker, deployment targets, profile,
# sanitizer and LTO flags) once, into BABYLON_RUST_* variables of the including
# directory. Include it from a directory that encloses every crate's CMakeLists.txt,
# then describe each crate with
#
#   babylon_add_rust_static_crate(<target>
#       CRATE_DIR <dir>               directory holding the crate's Cargo.toml
#       LIB_NAME <name>               the cargo [lib] name: lib<name>.a or <name>.lib
#       [SOURCES <file>...]           files whose change reruns cargo
#       [INCLUDE_DIRS <dir>...]       include directories exported to consumers
#       [CARGO_ARGS <arg>...]         crate-specific arguments appended to every `cargo build`
#       [CARGO_ENV <NAME=value>...]   extra environment for every `cargo build`
#       [COMMENT <text>])
#
# <target> is an imported static library produced by cargo in the shared target
# directory ${CMAKE_BINARY_DIR}/cargo; <target>_rust is the custom target that runs
# cargo. Multi-config generators build the debug and release profiles and select the
# library per configuration. All crates belong to the Cargo workspace at the repository
# root, so one Cargo.lock pins every build.

set(BABYLON_RUST_CARGO_COMMAND)
find_program(CARGO_EXECUTABLE cargo)
if(CARGO_EXECUTABLE)
    set(BABYLON_RUST_CARGO_COMMAND "${CARGO_EXECUTABLE}")
else()
    find_program(RUSTUP_EXECUTABLE rustup)
    if(NOT RUSTUP_EXECUTABLE)
        message(FATAL_ERROR "Building the Rust crates requires cargo in PATH or rustup in PATH.")
    endif()

    execute_process(
        COMMAND "${RUSTUP_EXECUTABLE}" show active-toolchain
        OUTPUT_VARIABLE _babylon_rust_active_toolchain_raw
        OUTPUT_STRIP_TRAILING_WHITESPACE
        RESULT_VARIABLE _babylon_rust_active_toolchain_result)

    if(NOT _babylon_rust_active_toolchain_result EQUAL 0 OR _babylon_rust_active_toolchain_raw STREQUAL "")
        message(FATAL_ERROR "Failed to resolve active rustup toolchain.")
    endif()

    string(REGEX MATCH "^[^ ]+" _babylon_rust_active_toolchain "${_babylon_rust_active_toolchain_raw}")
    if(_babylon_rust_active_toolchain STREQUAL "")
        message(FATAL_ERROR "Failed to parse active rustup toolchain from: ${_babylon_rust_active_toolchain_raw}")
    endif()

    execute_process(
        COMMAND "${RUSTUP_EXECUTABLE}" which --toolchain "${_babylon_rust_active_toolchain}" cargo
        OUTPUT_VARIABLE _babylon_rust_cargo_executable
        OUTPUT_STRIP_TRAILING_WHITESPACE
        RESULT_VARIABLE _babylon_rust_cargo_result)
    execute_process(
        COMMAND "${RUSTUP_EXECUTABLE}" which --toolchain "${_babylon_rust_active_toolchain}" rustc
        OUTPUT_VARIABLE _babylon_rust_rustc_executable
        OUTPUT_STRIP_TRAILING_WHITESPACE
        RESULT_VARIABLE _babylon_rust_rustc_result)

    if(NOT _babylon_rust_cargo_result EQUAL 0 OR _babylon_rust_cargo_executable STREQUAL "")
        message(FATAL_ERROR "Failed to resolve cargo path for toolchain ${_babylon_rust_active_toolchain}.")
    endif()
    if(NOT _babylon_rust_rustc_result EQUAL 0 OR _babylon_rust_rustc_executable STREQUAL "")
        message(FATAL_ERROR "Failed to resolve rustc path for toolchain ${_babylon_rust_active_toolchain}.")
    endif()

    get_filename_component(_babylon_rust_toolchain_bin_dir "${_babylon_rust_rustc_executable}" DIRECTORY)
    if(WIN32)
        set(_babylon_rust_env_path "${_babylon_rust_toolchain_bin_dir};$ENV{PATH}")
    else()
        set(_babylon_rust_env_path "${_babylon_rust_toolchain_bin_dir}:$ENV{PATH}")
    endif()

    set(BABYLON_RUST_CARGO_COMMAND
        "${CMAKE_COMMAND}"
        "-E"
        "env"
        "PATH=${_babylon_rust_env_path}"
        "RUSTC=${_babylon_rust_rustc_executable}"
        "${_babylon_rust_cargo_executable}")
endif()

set(BABYLON_RUST_TARGET_DIR "${CMAKE_BINARY_DIR}/cargo")
get_filename_component(BABYLON_RUST_WORKSPACE_DIR "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
get_property(BABYLON_RUST_MULTI_CONFIG GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
set(BABYLON_RUST_PROFILE_DIR "debug")
set(BABYLON_RUST_BUILD_FLAG "")
set(BABYLON_RUST_TARGET_TRIPLE "")
set(BABYLON_RUST_TARGET_FLAG "")
set(BABYLON_RUST_PLATFORM_ENV)
set(BABYLON_RUST_PLATFORM_RUSTFLAGS "")
set(BABYLON_RUST_COMMON_ENV "CARGO_NET_GIT_FETCH_WITH_CLI=true")
set(BABYLON_RUST_PROFILE_ENV)
set(BABYLON_RUST_PROFILE_RUSTFLAGS "")
set(BABYLON_RUST_BUILD_STD_ARGS "")
set(BABYLON_RUST_RELEASE_RUSTFLAGS "")
set(BABYLON_RUST_RELEASE_BUILD_STD_ARGS "")

if(CMAKE_BUILD_TYPE MATCHES "^[Rr]el")
    # Match Release, RelWithDebInfo, and RelMinSize to the Rust release profile.
    set(BABYLON_RUST_PROFILE_DIR "release")
    set(BABYLON_RUST_BUILD_FLAG "--release")
    list(APPEND BABYLON_RUST_PROFILE_ENV
        "CARGO_PROFILE_RELEASE_CODEGEN_UNITS=1")
    if(BABYLON_NATIVE_LTO_BITCODE_STATIC_LIBS)
        list(APPEND BABYLON_RUST_PROFILE_ENV
            "RUSTC_BOOTSTRAP=1"
            "CARGO_PROFILE_RELEASE_PANIC=abort")
        if(BABYLON_RUST_BUILD_STD_ARGS STREQUAL "")
            set(BABYLON_RUST_RELEASE_BUILD_STD_ARGS "-Z" "build-std=std,panic_abort")
        endif()
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
        set(BABYLON_RUST_TARGET_TRIPLE "aarch64-linux-android")
    elseif(ANDROID_ABI STREQUAL "armeabi-v7a")
        set(BABYLON_RUST_TARGET_TRIPLE "armv7-linux-androideabi")
    elseif(ANDROID_ABI STREQUAL "x86")
        set(BABYLON_RUST_TARGET_TRIPLE "i686-linux-android")
    elseif(ANDROID_ABI STREQUAL "x86_64")
        set(BABYLON_RUST_TARGET_TRIPLE "x86_64-linux-android")
    endif()

    set(_babylon_rust_android_platform_level "")
    if(DEFINED ANDROID_PLATFORM AND NOT ANDROID_PLATFORM STREQUAL "")
        string(REGEX REPLACE "^android-" "" _babylon_rust_android_platform_level "${ANDROID_PLATFORM}")
    endif()
    if(_babylon_rust_android_platform_level STREQUAL "")
        set(_babylon_rust_android_platform_level "24")
    endif()

    set(_babylon_rust_android_ndk_root "${ANDROID_NDK}")
    if(_babylon_rust_android_ndk_root STREQUAL "" AND DEFINED CMAKE_ANDROID_NDK)
        set(_babylon_rust_android_ndk_root "${CMAKE_ANDROID_NDK}")
    endif()

    if(_babylon_rust_android_ndk_root STREQUAL "")
        message(FATAL_ERROR "Unable to resolve Android NDK root for Rust linker setup.")
    endif()

    if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Darwin")
        set(_babylon_rust_android_host_tag "darwin-x86_64")
    elseif(CMAKE_HOST_SYSTEM_NAME STREQUAL "Linux")
        set(_babylon_rust_android_host_tag "linux-x86_64")
    elseif(CMAKE_HOST_SYSTEM_NAME STREQUAL "Windows")
        set(_babylon_rust_android_host_tag "windows-x86_64")
    else()
        message(FATAL_ERROR "Unsupported host system for Android Rust linker setup: ${CMAKE_HOST_SYSTEM_NAME}")
    endif()

    set(_babylon_rust_android_linker_triple "${BABYLON_RUST_TARGET_TRIPLE}")
    if(BABYLON_RUST_TARGET_TRIPLE STREQUAL "armv7-linux-androideabi")
        set(_babylon_rust_android_linker_triple "armv7a-linux-androideabi")
    endif()

    set(_babylon_rust_android_llvm_bin_dir "${_babylon_rust_android_ndk_root}/toolchains/llvm/prebuilt/${_babylon_rust_android_host_tag}/bin")
    set(_babylon_rust_android_linker "${_babylon_rust_android_llvm_bin_dir}/${_babylon_rust_android_linker_triple}${_babylon_rust_android_platform_level}-clang")
    if(NOT EXISTS "${_babylon_rust_android_linker}")
        message(FATAL_ERROR "Unable to locate Android Rust linker: ${_babylon_rust_android_linker}")
    endif()

    string(TOUPPER "${BABYLON_RUST_TARGET_TRIPLE}" _babylon_rust_target_triple_env)
    string(REPLACE "-" "_" _babylon_rust_target_triple_env "${_babylon_rust_target_triple_env}")
    set(_babylon_rust_android_sysroot "${_babylon_rust_android_ndk_root}/toolchains/llvm/prebuilt/${_babylon_rust_android_host_tag}/sysroot")
    list(APPEND BABYLON_RUST_PLATFORM_ENV
        "CARGO_TARGET_${_babylon_rust_target_triple_env}_LINKER=${_babylon_rust_android_linker}"
        "CC_${_babylon_rust_target_triple_env}=${_babylon_rust_android_linker}"
        "AR_${_babylon_rust_target_triple_env}=${_babylon_rust_android_llvm_bin_dir}/llvm-ar"
        "BINDGEN_EXTRA_CLANG_ARGS=--sysroot=${_babylon_rust_android_sysroot} --target=${_babylon_rust_android_linker_triple}${_babylon_rust_android_platform_level}")
elseif(TVOS)
    set(_babylon_rust_simulator_build OFF)
    if(PLATFORM MATCHES "SIMULATOR")
        set(_babylon_rust_simulator_build ON)
    elseif(CMAKE_OSX_SYSROOT MATCHES "appletvsimulator")
        set(_babylon_rust_simulator_build ON)
    endif()

    if(_babylon_rust_simulator_build)
        if(CMAKE_OSX_ARCHITECTURES MATCHES "(^|;)x86_64(;|$)")
            set(BABYLON_RUST_TARGET_TRIPLE "x86_64-apple-tvos")
        else()
            set(BABYLON_RUST_TARGET_TRIPLE "aarch64-apple-tvos-sim")
        endif()
        list(APPEND BABYLON_RUST_PLATFORM_ENV
            "TVOS_DEPLOYMENT_TARGET=${DEPLOYMENT_TARGET}")
        string(APPEND BABYLON_RUST_PLATFORM_RUSTFLAGS " -C link-arg=-mtvos-simulator-version-min=${DEPLOYMENT_TARGET}")
    else()
        set(BABYLON_RUST_TARGET_TRIPLE "aarch64-apple-tvos")
        list(APPEND BABYLON_RUST_PLATFORM_ENV
            "TVOS_DEPLOYMENT_TARGET=${DEPLOYMENT_TARGET}")
        string(APPEND BABYLON_RUST_PLATFORM_RUSTFLAGS " -C link-arg=-mtvos-version-min=${DEPLOYMENT_TARGET}")
    endif()
    set(BABYLON_RUST_BUILD_STD_ARGS "-Z" "build-std=std,panic_abort")
    set(BABYLON_RUST_RELEASE_BUILD_STD_ARGS "")
    list(APPEND BABYLON_RUST_PLATFORM_ENV
        "RUSTC_BOOTSTRAP=1"
        "CARGO_PROFILE_DEV_PANIC=abort"
        "CARGO_PROFILE_RELEASE_PANIC=abort")
elseif(IOS)
    set(_babylon_rust_simulator_build OFF)
    if(PLATFORM MATCHES "SIMULATOR")
        set(_babylon_rust_simulator_build ON)
    elseif(CMAKE_OSX_SYSROOT MATCHES "iphonesimulator")
        set(_babylon_rust_simulator_build ON)
    endif()

    if(_babylon_rust_simulator_build)
        if(PLATFORM MATCHES "SIMULATORARM64" OR CMAKE_OSX_ARCHITECTURES MATCHES "(^|;)arm64(;|$)")
            set(BABYLON_RUST_TARGET_TRIPLE "aarch64-apple-ios-sim")
        else()
            set(BABYLON_RUST_TARGET_TRIPLE "x86_64-apple-ios")
        endif()
        list(APPEND BABYLON_RUST_PLATFORM_ENV
            "IPHONEOS_DEPLOYMENT_TARGET=${DEPLOYMENT_TARGET}")
        string(APPEND BABYLON_RUST_PLATFORM_RUSTFLAGS " -C link-arg=-mios-simulator-version-min=${DEPLOYMENT_TARGET}")
    else()
        set(BABYLON_RUST_TARGET_TRIPLE "aarch64-apple-ios")
        list(APPEND BABYLON_RUST_PLATFORM_ENV
            "IPHONEOS_DEPLOYMENT_TARGET=${DEPLOYMENT_TARGET}")
        string(APPEND BABYLON_RUST_PLATFORM_RUSTFLAGS " -C link-arg=-miphoneos-version-min=${DEPLOYMENT_TARGET}")
    endif()
elseif(VISIONOS)
    set(_babylon_rust_simulator_build OFF)
    if(PLATFORM MATCHES "SIMULATOR")
        set(_babylon_rust_simulator_build ON)
    elseif(CMAKE_OSX_SYSROOT MATCHES "xrsimulator")
        set(_babylon_rust_simulator_build ON)
    endif()

    if(_babylon_rust_simulator_build)
        set(BABYLON_RUST_TARGET_TRIPLE "aarch64-apple-xros-sim")
    else()
        set(BABYLON_RUST_TARGET_TRIPLE "aarch64-apple-xros")
    endif()
elseif(APPLE)
    if(CMAKE_OSX_ARCHITECTURES MATCHES "(^|;)x86_64(;|$)")
        set(BABYLON_RUST_TARGET_TRIPLE "x86_64-apple-darwin")
    else()
        set(BABYLON_RUST_TARGET_TRIPLE "aarch64-apple-darwin")
    endif()
elseif(WIN32)
    if(CMAKE_SIZEOF_VOID_P EQUAL 8)
        set(BABYLON_RUST_TARGET_TRIPLE "x86_64-pc-windows-msvc")
    else()
        set(BABYLON_RUST_TARGET_TRIPLE "i686-pc-windows-msvc")
    endif()
endif()

if(NOT BABYLON_RUST_TARGET_TRIPLE STREQUAL "")
    set(BABYLON_RUST_TARGET_FLAG "--target" "${BABYLON_RUST_TARGET_TRIPLE}")
endif()

set(BABYLON_RUST_RUSTFLAGS_ENV_NAME "RUSTFLAGS")
if(NOT BABYLON_RUST_TARGET_TRIPLE STREQUAL "")
    string(TOUPPER "${BABYLON_RUST_TARGET_TRIPLE}" _babylon_rust_target_triple_env_name)
    string(REPLACE "-" "_" _babylon_rust_target_triple_env_name "${_babylon_rust_target_triple_env_name}")
    set(BABYLON_RUST_RUSTFLAGS_ENV_NAME "CARGO_TARGET_${_babylon_rust_target_triple_env_name}_RUSTFLAGS")
endif()

option(BABYLON_NATIVE_RUST_SANITIZERS "Instrument Rust dependencies and std with the host sanitizer (requires rust-src)." OFF)
if(BABYLON_NATIVE_RUST_SANITIZERS)
    if(CMAKE_CXX_COMPILER_ID STREQUAL "AppleClang")
        message(FATAL_ERROR "Rust uses upstream compiler-rt, not AppleClang's sanitizer ABI. Configure a fresh build with CC/CXX from matching upstream LLVM.")
    endif()
    if(NOT APPLE OR IOS OR TVOS OR VISIONOS OR BABYLON_NATIVE_LTO_BITCODE_STATIC_LIBS)
        message(FATAL_ERROR "Rust sanitizer integration currently requires a macOS host without bitcode LTO.")
    endif()
    if(ENABLE_SANITIZERS)
        set(_babylon_rust_sanitizer "address")
    elseif(ENABLE_THREAD_SANITIZER)
        set(_babylon_rust_sanitizer "thread")
    else()
        message(FATAL_ERROR "Rust sanitizers require ENABLE_SANITIZERS or ENABLE_THREAD_SANITIZER.")
    endif()
    # Use the final C++ link's runtime, including for instrumented Rust std.
    # The pinned stable compiler needs bootstrap for these unstable test flags.
    list(APPEND BABYLON_RUST_PLATFORM_ENV "RUSTC_BOOTSTRAP=1")
    set(BABYLON_RUST_BUILD_STD_ARGS "-Z" "build-std=std,panic_unwind")
    string(APPEND BABYLON_RUST_PLATFORM_RUSTFLAGS " -Zsanitizer=${_babylon_rust_sanitizer} -Zexternal-clangrt -Cforce-frame-pointers=yes")
endif()

set(BABYLON_RUST_RELEASE_RUSTFLAGS "${BABYLON_RUST_PLATFORM_RUSTFLAGS}")
if(BABYLON_NATIVE_LTO_BITCODE_STATIC_LIBS)
    string(APPEND BABYLON_RUST_RELEASE_RUSTFLAGS " -C linker-plugin-lto -C embed-bitcode=yes -C codegen-units=1")
endif()
if(CMAKE_BUILD_TYPE MATCHES "^[Rr]el")
    set(BABYLON_RUST_PROFILE_RUSTFLAGS "${BABYLON_RUST_RELEASE_RUSTFLAGS}")
endif()
string(STRIP "${BABYLON_RUST_PLATFORM_RUSTFLAGS}" BABYLON_RUST_PLATFORM_RUSTFLAGS)
string(STRIP "${BABYLON_RUST_RELEASE_RUSTFLAGS}" BABYLON_RUST_RELEASE_RUSTFLAGS)
string(STRIP "${BABYLON_RUST_PROFILE_RUSTFLAGS}" BABYLON_RUST_PROFILE_RUSTFLAGS)

# The workspace lock records wgpu-native through the rlib patch described in the
# top-level CMakeLists.txt (BABYLON_NATIVE_WGPU_NATIVE_RLIB). Cargo resolves the whole
# workspace for every member, so every invocation applies the same patch or --locked
# rejects the lock.
set(BABYLON_RUST_WORKSPACE_CARGO_ARGS)
if(BABYLON_NATIVE_WGPU_USE_UPSTREAM_NATIVE AND BABYLON_NATIVE_WGPU_NATIVE_RLIB)
    if(NOT DEFINED wgpu_native_rlib_SOURCE_DIR OR wgpu_native_rlib_SOURCE_DIR STREQUAL "")
        message(FATAL_ERROR "BABYLON_NATIVE_WGPU_NATIVE_RLIB requires wgpu_native_rlib_SOURCE_DIR.")
    endif()
    list(APPEND BABYLON_RUST_WORKSPACE_CARGO_ARGS
        "--config"
        "patch.\"https://github.com/rebeckerspecialties/wgpu-native.git\".wgpu-native.path=\"${wgpu_native_rlib_SOURCE_DIR}\"")
endif()

# Environment of every cargo invocation: per profile for multi-config generators, per
# the configured build type otherwise.
if(BABYLON_RUST_MULTI_CONFIG)
    set(BABYLON_RUST_ENV_DEBUG ${BABYLON_RUST_COMMON_ENV} ${BABYLON_RUST_PLATFORM_ENV})
    set(BABYLON_RUST_ENV_RELEASE ${BABYLON_RUST_COMMON_ENV} ${BABYLON_RUST_PLATFORM_ENV} "CARGO_PROFILE_RELEASE_CODEGEN_UNITS=1")
    if(BABYLON_NATIVE_LTO_BITCODE_STATIC_LIBS)
        list(APPEND BABYLON_RUST_ENV_RELEASE
            "RUSTC_BOOTSTRAP=1"
            "CARGO_PROFILE_RELEASE_PANIC=abort")
        if(BABYLON_RUST_BUILD_STD_ARGS STREQUAL "")
            set(BABYLON_RUST_RELEASE_BUILD_STD_ARGS "-Z" "build-std=std,panic_abort")
        endif()
    endif()
    if(NOT BABYLON_RUST_PLATFORM_RUSTFLAGS STREQUAL "")
        list(APPEND BABYLON_RUST_ENV_DEBUG "${BABYLON_RUST_RUSTFLAGS_ENV_NAME}=${BABYLON_RUST_PLATFORM_RUSTFLAGS}")
    endif()
    if(NOT BABYLON_RUST_RELEASE_RUSTFLAGS STREQUAL "")
        list(APPEND BABYLON_RUST_ENV_RELEASE "${BABYLON_RUST_RUSTFLAGS_ENV_NAME}=${BABYLON_RUST_RELEASE_RUSTFLAGS}")
    endif()
else()
    set(BABYLON_RUST_ENV ${BABYLON_RUST_COMMON_ENV} ${BABYLON_RUST_PLATFORM_ENV} ${BABYLON_RUST_PROFILE_ENV})
    if(BABYLON_RUST_PROFILE_DIR STREQUAL "release")
        if(NOT BABYLON_RUST_PROFILE_RUSTFLAGS STREQUAL "")
            list(APPEND BABYLON_RUST_ENV "${BABYLON_RUST_RUSTFLAGS_ENV_NAME}=${BABYLON_RUST_PROFILE_RUSTFLAGS}")
        endif()
    elseif(NOT BABYLON_RUST_PLATFORM_RUSTFLAGS STREQUAL "")
        list(APPEND BABYLON_RUST_ENV "${BABYLON_RUST_RUSTFLAGS_ENV_NAME}=${BABYLON_RUST_PLATFORM_RUSTFLAGS}")
    endif()
endif()

function(babylon_add_rust_static_crate target)
    set(_one_value_args CRATE_DIR LIB_NAME COMMENT)
    set(_multi_value_args SOURCES INCLUDE_DIRS CARGO_ARGS CARGO_ENV)
    cmake_parse_arguments(PARSE_ARGV 1 _crate "" "${_one_value_args}" "${_multi_value_args}")

    if(_crate_UNPARSED_ARGUMENTS)
        message(FATAL_ERROR "babylon_add_rust_static_crate(${target}): unexpected arguments: ${_crate_UNPARSED_ARGUMENTS}")
    endif()
    if(NOT _crate_CRATE_DIR OR NOT _crate_LIB_NAME)
        message(FATAL_ERROR "babylon_add_rust_static_crate(${target}) requires CRATE_DIR and LIB_NAME.")
    endif()
    if(NOT DEFINED BABYLON_RUST_CARGO_COMMAND)
        message(FATAL_ERROR "babylon_add_rust_static_crate(${target}): include BabylonRustCrate.cmake from an enclosing directory first.")
    endif()
    if(NOT _crate_COMMENT)
        set(_crate_COMMENT "Building Rust crate ${_crate_LIB_NAME}")
    endif()

    set(_manifest "${_crate_CRATE_DIR}/Cargo.toml")
    set(_depends
        "${BABYLON_RUST_WORKSPACE_DIR}/Cargo.toml"
        "${BABYLON_RUST_WORKSPACE_DIR}/Cargo.lock"
        "${_manifest}"
        ${_crate_SOURCES})

    _babylon_rust_output_lib(_lib_debug "${BABYLON_RUST_TARGET_DIR}" "${BABYLON_RUST_TARGET_TRIPLE}" "debug" "${_crate_LIB_NAME}")
    _babylon_rust_output_lib(_lib_release "${BABYLON_RUST_TARGET_DIR}" "${BABYLON_RUST_TARGET_TRIPLE}" "release" "${_crate_LIB_NAME}")

    if(BABYLON_RUST_MULTI_CONFIG)
        set(_outputs "${_lib_debug}" "${_lib_release}")
        add_custom_command(
            OUTPUT ${_outputs}
            COMMAND ${CMAKE_COMMAND} -E env ${BABYLON_RUST_ENV_DEBUG} ${_crate_CARGO_ENV} ${BABYLON_RUST_CARGO_COMMAND} build --locked ${BABYLON_RUST_BUILD_STD_ARGS} --manifest-path "${_manifest}" --target-dir "${BABYLON_RUST_TARGET_DIR}" ${BABYLON_RUST_TARGET_FLAG} ${BABYLON_RUST_WORKSPACE_CARGO_ARGS} ${_crate_CARGO_ARGS}
            COMMAND ${CMAKE_COMMAND} -E env ${BABYLON_RUST_ENV_RELEASE} ${_crate_CARGO_ENV} ${BABYLON_RUST_CARGO_COMMAND} build --locked ${BABYLON_RUST_BUILD_STD_ARGS} ${BABYLON_RUST_RELEASE_BUILD_STD_ARGS} --manifest-path "${_manifest}" --target-dir "${BABYLON_RUST_TARGET_DIR}" --release ${BABYLON_RUST_TARGET_FLAG} ${BABYLON_RUST_WORKSPACE_CARGO_ARGS} ${_crate_CARGO_ARGS}
            WORKING_DIRECTORY "${_crate_CRATE_DIR}"
            DEPENDS ${_depends}
            COMMENT "${_crate_COMMENT} (debug + release)"
            VERBATIM)
    else()
        if(BABYLON_RUST_PROFILE_DIR STREQUAL "release")
            set(_lib "${_lib_release}")
        else()
            set(_lib "${_lib_debug}")
        endif()
        set(_outputs "${_lib}")
        add_custom_command(
            OUTPUT ${_outputs}
            COMMAND ${CMAKE_COMMAND} -E env ${BABYLON_RUST_ENV} ${_crate_CARGO_ENV} ${BABYLON_RUST_CARGO_COMMAND} build --locked ${BABYLON_RUST_BUILD_STD_ARGS} ${BABYLON_RUST_RELEASE_BUILD_STD_ARGS} --manifest-path "${_manifest}" --target-dir "${BABYLON_RUST_TARGET_DIR}" ${BABYLON_RUST_BUILD_FLAG} ${BABYLON_RUST_TARGET_FLAG} ${BABYLON_RUST_WORKSPACE_CARGO_ARGS} ${_crate_CARGO_ARGS}
            WORKING_DIRECTORY "${_crate_CRATE_DIR}"
            DEPENDS ${_depends}
            COMMENT "${_crate_COMMENT}"
            VERBATIM)
    endif()

    add_custom_target(${target}_rust DEPENDS ${_outputs})

    add_library(${target} STATIC IMPORTED GLOBAL)
    if(BABYLON_RUST_MULTI_CONFIG)
        set_target_properties(${target} PROPERTIES
            IMPORTED_LOCATION_DEBUG "${_lib_debug}"
            IMPORTED_LOCATION_RELEASE "${_lib_release}"
            IMPORTED_LOCATION_RELWITHDEBINFO "${_lib_release}"
            IMPORTED_LOCATION_MINSIZEREL "${_lib_release}")
    else()
        set_target_properties(${target} PROPERTIES IMPORTED_LOCATION "${_lib}")
    endif()
    add_dependencies(${target} ${target}_rust)

    if(_crate_INCLUDE_DIRS)
        target_include_directories(${target} INTERFACE ${_crate_INCLUDE_DIRS})
    endif()
endfunction()
