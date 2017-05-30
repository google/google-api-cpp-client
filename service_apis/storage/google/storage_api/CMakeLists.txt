# This is a CMake file for Cloud Storage JSON API v1
# using the Google APIs Client Library for C++.
#
# See http://google.github.io/google-api-cpp-client/latest/start/get_started
# for more information about what to do with this file.

project (google_storage_api)

# These sources assume that the CMakeLists.txt in ../.. added itself to
# the include directories so that include paths are specified explicitly
# with the directory #include "google/storage_api/..."
add_library(google_storage_api STATIC
            bucket.cc
            bucket_access_control.cc
            bucket_access_controls.cc
            buckets.cc
            channel.cc
            compose_request.cc
            notification.cc
            notifications.cc
            object.cc
            object_access_control.cc
            object_access_controls.cc
            objects.cc
            policy.cc
            rewrite_response.cc
            service_account.cc
            test_iam_permissions_response.cc
            storage_service.cc)
target_link_libraries(google_storage_api googleapis_http)
target_link_libraries(google_storage_api googleapis_internal)
target_link_libraries(google_storage_api googleapis_jsoncpp)
target_link_libraries(google_storage_api googleapis_utils)
