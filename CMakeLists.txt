cmake_minimum_required(VERSION 3.20)

if(MSVC AND "${CMAKE_PROJECT_NAME}" STREQUAL "nozzle")
    cmake_policy(SET CMP0091 NEW)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

project(nozzle
    VERSION 0.1.0
    DESCRIPTION "Local inter-process GPU texture sharing library"
    LANGUAGES CXX
)

option(NOZZLE_STRICT_NO_EXCEPTIONS "Build with -fno-exceptions (strict mode)" OFF)
option(NOZZLE_INSTALL "Install nozzle library, headers, and CMake package files" ON)

# --- C++ standard & compile options ---
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# --- x86_64 ISA baseline for distributed Linux binaries ---
# Empty (the default) keeps the toolchain's own default target. Release and SDK
# builds should set this explicitly, because toolchain defaults move with build
# images. For example, the published nozzle-viewer Linux binary, built on
# GitHub's ubuntu-26.04 image with no ISA flags, contains AVX2/FMA code
# (nozzle-dev#196). The value is applied with add_compile_options() before any
# target is created, so it covers the library and everything this directory
# builds (tests, examples). A parent project that adds nozzle with
# add_subdirectory() must set its own flags for its own code.
# A parent project may set this as a normal variable before add_subdirectory().
# Only create the cache entry when no variable exists, because under CMP0126
# OLD (the policy setting for cmake_minimum_required 3.20) set(CACHE) would
# otherwise discard that normal variable on the first configure.
if(NOT DEFINED NOZZLE_X86_64_BASELINE)
    set(NOZZLE_X86_64_BASELINE "" CACHE STRING
        "x86_64 Linux ISA baseline: empty (toolchain default), v1, v2, v3 or v4")
    set_property(CACHE NOZZLE_X86_64_BASELINE PROPERTY STRINGS "" v1 v2 v3 v4)
endif()
if(NOT NOZZLE_X86_64_BASELINE STREQUAL "")
    if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux"
       OR NOT CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64|amd64)$")
        message(FATAL_ERROR
            "NOZZLE_X86_64_BASELINE is only supported for Linux x86_64 targets "
            "(CMAKE_SYSTEM_NAME=${CMAKE_SYSTEM_NAME}, "
            "CMAKE_SYSTEM_PROCESSOR=${CMAKE_SYSTEM_PROCESSOR})")
    endif()
    if(NOT CMAKE_CXX_COMPILER_ID MATCHES "^(GNU|Clang)$")
        message(FATAL_ERROR
            "NOZZLE_X86_64_BASELINE requires GCC or Clang (got ${CMAKE_CXX_COMPILER_ID})")
    endif()
    if(NOZZLE_X86_64_BASELINE STREQUAL "v1")
        set(NOZZLE_X86_64_MARCH "x86-64")
    elseif(NOZZLE_X86_64_BASELINE MATCHES "^v[234]$")
        set(NOZZLE_X86_64_MARCH "x86-64-${NOZZLE_X86_64_BASELINE}")
    else()
        message(FATAL_ERROR
            "NOZZLE_X86_64_BASELINE must be empty, v1, v2, v3 or v4 (got '${NOZZLE_X86_64_BASELINE}')")
    endif()
    add_compile_options(-march=${NOZZLE_X86_64_MARCH} -mtune=generic)
    message(STATUS "nozzle: x86_64 ISA baseline -march=${NOZZLE_X86_64_MARCH}")
endif()

# --- Logging (plog - header-only, via submodule) ---

add_library(nozzle)

if(BUILD_SHARED_LIBS)
    target_compile_definitions(nozzle
        PRIVATE NOZZLE_EXPORTS
        PUBLIC NOZZLE_SHARED
    )
    set_target_properties(nozzle PROPERTIES
        CXX_VISIBILITY_PRESET hidden
        VISIBILITY_INLINES_HIDDEN ON
    )
    target_compile_options(nozzle PRIVATE
        $<$<COMPILE_LANGUAGE:OBJCXX>:-fvisibility=hidden>
    )
