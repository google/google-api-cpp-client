# This is a CMake file for Drive API v2
# using the Google APIs Client Library for C++.
#
# See http://google.github.io/google-api-cpp-client/latest/start/get_started
# for more information about what to do with this file.

project (google_drive_api)

# These sources assume that the CMakeLists.txt in ../.. added itself to
# the include directories so that include paths are specified explicitly
# with the directory #include "google/drive_api/..."
add_library(google_drive_api STATIC
            about.cc
            app.cc
            app_list.cc
            change.cc
            change_list.cc
            channel.cc
            child_list.cc
            child_reference.cc
            comment.cc
            comment_list.cc
            comment_reply.cc
            comment_reply_list.cc
            file.cc
            file_list.cc
            parent_list.cc
            parent_reference.cc
            permission.cc
            permission_id.cc
            permission_list.cc
            property.cc
            property_list.cc
            revision.cc
            revision_list.cc
            user.cc
            drive_service.cc)
target_link_libraries(google_drive_api googleapis_http)
target_link_libraries(google_drive_api googleapis_internal)
target_link_libraries(google_drive_api googleapis_jsoncpp)
target_link_libraries(google_drive_api googleapis_utils)
