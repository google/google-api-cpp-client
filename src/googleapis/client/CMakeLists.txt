# NOTE: cmake -D CURL_STATICLIB=string:ON to build curl library with static lib

project (GoogleApis_C++_Client)

# just for testing
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/test)
include_directories(${GFLAGS_INCLUDES} ${GTEST_INCLUDES})

configure_file(data/roots.pem ${EXECUTABLE_OUTPUT_PATH}/roots.pem COPYONLY)

# Abstract JSON support independent of underlying implementation.
# Libraries should use this where possible so actual implementation libraries
# are decoupled. This potentially allows different service libraries to have
# different json implementations for their data objects in the same binary.
add_library(googleapis_json STATIC data/serializable_json.cc)
target_link_libraries(googleapis_json googleapis_utils)
target_link_libraries(googleapis_json googleapis_internal)
install(TARGETS googleapis_json DESTINATION ${GOOGLEAPIS_INSTALL_LIB_DIR})
install(FILES data/serializable_json.h
        DESTINATION ${GOOGLEAPIS_INSTALL_INCLUDE_DIR}/client/data)

# Generic utility functions supporting the core googleapis components.
# This will combine the Data Layer and utility/programing support.
# since there isnt a pragmatic reason to separate their runtime packaging.
add_library(googleapis_utils STATIC
            data/base64_codec.cc
            data/codec.cc
            data/data_reader.cc
            data/data_writer.cc
            data/composite_data_reader.cc
            data/file_data_reader.cc
            data/inmemory_data_reader.cc
            data/istream_data_reader.cc
            util/date_time.cc
            util/escaping.cc
            util/file_utils.cc
            util/program_path.cc
            util/status.cc
            util/uri_template.cc
            util/uri_utils.cc)
target_link_libraries(googleapis_utils ${GLOG_LIBRARY})
target_link_libraries(googleapis_utils googleapis_internal)
install(TARGETS googleapis_utils DESTINATION ${GOOGLEAPIS_INSTALL_LIB_DIR})
install(FILES data/codec.h
              data/base64_codec.h
              data/data_reader.h
              data/data_writer.h
        DESTINATION ${GOOGLEAPIS_INSTALL_INCLUDE_DIR}/client/data)
install(FILES util/date_time.h
              util/escaping.h
              util/file_utils.h
              util/program_path.h
              util/status.h
              util/uri_template.h
              util/uri_utils.h
        DESTINATION ${GOOGLEAPIS_INSTALL_INCLUDE_DIR}/client/util)

install(FILES auth/oauth2_pending_authorizations.h
        DESTINATION ${GOOGLEAPIS_INSTALL_INCLUDE_DIR}/client/auth)

# A concrete JSON implementation using JsonCpp (external package).
add_library(googleapis_jsoncpp STATIC data/jsoncpp_data.cc)
target_link_libraries(googleapis_jsoncpp googleapis_json)
target_link_libraries(googleapis_jsoncpp googleapis_utils)
target_link_libraries(googleapis_jsoncpp jsoncpp)
target_link_libraries(googleapis_jsoncpp googleapis_internal)
install(TARGETS googleapis_jsoncpp DESTINATION ${GOOGLEAPIS_INSTALL_LIB_DIR})
install(FILES data/jsoncpp_data.h
              data/jsoncpp_data_helpers.h
        DESTINATION ${GOOGLEAPIS_INSTALL_INCLUDE_DIR}/client/data)

# The TransportLayer and ClientServiceLayer components of the
# googleapis libraries. This also includes generic Authorization components
# but not OAuth 2.0 specific components.
add_library(googleapis_http STATIC
            auth/credential_store.cc
            auth/file_credential_store.cc
            transport/ca_paths.cc
            transport/http_authorization.cc
            transport/http_request.cc
            transport/http_request_batch.cc
            transport/http_response.cc
            transport/http_scribe.cc
            transport/http_transport.cc
            transport/http_transport_global_state.cc
            transport/versioninfo.cc
            service/client_service.cc
            service/media_uploader.cc
            service/service_request_pager.cc)
target_link_libraries(googleapis_http ${GLOG_LIBRARY})
target_link_libraries(googleapis_http googleapis_utils)
target_link_libraries(googleapis_http googleapis_internal)
install(TARGETS googleapis_http DESTINATION ${GOOGLEAPIS_INSTALL_LIB_DIR})
install(FILES auth/credential_store.h
              auth/file_credential_store.h
        DESTINATION ${GOOGLEAPIS_INSTALL_INCLUDE_DIR}/client/auth)