endif()

set(NOZZLE_EXCEPTION_FLAGS "")
if(NOZZLE_STRICT_NO_EXCEPTIONS)
    set(NOZZLE_EXCEPTION_FLAGS
        $<$<CXX_COMPILER_ID:Clang,AppleClang>:-fno-exceptions>
        $<$<CXX_COMPILER_ID:GNU>:-fno-exceptions>
        $<$<CXX_COMPILER_ID:MSVC>:/EHs-c->
    )
endif()

target_compile_options(nozzle PRIVATE
    ${NOZZLE_EXCEPTION_FLAGS}
    $<$<CXX_COMPILER_ID:Clang,AppleClang>:-fno-rtti>
    $<$<CXX_COMPILER_ID:GNU>:-fno-rtti>
    $<$<CXX_COMPILER_ID:MSVC>:/GR->
)

# --- Platform detection ---
if(APPLE)
    set(NOZZLE_PLATFORM_MACOS 1)
    set(NOZZLE_HAS_METAL 1)
    target_compile_definitions(nozzle PUBLIC
        NOZZLE_PLATFORM_MACOS=1
        NOZZLE_HAS_METAL=1
    )
elseif(WIN32)
    set(NOZZLE_PLATFORM_WINDOWS 1)
    set(NOZZLE_HAS_D3D11 1)
    target_compile_definitions(nozzle PUBLIC
        NOZZLE_PLATFORM_WINDOWS=1
        NOZZLE_HAS_D3D11=1
    )
elseif(UNIX AND NOT APPLE)
    set(NOZZLE_PLATFORM_LINUX 1)
    set(NOZZLE_HAS_DMA_BUF 1)
    target_compile_definitions(nozzle PUBLIC
        NOZZLE_PLATFORM_LINUX=1
        NOZZLE_HAS_DMA_BUF=1
    )
endif()

# --- macOS deployment target ---
if(APPLE)
    if(NOT CMAKE_OSX_DEPLOYMENT_TARGET)
        set(CMAKE_OSX_DEPLOYMENT_TARGET "12.0" CACHE STRING "macOS deployment target" FORCE)
    endif()
endif()

