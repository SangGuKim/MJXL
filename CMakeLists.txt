cmake_minimum_required(VERSION 3.28)
project(mjxl VERSION 0.1.0 LANGUAGES C CXX)

option(MJXL_BUILD_TESTS "Build portable codec/archive regression checks" OFF)
if(MJXL_BUILD_TESTS)
    enable_testing()
endif()

add_subdirectory(native)