install(FILES transport/http_authorization.h
              transport/http_request.h
              transport/http_request_batch.h
              transport/http_response.h
              transport/http_scribe.h
              transport/http_transport.h
              transport/http_types.h
              transport/versioninfo.h
        DESTINATION ${GOOGLEAPIS_INSTALL_INCLUDE_DIR}/client/transport)
install(FILES service/client_service.h
              service/media_uploader.h
              service/service_request_pager.h
        DESTINATION ${GOOGLEAPIS_INSTALL_INCLUDE_DIR}/client/service)

# Concrete scribe implementations.
add_library(googleapis_scribes STATIC
            transport/html_scribe.cc
            transport/json_scribe.cc)
target_link_libraries(googleapis_scribes googleapis_utils)
target_link_libraries(googleapis_scribes googleapis_internal)
install(TARGETS googleapis_scribes DESTINATION ${GOOGLEAPIS_INSTALL_LIB_DIR})
install(FILES transport/html_scribe.h
              transport/json_scribe.h
        DESTINATION ${GOOGLEAPIS_INSTALL_INCLUDE_DIR}/client/transport)

add_library(googleapis_jsonplayback STATIC
            transport/json_playback_transport.cc)
target_link_libraries(googleapis_jsonplayback googleapis_http)
target_link_libraries(googleapis_jsonplayback googleapis_scribes)
target_link_libraries(googleapis_jsonplayback googleapis_utils)
target_link_libraries(googleapis_jsonplayback googleapis_internal)
target_link_libraries(googleapis_jsonplayback jsoncpp)
install(TARGETS googleapis_jsonplayback
        DESTINATION ${GOOGLEAPIS_INSTALL_LIB_DIR})
install(FILES transport/json_playback_transport.h
        DESTINATION ${GOOGLEAPIS_INSTALL_INCLUDE_DIR}/client)

# Implementation of OAuth2
add_library(googleapis_oauth2 STATIC auth/oauth2_authorization.cc auth/oauth2_service_authorization.cc auth/jwt_builder.cc)
target_link_libraries(googleapis_oauth2 ${GLOG_LIBRARY})
target_link_libraries(googleapis_oauth2 googleapis_http)
target_link_libraries(googleapis_oauth2 googleapis_internal)
target_link_libraries(googleapis_oauth2 jsoncpp)
install(TARGETS googleapis_oauth2 DESTINATION ${GOOGLEAPIS_INSTALL_LIB_DIR})
install(FILES auth/oauth2_authorization.h
        DESTINATION ${GOOGLEAPIS_INSTALL_INCLUDE_DIR}/client/auth)

if (HAVE_OPENSSL)
# Concrete Codec implementation using OpenSSL (external package).
add_library(googleapis_openssl_codec STATIC data/openssl_codec.cc)
target_link_libraries(googleapis_openssl_codec crypto)
target_link_libraries(googleapis_openssl_codec dl)      # For crypto
target_link_libraries(googleapis_openssl_codec googleapis_internal)
target_link_libraries(googleapis_openssl_codec googleapis_utils)
install(TARGETS googleapis_openssl_codec
        DESTINATION ${GOOGLEAPIS_INSTALL_LIB_DIR})
install(FILES data/openssl_codec.h
        DESTINATION ${GOOGLEAPIS_INSTALL_INCLUDE_DIR}/client/data)
endif()

# Concrete HttpTransport implementation using Curl (external package).
add_library(googleapis_curl_http STATIC transport/curl_http_transport.cc)
target_link_libraries(googleapis_curl_http ${CURL_LIBRARY})
target_link_libraries(googleapis_curl_http ${GLOG_LIBRARY})
target_link_libraries(googleapis_curl_http googleapis_http)
target_link_libraries(googleapis_curl_http googleapis_internal)
if (HAVE_OPENSSL)
target_link_libraries(googleapis_curl_http ssl)
target_link_libraries(googleapis_curl_http crypto)
target_link_libraries(googleapis_curl_http dl)
endif()
install(TARGETS googleapis_curl_http DESTINATION ${GOOGLEAPIS_INSTALL_LIB_DIR})
install(FILES transport/curl_http_transport.h
        DESTINATION ${GOOGLEAPIS_INSTALL_INCLUDE_DIR}/client/transport)


if (googleapis_build_tests)
add_library(googleapis_http_transport_test_fixture STATIC
            transport/test/http_transport_test_fixture.cc)
