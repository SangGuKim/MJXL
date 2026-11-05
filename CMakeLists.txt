cmake_minimum_required(VERSION 3.28)
project(mjxl VERSION 0.1.0 LANGUAGES C CXX)

if(NOT WIN32)
    message(FATAL_ERROR "This CMake entry point builds the Windows tools. On Linux, use make -C native.")
endif()

add_subdirectory(native)
