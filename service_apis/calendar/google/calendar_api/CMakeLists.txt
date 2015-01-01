# This is a CMake file for Calendar API v3
# using the Google APIs Client Library for C++.
#
# See http://google.github.io/google-api-cpp-client/latest/start/get_started
# for more information about what to do with this file.

project (google_calendar_api)

# These sources assume that the CMakeLists.txt in ../.. added itself to
# the include directories so that include paths are specified explicitly
# with the directory #include "google/calendar_api/..."
add_library(google_calendar_api STATIC
            acl.cc
            acl_rule.cc
            calendar.cc
            calendar_list.cc
            calendar_list_entry.cc
            calendar_notification.cc
            channel.cc
            color_definition.cc
            colors.cc
            error.cc
            event.cc
            event_attachment.cc
            event_attendee.cc
            event_date_time.cc
            event_reminder.cc
            events.cc
            free_busy_calendar.cc
            free_busy_group.cc
            free_busy_request.cc
            free_busy_request_item.cc
            free_busy_response.cc
            setting.cc
            settings.cc
            time_period.cc
            calendar_service.cc)
target_link_libraries(google_calendar_api googleapis_http)
target_link_libraries(google_calendar_api googleapis_internal)
target_link_libraries(google_calendar_api googleapis_jsoncpp)
target_link_libraries(google_calendar_api googleapis_utils)
