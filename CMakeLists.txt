# The following lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.16)

set(SUPPORTED_TARGETS esp32s3)

# Read version from VERSION file
file(READ "${CMAKE_SOURCE_DIR}/VERSION" PROJECT_VERSION_STRING)
string(STRIP "${PROJECT_VERSION_STRING}" PROJECT_VERSION_STRING)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(gps_tracker VERSION ${PROJECT_VERSION_STRING})