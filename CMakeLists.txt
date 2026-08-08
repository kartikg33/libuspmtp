cmake_minimum_required(VERSION 3.20)
project(libuspmtp VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ── Options ────────────────────────────────────────────────────────────────
option(LIBUSPMTP_BUILD_SHARED   "Build shared library (default: static)" OFF)
option(LIBUSPMTP_BUILD_TESTS    "Build test executables" OFF)
option(LIBUSPMTP_BUILD_EXAMPLES "Build example executables" OFF)

# ── Source files ───────────────────────────────────────────────────────────
set(LIBUSPMTP_SOURCES
    src/usp_proto.cpp
    src/transport.cpp
    src/client.cpp
    src/libuspmtp.cpp
)

# ── Library target ─────────────────────────────────────────────────────────
if(LIBUSPMTP_BUILD_SHARED)
    add_library(libuspmtp SHARED ${LIBUSPMTP_SOURCES})
    target_compile_definitions(libuspmtp
        PRIVATE  LIBUSPMTP_BUILD_SHARED LIBUSPMTP_EXPORTS
        PUBLIC   LIBUSPMTP_BUILD_SHARED)
else()
    add_library(libuspmtp STATIC ${LIBUSPMTP_SOURCES})
endif()

# Alias for consumers using find_package / add_subdirectory
add_library(libuspmtp::libuspmtp ALIAS libuspmtp)

set_target_properties(libuspmtp PROPERTIES
    OUTPUT_NAME uspmtp
    VERSION     ${PROJECT_VERSION}
    SOVERSION   0
)

target_include_directories(libuspmtp
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# Thread support (pthreads on Linux/macOS)
find_package(Threads REQUIRED)
target_link_libraries(libuspmtp PRIVATE Threads::Threads)

# Compiler warnings
target_compile_options(libuspmtp PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:
        -Wall -Wextra -Wpedantic -Wno-unused-parameter>
)

# ── Tests ──────────────────────────────────────────────────────────────────
if(LIBUSPMTP_BUILD_TESTS)
    enable_testing()

    foreach(test_name IN ITEMS test_proto test_transport test_client)
        add_executable(${test_name} tests/${test_name}.cpp)
        target_link_libraries(${test_name} PRIVATE libuspmtp::libuspmtp Threads::Threads)
        target_include_directories(${test_name} PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/src
            ${CMAKE_CURRENT_SOURCE_DIR}/include)
        add_test(NAME ${test_name} COMMAND ${test_name})
    endforeach()
endif()

# ── Examples ───────────────────────────────────────────────────────────────
if(LIBUSPMTP_BUILD_EXAMPLES)
    add_executable(example_c examples/c/basic.c)
    target_link_libraries(example_c PRIVATE libuspmtp::libuspmtp)
    target_include_directories(example_c PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)

    add_executable(example_cpp examples/cpp/basic.cpp)
    target_link_libraries(example_cpp PRIVATE libuspmtp::libuspmtp Threads::Threads)
    target_include_directories(example_cpp PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/src)
endif()

# ── Install rules ──────────────────────────────────────────────────────────
include(GNUInstallDirs)

install(TARGETS libuspmtp
    EXPORT  libuspmtpTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

install(FILES include/libuspmtp.h
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

install(EXPORT libuspmtpTargets
    FILE      libuspmtpTargets.cmake
    NAMESPACE libuspmtp::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/libuspmtp
)

# Generate and install a basic config file
include(CMakePackageConfigHelpers)
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/libuspmtpConfigVersion.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)

configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/libuspmtpConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/libuspmtpConfig.cmake"
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/libuspmtp
)

install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/libuspmtpConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/libuspmtpConfigVersion.cmake"
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/libuspmtp
)
