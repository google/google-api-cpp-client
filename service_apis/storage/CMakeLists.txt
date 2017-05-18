# This is a CMake file for YouTube Data API v3
# using the Google APIs Client Library for C++.
#
# See http://google.github.io/google-api-cpp-client/latest/start/get_started
# for more information about what to do with this file.

project (storage)
INCLUDE_DIRECTORIES(${CMAKE_CURRENT_SOURCE_DIR}/.)

add_subdirectory(google/storage_api)
