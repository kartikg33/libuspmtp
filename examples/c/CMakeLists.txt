#
# Copyright 2026 Kartik Gohil
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#
# SPDX-License-Identifier: Apache-2.0
#
#
# examples/c/CMakeLists.txt
#
# Standalone C example application for libuspmtp.
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
# Usage (from the examples/c directory):
#   cmake -S . -B build -DLIBUSPMTP_SOURCE_DIR=<repo-root>
#   cmake --build build
#

cmake_minimum_required(VERSION 3.20)
project(libuspmtp-example-c LANGUAGES C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

if(NOT DEFINED LIBUSPMTP_SOURCE_DIR)
    get_filename_component(_lib_dir "${CMAKE_CURRENT_SOURCE_DIR}/.." ABSOLUTE)
    set(LIBUSPMTP_SOURCE_DIR "${_lib_dir}")
endif()

add_subdirectory("${LIBUSPMTP_SOURCE_DIR}" libuspmtp-build)

add_executable(example_c basic.c)
target_link_libraries(example_c PRIVATE libuspmtp::libuspmtp)
# The library is implemented in C++, so even though this example is pure C
# its statically linked libuspmtp.a pulls in the C++ standard library. CXX
# is enabled in this project so CMake links libstdc++ after the static lib.
set_target_properties(example_c PROPERTIES LINKER_LANGUAGE CXX)
