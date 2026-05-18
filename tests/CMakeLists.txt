include(FetchContent)

FetchContent_Declare(
    Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG v3.6.0
)

FetchContent_MakeAvailable(Catch2)

# --- Unit test executable ---
# Links only the source files needed for registry/discovery tests
# instead of the full nozzle library (which has unfinished compilation units).
set(UNIT_TEST_SOURCES
    unit/test_main.cpp
    unit/test_registry.cpp
    unit/test_discovery.cpp
    unit/test_channel_swizzle.cpp
    unit/test_format_resolve.cpp
    unit/test_resolved_format.cpp
    unit/test_format_convert.cpp
    unit/test_shared_metadata.cpp
    ${CMAKE_SOURCE_DIR}/src/common/ipc.cpp
    ${CMAKE_SOURCE_DIR}/src/common/registry.cpp
    ${CMAKE_SOURCE_DIR}/src/common/discovery.cpp
    ${CMAKE_SOURCE_DIR}/src/common/channel_swizzle.cpp
    ${CMAKE_SOURCE_DIR}/src/common/format_resolve.cpp
    ${CMAKE_SOURCE_DIR}/src/common/format_convert.cpp
    ${CMAKE_SOURCE_DIR}/src/common/format_convert_sse2.cpp
    ${CMAKE_SOURCE_DIR}/src/common/format_convert_neon.cpp
)

if(APPLE)
    list(APPEND UNIT_TEST_SOURCES
        ${CMAKE_SOURCE_DIR}/src/common/channel_swizzle_vimage.cpp
        ${CMAKE_SOURCE_DIR}/src/common/format_convert_vimage.cpp
    )
endif()

add_executable(nozzle_tests ${UNIT_TEST_SOURCES})

target_link_libraries(nozzle_tests PRIVATE
    Catch2::Catch2WithMain
)

if(APPLE)
    find_library(ACCELERATE_FRAMEWORK Accelerate REQUIRED)
    target_link_libraries(nozzle_tests PRIVATE ${ACCELERATE_FRAMEWORK})
endif()

target_include_directories(nozzle_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/common
    ${CMAKE_SOURCE_DIR}/include
)

if(WIN32)
    target_compile_definitions(nozzle_tests PRIVATE
        NOZZLE_PLATFORM_WINDOWS=1
        NOZZLE_HAS_D3D11=1
    )
elseif(APPLE)
    target_compile_definitions(nozzle_tests PRIVATE
        NOZZLE_PLATFORM_MACOS=1
        NOZZLE_HAS_METAL=1
    )
elseif(UNIX AND NOT APPLE)
    target_compile_definitions(nozzle_tests PRIVATE
        NOZZLE_PLATFORM_LINUX=1
        NOZZLE_HAS_DMA_BUF=1
    )
endif()

# --- Integration test executable ---
# Links against the full nozzle library (including Metal backend on macOS).
set(INTEGRATION_TEST_SOURCES
    integration/test_cross_process.cpp
    integration/test_c_sender_desc.cpp
    integration/test_cpp_sender_desc.cpp
    integration/test_integration_fallback_getters.cpp
    integration/test_publish_mismatch.cpp
)

    if(APPLE)
    list(APPEND INTEGRATION_TEST_SOURCES
        integration/test_metal_format_mapping.cpp
        integration/test_metal_device_injection.mm
        integration/test_metal_native_publish.mm
    )
endif()

if(WIN32)
    list(APPEND INTEGRATION_TEST_SOURCES
        integration/test_d3d11_device_injection.cpp
    )
endif()

if(NOT BUILD_SHARED_LIBS)
    add_executable(nozzle_integration_tests ${INTEGRATION_TEST_SOURCES})

    if(APPLE)
        set(NOZZLE_FORCE_LOAD_FLAG "-Wl,-force_load")
    else()
        set(NOZZLE_FORCE_LOAD_FLAG "")
    endif()

    target_link_libraries(nozzle_integration_tests PRIVATE
        Catch2::Catch2WithMain
        ${NOZZLE_FORCE_LOAD_FLAG}
        nozzle
    )

    if(WIN32)
        target_link_libraries(nozzle_integration_tests PRIVATE d3d11 dxgi)
    endif()

    target_include_directories(nozzle_integration_tests PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/src/common
    )

    target_compile_options(nozzle_integration_tests PRIVATE
        $<$<CXX_COMPILER_ID:Clang,AppleClang>:-fno-rtti>
        $<$<CXX_COMPILER_ID:GNU>:-fno-rtti>
    )
    if(NOZZLE_STRICT_NO_EXCEPTIONS)
        target_compile_options(nozzle_integration_tests PRIVATE
            $<$<CXX_COMPILER_ID:Clang,AppleClang>:-fno-exceptions>
            $<$<CXX_COMPILER_ID:GNU>:-fno-exceptions>
        )
    endif()
endif()

include(CTest)
include(Catch)
catch_discover_tests(nozzle_tests)
if(NOT BUILD_SHARED_LIBS)
    catch_discover_tests(nozzle_integration_tests)
endif()

# --- C API test executable ---
# Pure C API logic tests that require no GPU/IPC resources.
# Links against the full nozzle library but runs as gating (no || true).
add_executable(nozzle_c_api_tests
    c_api/test_swizzle.cpp
    c_api/test_fallback_info.cpp
    c_api/test_fill_alpha.cpp
    c_api/test_error_codes.cpp
)

target_link_libraries(nozzle_c_api_tests PRIVATE
    Catch2::Catch2WithMain
    nozzle
)

target_include_directories(nozzle_c_api_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/common
    ${CMAKE_SOURCE_DIR}/src/c_api
)

if(NOZZLE_ENABLE_TEST_HOOKS)
    target_compile_definitions(nozzle_c_api_tests PRIVATE NOZZLE_ENABLE_TEST_HOOKS=1)
endif()

catch_discover_tests(nozzle_c_api_tests)
