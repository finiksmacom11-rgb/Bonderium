cmake_minimum_required(VERSION 3.20)

set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

project(Bonderium VERSION 0.3.3 LANGUAGES CXX)

include(CTest)

set(BONDERIUM_VERSION "${PROJECT_VERSION}" CACHE STRING "Application version")
option(BONDERIUM_ENABLE_UPDATES "Check GitHub Releases for application updates" OFF)
option(BONDERIUM_BUILD_BENCHMARKS "Build the simulation performance benchmark" OFF)
option(BONDERIUM_ENABLE_CODE_ANALYSIS "Enable MSVC static code analysis" OFF)

if(BONDERIUM_ENABLE_CODE_ANALYSIS)
    if(MSVC)
        add_compile_options(/analyze)
    else()
        message(FATAL_ERROR
            "BONDERIUM_ENABLE_CODE_ANALYSIS requires the MSVC compiler.")
    endif()
endif()

add_executable(Bonderium WIN32
    src/elements.cpp
    src/main.cpp
    src/physics_calculator.cpp
    src/project_file.cpp
    src/simulation.cpp
    src/updater.cpp
)

target_compile_features(Bonderium PRIVATE cxx_std_20)
target_compile_definitions(Bonderium PRIVATE
    BONDERIUM_VERSION="${BONDERIUM_VERSION}"
    BONDERIUM_ENABLE_UPDATES=$<BOOL:${BONDERIUM_ENABLE_UPDATES}>
)
target_link_libraries(Bonderium PRIVATE
    advapi32 comdlg32 psapi shell32 winhttp opengl32)

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

    add_executable(ElementTests
        tests/elements_tests.cpp
        src/elements.cpp
    )
    target_compile_features(ElementTests PRIVATE cxx_std_20)
    target_include_directories(ElementTests PRIVATE src)
    if(MSVC)
        target_compile_options(ElementTests PRIVATE /W4 /permissive- /utf-8)
    else()
        target_compile_options(ElementTests PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME ElementTests COMMAND ElementTests)

    add_executable(ProjectFileTests
        tests/project_file_tests.cpp
        src/project_file.cpp
        src/simulation.cpp
    )
    target_compile_features(ProjectFileTests PRIVATE cxx_std_20)
    target_include_directories(ProjectFileTests PRIVATE src)
    if(MSVC)
        target_compile_options(ProjectFileTests PRIVATE /W4 /permissive- /utf-8)
    else()
        target_compile_options(ProjectFileTests PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME ProjectFileTests COMMAND ProjectFileTests)

    add_executable(PhysicsCalculatorTests
        tests/physics_calculator_tests.cpp
        src/physics_calculator.cpp
    )
    target_compile_features(PhysicsCalculatorTests PRIVATE cxx_std_20)
    target_include_directories(PhysicsCalculatorTests PRIVATE src)
    if(MSVC)
        target_compile_options(PhysicsCalculatorTests PRIVATE
            /W4 /permissive- /utf-8)
    else()
        target_compile_options(PhysicsCalculatorTests PRIVATE
            -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME PhysicsCalculatorTests COMMAND PhysicsCalculatorTests)
endif()

if(BONDERIUM_BUILD_BENCHMARKS)
    add_executable(PerformanceBenchmarks
        tests/performance_benchmark.cpp
        src/simulation.cpp
    )
    target_compile_features(PerformanceBenchmarks PRIVATE cxx_std_20)
    target_include_directories(PerformanceBenchmarks PRIVATE src)
    if(MSVC)
        target_compile_options(PerformanceBenchmarks PRIVATE /W4 /permissive- /utf-8)
    else()
        target_compile_options(PerformanceBenchmarks PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endif()

if(MSVC)
    target_compile_options(Bonderium PRIVATE /W4 /permissive- /utf-8)
else()
    target_compile_options(Bonderium PRIVATE -Wall -Wextra -Wpedantic)
endif()
