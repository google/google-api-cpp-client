cmake_minimum_required (VERSION 2.6)
project (GoogleApis_C++)

set(googleapis_VERSION_MAJOR 0)
set(googleapis_VERSION_MINOR 1)
set(googleapis_VERSION_PATCH 0)
set(googleapis_VERSION_DECORATOR "")

enable_testing()

set(LIBRARY_OUTPUT_PATH ${CMAKE_BINARY_DIR}/lib)
set(EXECUTABLE_OUTPUT_PATH ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_INSTALL_PREFIX "export")
set(GOOGLEAPIS_INSTALL_BASE_DIR ".")
set(GOOGLEAPIS_INSTALL_LIB_DIR ${GOOGLEAPIS_INSTALL_BASE_DIR}/lib)
set(GOOGLEAPIS_INSTALL_INCLUDE_DIR
    ${GOOGLEAPIS_INSTALL_BASE_DIR}/include/googleapis)

# BUILD_SHARED_LIBS is a standard CMake variable, but we declare it here to
# make it prominent since we are turning it off by default.
option(BUILD_SHARED_LIBS "Build shared libraries (DLLs)." OFF)
option(gtest_force_shared_crt "Force Gmock to use standard compiler flags" ON)

option(googleapis_build_tests "Build all of googleapis' own tests." ON)
option(googleapis_build_samples "Build googleapis sample programs." ON)
option(googleapis_build_service_apis "Build specialized service APIs." ON)

set(GOOGLEAPIS_EXTERNAL_DEPENDENCIES_DIR ${CMAKE_BINARY_DIR}/external_dependencies/install CACHE STRING "Root directory where external dependencies are located")

if (MSVC)
  option(gtest_force_shared_crt "Force Gmock to use standard compiler flags" ON)
  ADD_DEFINITIONS(-DCOMPILER_MSVC -DNOMINMAX -DWIN32_LEAN_AND_MEAN /Zm1000 /J
                  -D_WIN32_WINNT=0x0600
                  -D_UNICODE -DUNICODE
                  -DGFLAGS_DLL_DECL= -DGFLAGS_DLL_DEFINE_FLAG=
                  -D_VARIADIC_MAX=10 -DGLOG_NO_ABBREVIATED_SEVERITIES)
  set(GLOG_LIBRARY libglog)
  set(GFLAGS_LIBRARY libgflags)
  set(CURL_LIBRARY libcurl_imp)
else()
  set(GLOG_LIBRARY glog)
  set(GFLAGS_LIBRARY gflags)
  set(CURL_LIBRARY curl)
endif()

include_directories(
	"${GOOGLEAPIS_EXTERNAL_DEPENDENCIES_DIR}/include"
	"${CMAKE_CURRENT_SOURCE_DIR}/src")

link_directories("${GOOGLEAPIS_EXTERNAL_DEPENDENCIES_DIR}/lib"
                 "${GOOGLEAPIS_INSTALL_LIB_DIR}")

set(CMAKE_REQUIRED_INCLUDES "${GOOGLEAPIS_EXTERNAL_DEPENDENCIES_DIR}/include")
include("introspect.cmake")
configure_file(
  "${PROJECT_SOURCE_DIR}/src/googleapis/config.h.in"
  "${PROJECT_SOURCE_DIR}/src/googleapis/config.h")


# This environment is set up such that bundles can be downloaded as
# subdirectoris of ./service_apis and they will compile into libraries.
# For other files to compile against them they will need to add
# $(GOOGLEAPIS_SERVICE_REPOSITORY_DIR)/downloaded_apis_dir
# to the include path. For example:
# INCLUDE_DIRECTORIES(${GOOGLEAPIS_SERVICE_REPOSITORY_DIR}/google_drive_api)
#
# For a list of available APIs, see:
# http://google.github.io/google-api-cpp-client/latest/available_service_apis.html
set(GOOGLEAPIS_SERVICE_REPOSITORY_DIR ${CMAKE_BINARY_DIR}/service_apis)

add_subdirectory(src)

if (googleapis_build_samples)
  set(googleapis_build_service_apis true)  # force on
endif()
if (googleapis_build_service_apis)
  add_subdirectory(service_apis)
endif()

# Build sampels after the service apis
# but keep them under src
if (googleapis_build_samples)
  add_subdirectory(src/samples)
endif()
