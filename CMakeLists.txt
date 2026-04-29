cmake_minimum_required(VERSION 3.20)

# --- MSVC: use static CRT (/MT) for plugin compatibility ---
if(MSVC)
    cmake_policy(SET CMP0091 NEW)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

project(nozzle
    VERSION 0.1.0
    DESCRIPTION "Local inter-process GPU texture sharing library"
    LANGUAGES CXX
)

# --- C++ standard & compile options ---
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# --- Logging (plog - header-only, via submodule) ---

add_library(nozzle STATIC)

target_compile_options(nozzle PRIVATE
    $<$<CXX_COMPILER_ID:Clang,AppleClang>:-fno-exceptions -fno-rtti>
    $<$<CXX_COMPILER_ID:GNU>:-fno-exceptions -fno-rtti>
    $<$<CXX_COMPILER_ID:MSVC>:/EHs-c- /GR->
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

# --- Common sources ---
target_sources(nozzle PRIVATE
    src/common/ipc.cpp
    src/common/registry.cpp
    src/common/sender.cpp
    src/common/receiver.cpp
    src/common/frame.cpp
    src/common/texture.cpp
    src/common/device.cpp
    src/common/discovery.cpp
    src/common/metadata.cpp
    src/common/pixel_access.cpp
    src/c_api/nozzle_c.cpp
)

# --- Platform-specific sources ---
if(APPLE)
    target_sources(nozzle PRIVATE
        src/backends/metal/metal_backend.mm
        src/backends/metal/metal_texture.mm
        src/backends/metal/metal_sync.mm
    )
    find_library(METAL_FRAMEWORK Metal REQUIRED)
    find_library(IOSURFACE_FRAMEWORK IOSurface REQUIRED)
    find_library(FOUNDATION_FRAMEWORK Foundation REQUIRED)
    target_link_libraries(nozzle PRIVATE
        ${METAL_FRAMEWORK}
        ${IOSURFACE_FRAMEWORK}
        ${FOUNDATION_FRAMEWORK}
        objc
    )
    enable_language(OBJCXX)
endif()

if(WIN32)
    target_sources(nozzle PRIVATE
        src/backends/d3d11/d3d11_backend.cpp
        src/backends/d3d11/d3d11_texture.cpp
        src/backends/d3d11/d3d11_sync.cpp
    )
    target_link_libraries(nozzle PRIVATE d3d11 dxgi bcrypt)
endif()

if(NOZZLE_HAS_DMA_BUF)
    target_sources(nozzle PRIVATE
        src/backends/linux/linux_texture.cpp
    )
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
    target_sources(nozzle PRIVATE
        src/backends/opengl/opengl_backend.cpp
    )
    target_compile_definitions(nozzle PUBLIC NOZZLE_HAS_OPENGL=1)
    if(APPLE)
        find_library(OPENGL_FRAMEWORK OpenGL REQUIRED)
        target_link_libraries(nozzle PRIVATE ${OPENGL_FRAMEWORK})
    elseif(WIN32)
        target_link_libraries(nozzle PRIVATE opengl32)
    elseif(NOZZLE_HAS_DMA_BUF)
        target_link_libraries(nozzle PRIVATE GL)
    endif()
endif()

# --- CMake package config export ---
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

install(TARGETS nozzle
    EXPORT nozzleTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

install(DIRECTORY include/bbb
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

install(EXPORT nozzleTargets
    FILE nozzleTargets.cmake
    NAMESPACE bbb::
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

# --- Examples ---
option(NOZZLE_BUILD_EXAMPLES "Build nozzle examples" ON)
if(NOZZLE_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

# --- Tests ---
option(NOZZLE_BUILD_TESTS "Build nozzle tests" ON)
if(NOZZLE_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