# --- Include directories ---
target_include_directories(nozzle PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_include_directories(nozzle PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${plog_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/libs/plog/include
)

# --- Source lists ---
# sources/*.txt is the single list. Non-CMake wrappers read the same files, so add
# new sources there. scripts/check_source_manifest.sh keeps the lists and src/ in step.
function(nozzle_add_source_group group)
    set(list_file "${CMAKE_CURRENT_SOURCE_DIR}/sources/${group}.txt")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${list_file}")
    file(STRINGS "${list_file}" group_sources)
    if(NOT group_sources)
        message(FATAL_ERROR "${list_file} is empty")
    endif()
    target_sources(nozzle PRIVATE ${group_sources})
endfunction()

# Common sources, including SIMD files that are guarded internally by architecture.
nozzle_add_source_group(common)

# --- Platform-specific sources ---
if(APPLE)
    nozzle_add_source_group(macos)
    find_library(METAL_FRAMEWORK Metal REQUIRED)
    find_library(IOSURFACE_FRAMEWORK IOSurface REQUIRED)
    find_library(FOUNDATION_FRAMEWORK Foundation REQUIRED)
    find_library(ACCELERATE_FRAMEWORK Accelerate REQUIRED)
    target_link_libraries(nozzle PRIVATE
        $<BUILD_INTERFACE:${METAL_FRAMEWORK}>
        $<BUILD_INTERFACE:${IOSURFACE_FRAMEWORK}>
        $<BUILD_INTERFACE:${FOUNDATION_FRAMEWORK}>
        $<BUILD_INTERFACE:${ACCELERATE_FRAMEWORK}>
        objc
    )
    target_link_options(nozzle INTERFACE
        $<INSTALL_INTERFACE:LINKER:-framework,Metal>
        $<INSTALL_INTERFACE:LINKER:-framework,IOSurface>
        $<INSTALL_INTERFACE:LINKER:-framework,Foundation>
        $<INSTALL_INTERFACE:LINKER:-framework,Accelerate>
    )
    enable_language(OBJCXX)
endif()

if(WIN32)
    nozzle_add_source_group(windows)
    target_link_libraries(nozzle PRIVATE d3d11 dxgi bcrypt)
endif()

if(NOZZLE_HAS_DMA_BUF)
    nozzle_add_source_group(linux)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(DRM REQUIRED IMPORTED_TARGET libdrm)
    pkg_check_modules(GBM REQUIRED IMPORTED_TARGET gbm)
    pkg_check_modules(EGL REQUIRED IMPORTED_TARGET egl)
    target_link_libraries(nozzle PRIVATE
        PkgConfig::DRM
        PkgConfig::GBM
        PkgConfig::EGL
    )
    target_include_directories(nozzle PRIVATE
        ${DRM_INCLUDE_DIRS}
        ${GBM_INCLUDE_DIRS}
        ${EGL_INCLUDE_DIRS}
    )
endif()

# --- OpenGL interop ---
option(NOZZLE_BUILD_OPENGL "Build OpenGL interop helpers" ON)

if(NOZZLE_BUILD_OPENGL)
    nozzle_add_source_group(opengl)
    target_compile_definitions(nozzle PUBLIC NOZZLE_HAS_OPENGL=1)
    if(APPLE)
        find_library(OPENGL_FRAMEWORK OpenGL REQUIRED)
        target_link_libraries(nozzle PRIVATE
            $<BUILD_INTERFACE:${OPENGL_FRAMEWORK}>
        )
        target_link_options(nozzle INTERFACE
            $<INSTALL_INTERFACE:LINKER:-framework,OpenGL>
        )
    elseif(WIN32)
        target_link_libraries(nozzle PRIVATE opengl32)
    elseif(NOZZLE_HAS_DMA_BUF)
        target_link_libraries(nozzle PRIVATE GL)
    endif()
endif()

# --- CMake package config export ---
if(NOZZLE_INSTALL)
    include(GNUInstallDirs)
    include(CMakePackageConfigHelpers)

    install(TARGETS nozzle
        EXPORT nozzleTargets
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    )

    install(DIRECTORY include/nozzle
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    )

    install(EXPORT nozzleTargets
        FILE nozzleTargets.cmake
        NAMESPACE nozzle::
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/nozzle
    )

    configure_package_config_file(
        ${CMAKE_CURRENT_SOURCE_DIR}/cmake/nozzleConfig.cmake.in
        ${CMAKE_CURRENT_BINARY_DIR}/nozzleConfig.cmake
        INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/nozzle
    )

    write_basic_package_version_file(
        ${CMAKE_CURRENT_BINARY_DIR}/nozzleConfigVersion.cmake
        VERSION ${PROJECT_VERSION}
        COMPATIBILITY SameMajorVersion
    )

    install(FILES
        ${CMAKE_CURRENT_BINARY_DIR}/nozzleConfig.cmake
        ${CMAKE_CURRENT_BINARY_DIR}/nozzleConfigVersion.cmake
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/nozzle
    )
endif()

# --- Examples ---
option(NOZZLE_BUILD_EXAMPLES "Build nozzle examples" ON)
option(NOZZLE_DEBUG "Enable debug logging" OFF)
if(NOZZLE_DEBUG)
    target_compile_definitions(nozzle PRIVATE NOZZLE_DEBUG=1)
endif()
if(NOZZLE_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

# --- Tests ---
option(NOZZLE_BUILD_TESTS "Build nozzle tests" ON)
option(NOZZLE_BUILD_F16C_TEST "Build the focused x86 F16C conversion regression" OFF)
option(NOZZLE_ENABLE_TEST_HOOKS "Enable internal test-only C API hooks in nozzle library" OFF)
if(NOZZLE_ENABLE_TEST_HOOKS)
    target_compile_definitions(nozzle PRIVATE NOZZLE_ENABLE_TEST_HOOKS=1)
endif()
if(NOZZLE_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