target_link_libraries(googleapis_http_transport_test_fixture googleapis_json)
target_link_libraries(googleapis_http_transport_test_fixture googleapis_jsoncpp)
target_link_libraries(googleapis_http_transport_test_fixture googleapis_http)
target_link_libraries(googleapis_http_transport_test_fixture googleapis_scribes)
target_link_libraries(googleapis_http_transport_test_fixture googleapis_internal)
target_link_libraries(googleapis_http_transport_test_fixture ${GLOG_LIBRARY})
target_link_libraries(googleapis_http_transport_test_fixture ${GFLAGS_LIBRARY})
target_link_libraries(googleapis_http_transport_test_fixture gtest)

# Some gtest enhancements to facilitate test setup and cleanup.
add_library(googleapis_gtest_main STATIC util/test/googleapis_gtest_main.cc)
target_link_libraries(googleapis_gtest_main googleapis_internal)
target_link_libraries(googleapis_gtest_main ${GLOG_LIBRARY})
target_link_libraries(googleapis_gtest_main gtest_main)
endif (googleapis_build_tests)

# Defines an executable and adds a test for it using the most basic libraries
# Args:
#    name  - name of test. Must have a source file in test/<name>.cc
#    ...   - optional list of additional library dependencies
function(client_test module name)
  add_executable(${name} ${module}/test/${name}.cc)
  foreach (lib "${ARGN}")
    target_link_libraries(${name} ${lib})
  endforeach()
  target_link_libraries(${name} googleapis_internal)
  target_link_libraries(${name} ${GLOG_LIBRARY})
  target_link_libraries(${name} gmock)
  target_link_libraries(${name} googleapis_gtest_main)
  add_test(${name} ${EXECUTABLE_OUTPUT_PATH}/${name})
endfunction()

# Defines an executable and adds a test for it using the standard googleapis
# libraries.
#
# Args:
#    name  - name of test. Must have a source file in test/<name>.cc
#    ...   - optional list of additional library dependencies
function(googleapis_test module name)
  client_test(${module} ${name}
              googleapis_json
              googleapis_http
              googleapis_utils
              ${ARGN})
endfunction()

if (googleapis_build_tests)

client_test(util date_time_test googleapis_utils)
client_test(util file_utils_test googleapis_utils)
client_test(util uri_template_test googleapis_utils)
client_test(util uri_utils_test googleapis_utils)

client_test(data jsoncpp_data_test googleapis_jsoncpp)
client_test(data base64_codec_test googleapis_utils)
client_test(data composite_data_reader_test googleapis_utils)
client_test(data data_reader_test googleapis_utils)
# TODO(user): Fix test client_test(data data_writer_test googleapis_utils)
client_test(data file_data_reader_test googleapis_utils)
client_test(data inmemory_data_reader_test googleapis_utils)
client_test(data istream_data_reader_test googleapis_utils)
if (HAVE_OPENSSL)
client_test(data openssl_codec_test googleapis_utils googleapis_openssl_codec)
endif()

googleapis_test(auth oauth2_authorization_test googleapis_oauth2)
# TODO(user): Fix test googleapis_test(auth file_credential_store_test)
googleapis_test(transport http_request_batch_test)
googleapis_test(transport http_scribe_test)
googleapis_test(transport html_scribe_test googleapis_scribes)
googleapis_test(transport http_transport_test)
googleapis_test(transport curl_http_transport_test
                googleapis_http_transport_test_fixture
                googleapis_curl_http)
googleapis_test(transport json_playback_transport_test
                googleapis_http_transport_test_fixture
                googleapis_jsonplayback)
# The data file within the test is specified relative to the base build tree.
SET_TESTS_PROPERTIES(json_playback_transport_test PROPERTIES
                     WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

googleapis_test(service client_service_test)
googleapis_test(service media_uploader_test)
googleapis_test(service service_request_pager_test)


# This is a test but dont treat it as one because it isnt automated.
add_executable(interactive_oauth2_authorization_test
           auth/test/interactive_oauth2_authorization_test.cc)

target_link_libraries(interactive_oauth2_authorization_test
                      googleapis_json)
target_link_libraries(interactive_oauth2_authorization_test
                      googleapis_http)
target_link_libraries(interactive_oauth2_authorization_test
                      googleapis_internal)
target_link_libraries(interactive_oauth2_authorization_test
                      googleapis_curl_http)
if (HAVE_MONGOOSE)
target_link_libraries(interactive_oauth2_authorization_test
                      googleapis_mongoose)
endif (HAVE_MONGOOSE)
target_link_libraries(interactive_oauth2_authorization_test
                      googleapis_internal)
target_link_libraries(interactive_oauth2_authorization_test
                      googleapis_gtest_main)
target_link_libraries(interactive_oauth2_authorization_test
                      ${GLOG_LIBRARY})
target_link_libraries(interactive_oauth2_authorization_test
                      ${GFLAGS_LIBRARY})

endif()  # googleapis_build_tests
