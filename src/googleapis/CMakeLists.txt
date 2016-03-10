project (GoogleApis_C++_Client_Utils)

add_subdirectory(client)

# The rest of googleapis is auto-generated.
# Really we'd like this to be part of some other external project.
add_library(googleapis_internal STATIC
./base/callback.cc
./base/once.cc
./base/strtoint.cc
./base/windows_compatability.cc
./strings/ascii_ctype.cc
./strings/case.cc
./strings/memutil.cc
./strings/numbers.cc
./strings/split.cc
./strings/strcat.cc
./strings/stringpiece.cc
./strings/strip.cc
./strings/util.cc
./util/executor.cc
./util/file.cc
./util/hash.cc
./util/status.cc
)
target_link_libraries(googleapis_internal ${GLOG_LIBRARY})
install(TARGETS googleapis_internal DESTINATION ${GOOGLEAPIS_INSTALL_LIB_DIR})

install(FILES 
base/callback-specializations.h
base/callback-types.h
base/callback.h
base/int128.h
base/integral_types.h
base/macros.h
base/mutex.h
base/once.h
base/port.h
base/strtoint.h
base/template_util.h
base/thread_annotations.h
base/windows_compatability.h
DESTINATION ${GOOGLEAPIS_INSTALL_INCLUDE_DIR}/base)

install(FILES 
strings/ascii_ctype.h
strings/case.h
strings/join.h
strings/memutil.h
strings/numbers.h
strings/split.h
strings/strcat.h
strings/stringpiece.h
strings/strip.h
strings/util.h
DESTINATION ${GOOGLEAPIS_INSTALL_INCLUDE_DIR}/strings)

install(FILES 
util/case_insensitive_hash.h
util/executor.h
util/file.h
util/hash.h
util/mock_executor.h
util/status.h
DESTINATION ${GOOGLEAPIS_INSTALL_INCLUDE_DIR}/util)

