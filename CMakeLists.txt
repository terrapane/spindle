cmake_minimum_required(VERSION 3.21)

# Define the Spindle project
project(spindle
        VERSION 1.0.7.0
        DESCRIPTION "Spindle: Library for Multi-Threaded Applications"
        LANGUAGES CXX)

# Set options depending on whether this is a subproject
if(PROJECT_IS_TOP_LEVEL)
    # Option to control whether tests are built
    option(spindle_BUILD_TESTS "Build Tests for the Spindle Library" ON)
    option(spindle_SAMPLE_PROGRAMS "Build Spindle Library Sample Programs" ON)
else()
    # Option to control whether tests are built
    option(spindle_BUILD_TESTS "Build Tests for the Spindle Library" OFF)
    option(spindle_SAMPLE_PROGRAMS "Build Spindle Library Sample Programs" OFF)
endif()

# Option to control ability to install the library
option(spindle_INSTALL "Install the Spindle Library" ON)

# Determine whether clang-tidy will be performed
option(spindle_CLANG_TIDY "Use clang-tidy to perform linting during build" OFF)

# Ensure testing is enabled to surface tests in dependencies
include(CTest)

add_subdirectory(dependencies)
add_subdirectory(src)

if(spindle_SAMPLE_PROGRAMS)
    add_subdirectory(samples)
endif()

if(BUILD_TESTING AND spindle_BUILD_TESTS)
    add_subdirectory(test)
endif()
