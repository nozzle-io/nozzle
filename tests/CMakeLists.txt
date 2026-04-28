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
    ${CMAKE_SOURCE_DIR}/src/common/registry.cpp
    ${CMAKE_SOURCE_DIR}/src/common/discovery.cpp
)

add_executable(nozzle_tests ${UNIT_TEST_SOURCES})

target_link_libraries(nozzle_tests PRIVATE
    Catch2::Catch2WithMain
)

target_include_directories(nozzle_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/common
    ${CMAKE_SOURCE_DIR}/include
)

include(CTest)
include(Catch)
catch_discover_tests(nozzle_tests)
