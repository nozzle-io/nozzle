cmake_minimum_required(VERSION 3.20)
project(nozzle_plugin_isolation LANGUAGES CXX)

if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
    message(FATAL_ERROR "This fixture verifies Linux ELF RTLD_GLOBAL binding")
endif()
foreach(required NOZZLE_CORE_SOURCE NOZZLE_CORE_REVISION NOZZLE_PROBE_NAME)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "${required} is required")
    endif()
endforeach()
if(NOT NOZZLE_PROBE_NAME MATCHES "^[a-z_][a-z0-9_]*$")
    message(FATAL_ERROR "NOZZLE_PROBE_NAME must be a C identifier")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(NOZZLE_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(NOZZLE_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(NOZZLE_INSTALL OFF CACHE BOOL "" FORCE)
add_subdirectory("${NOZZLE_CORE_SOURCE}" core)

# Do not hide the fixture target: the production static core must isolate itself.
add_library(plugin MODULE plugin.cpp)
target_link_libraries(plugin PRIVATE nozzle ${CMAKE_DL_LIBS})
target_compile_definitions(plugin PRIVATE
    NOZZLE_CORE_REVISION="${NOZZLE_CORE_REVISION}"
    NOZZLE_PROBE_NAME=${NOZZLE_PROBE_NAME}
    NOZZLE_EXPECT_OPENGL=$<BOOL:${NOZZLE_BUILD_OPENGL}>
)

# No nozzle linkage: this executable models a host, not a third core copy.
add_executable(plugin_host host.cpp)
target_link_libraries(plugin_host PRIVATE ${CMAKE_DL_LIBS})
