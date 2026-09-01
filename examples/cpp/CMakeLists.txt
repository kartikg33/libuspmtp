#
# examples/cpp/CMakeLists.txt
#
# Standalone C++ example application for libuspmtp.
#
# This is deliberately an app-level build that is independent of the library's
# own build. It pulls the library in via add_subdirectory so that the example
# only needs the library source tree, then links the example binary against
# the libuspmtp::libuspmtp target (statically linked).
#
# Configuration:
#   LIBUSPMTP_SOURCE_DIR  Path to the libuspmtp source root. Defaults to the
#                         repository root (one directory up from examples).
#
# Usage (from the examples/cpp directory):
#   cmake -S . -B build -DLIBUSPMTP_SOURCE_DIR=<repo-root>
#   cmake --build build
#

cmake_minimum_required(VERSION 3.20)
project(libuspmtp-example-cpp LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT DEFINED LIBUSPMTP_SOURCE_DIR)
    get_filename_component(_lib_dir "${CMAKE_CURRENT_SOURCE_DIR}/.." ABSOLUTE)
    set(LIBUSPMTP_SOURCE_DIR "${_lib_dir}")
endif()

add_subdirectory("${LIBUSPMTP_SOURCE_DIR}" libuspmtp-build)

find_package(Threads REQUIRED)

add_executable(example_cpp basic.cpp)
target_link_libraries(example_cpp PRIVATE libuspmtp::libuspmtp Threads::Threads)
# The C++ example uses the internal C++ API header, which lives in the library
# source tree and is not installed. Make it available here.
target_include_directories(example_cpp PRIVATE "${LIBUSPMTP_SOURCE_DIR}/src")
