cmake_minimum_required(VERSION 3.18...3.30)

# SKBUILD_PROJECT_VERSION is provided by scikit-build-core; fall back for
# plain `cmake` dev builds.
if(DEFINED SKBUILD_PROJECT_VERSION)
    project(casacoretables VERSION ${SKBUILD_PROJECT_VERSION} LANGUAGES CXX)
else()
    project(casacoretables VERSION 0.0.1 LANGUAGES CXX)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

option(BUILD_CPP_TESTS "Build the vendored casacore C++ table unit tests" OFF)
option(BUILD_PYTHON_MODULE "Build the pybind11 _tables extension module" ON)

# The vendored, minimal-dependency casacore tables static library.
add_subdirectory(extern)

# pybind11 extension module (built by default; required for the wheel).
if(BUILD_PYTHON_MODULE)
    add_subdirectory(bindings)
endif()

# Vendored casacore C++ unit tests (developer convenience: -DBUILD_CPP_TESTS=ON).
if(BUILD_CPP_TESTS)
    enable_testing()
    add_subdirectory(tests/cpp)
endif()
