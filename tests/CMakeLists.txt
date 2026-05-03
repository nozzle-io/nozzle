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
    ${CMAKE_SOURCE_DIR}/src/common/ipc.cpp
    ${CMAKE_SOURCE_DIR}/src/common/registry.cpp
    ${CMAKE_SOURCE_DIR}/src/common/discovery.cpp
    ${CMAKE_SOURCE_DIR}/src/common/channel_swizzle.cpp
)

if(APPLE)
    list(APPEND UNIT_TEST_SOURCES
        ${CMAKE_SOURCE_DIR}/src/common/channel_swizzle_vimage.cpp
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
)

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

target_include_directories(nozzle_integration_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/common
)

target_compile_options(nozzle_integration_tests PRIVATE
    $<$<CXX_COMPILER_ID:Clang,AppleClang>:-fno-exceptions -fno-rtti>
    $<$<CXX_COMPILER_ID:GNU>:-fno-exceptions -fno-rtti>
)

include(CTest)
include(Catch)
catch_discover_tests(nozzle_tests)
catch_discover_tests(nozzle_integration_tests)
