# This is a CMake file for Wax API v1
# using the Google APIs Client Library for C++.
#
# See http://google.github.io/google-api-cpp-client/latest/start/get_started
# for more information about what to do with this file.

project (google_wax_api)

# These sources assume that the CMakeLists.txt in ../.. added itself to
# the include directories so that include paths are specified explicitly
# with the directory #include "google/wax_api/..."
add_library(google_wax_api STATIC
            subscription.cc
            wax_create_session_request.cc
            wax_data_item.cc
            wax_list_response.cc
            wax_new_session_params.cc
            wax_new_session_request.cc
            wax_new_session_response.cc
            wax_remove_session_request.cc
            wax_remove_session_response.cc
            wax_service.cc)
target_link_libraries(google_wax_api googleapis_http)
target_link_libraries(google_wax_api googleapis_internal)
target_link_libraries(google_wax_api googleapis_jsoncpp)
target_link_libraries(google_wax_api googleapis_utils)
