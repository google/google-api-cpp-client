project (GoogleApis_C++_Samples)

include_directories(${GFLAGS_INCLUDES})
INCLUDE_DIRECTORIES(${GOOGLEAPIS_SERVICE_REPOSITORY_DIR}/calendar)
INCLUDE_DIRECTORIES(${GOOGLEAPIS_SERVICE_REPOSITORY_DIR}/drive)
INCLUDE_DIRECTORIES(${GOOGLEAPIS_SERVICE_REPOSITORY_DIR}/youtube)

# This is the basic sample from the tutorial
add_executable(calendar_sample calendar_sample_main.cc)
target_link_libraries(calendar_sample google_calendar_api)
target_link_libraries(calendar_sample googleapis_jsoncpp)
target_link_libraries(calendar_sample googleapis_curl_http)
target_link_libraries(calendar_sample googleapis_http)
target_link_libraries(calendar_sample googleapis_oauth2)
target_link_libraries(calendar_sample googleapis_utils)
if (HAVE_OPENSSL)
target_link_libraries(calendar_sample googleapis_openssl_codec)
endif()
target_link_libraries(calendar_sample ${GFLAGS_LIBRARY})
target_link_libraries(calendar_sample pthread)


# A helper library used by other samples
# It provides a shell command processor for sending messages interactively.
add_library(sample_contrib STATIC
            command_processor.cc
            installed_application.cc)
target_link_libraries(sample_contrib googleapis_http)
target_link_libraries(sample_contrib googleapis_oauth2)
target_link_libraries(sample_contrib googleapis_mongoose)
target_link_libraries(sample_contrib googleapis_utils)
if (HAVE_OPENSSL)
target_link_libraries(sample_contrib googleapis_openssl_codec)
endif()

# A sample that interacts with the GDrive Service.
add_executable(gdrive_sample gdriveutil_main.cc)
target_link_libraries(gdrive_sample google_drive_api)
target_link_libraries(gdrive_sample sample_contrib)
target_link_libraries(gdrive_sample googleapis_jsoncpp)
target_link_libraries(gdrive_sample googleapis_curl_http)
target_link_libraries(gdrive_sample googleapis_http)
target_link_libraries(gdrive_sample googleapis_oauth2)
target_link_libraries(gdrive_sample googleapis_utils)
target_link_libraries(gdrive_sample ${GFLAGS_LIBRARY})

# A sample that interacts with the YouTube Service for Live Broadcasts.
add_executable(youtube_broadcast_sample youtube_broadcast_main.cc)
target_link_libraries(youtube_broadcast_sample youtube_api)
target_link_libraries(youtube_broadcast_sample sample_contrib)
target_link_libraries(youtube_broadcast_sample googleapis_jsoncpp)
target_link_libraries(youtube_broadcast_sample googleapis_curl_http)
target_link_libraries(youtube_broadcast_sample googleapis_http)
target_link_libraries(youtube_broadcast_sample googleapis_oauth2)
target_link_libraries(youtube_broadcast_sample googleapis_utils)
target_link_libraries(youtube_broadcast_sample ${GFLAGS_LIBRARY})

add_executable(webflow_sample
               webflow_sample_main.cc
               abstract_gplus_login_flow.cc
               abstract_login_flow.cc
               abstract_webserver_login_flow.cc)
target_link_libraries(webflow_sample sample_contrib)
target_link_libraries(webflow_sample googleapis_jsoncpp)
target_link_libraries(webflow_sample googleapis_curl_http)
target_link_libraries(webflow_sample googleapis_http)
target_link_libraries(webflow_sample googleapis_oauth2)
target_link_libraries(webflow_sample googleapis_utils)
target_link_libraries(webflow_sample ${GFLAGS_LIBRARY})
