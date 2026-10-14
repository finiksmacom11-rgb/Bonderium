cmake_minimum_required(VERSION 3.20)

set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

project(Bonderium VERSION 0.1.0 LANGUAGES CXX)

include(CTest)

set(BONDERIUM_VERSION "${PROJECT_VERSION}" CACHE STRING "Application version")
option(BONDERIUM_ENABLE_UPDATES "Check GitHub Releases for application updates" OFF)

add_executable(Bonderium WIN32
    src/main.cpp
    src/simulation.cpp
    src/updater.cpp
)

target_compile_features(Bonderium PRIVATE cxx_std_20)
target_compile_definitions(Bonderium PRIVATE
    BONDERIUM_VERSION="${BONDERIUM_VERSION}"
    BONDERIUM_ENABLE_UPDATES=$<BOOL:${BONDERIUM_ENABLE_UPDATES}>
)
target_link_libraries(Bonderium PRIVATE advapi32 psapi shell32 winhttp opengl32)

if(BUILD_TESTING)
    add_executable(SimulationTests
        tests/simulation_tests.cpp
        src/simulation.cpp
    )
    target_compile_features(SimulationTests PRIVATE cxx_std_20)
    target_include_directories(SimulationTests PRIVATE src)
    if(MSVC)
        target_compile_options(SimulationTests PRIVATE /W4 /permissive- /utf-8)
    else()
        target_compile_options(SimulationTests PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME SimulationTests COMMAND SimulationTests)
endif()

if(MSVC)
    target_compile_options(Bonderium PRIVATE /W4 /permissive- /utf-8)
else()
    target_compile_options(Bonderium PRIVATE -Wall -Wextra -Wpedantic)
endif()
