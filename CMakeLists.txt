cmake_minimum_required(VERSION 3.20)

project(Bonderium LANGUAGES CXX)

add_executable(Bonderium WIN32 src/main.cpp)

target_compile_features(Bonderium PRIVATE cxx_std_20)

if(MSVC)
    target_compile_options(Bonderium PRIVATE /W4 /permissive-)
else()
    target_compile_options(Bonderium PRIVATE -Wall -Wextra -Wpedantic)
endif()
