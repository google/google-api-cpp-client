include(CheckIncludeFile)
include(CheckCXXSourceCompiles)

find_library(JSONCPP_LIBRARY
             NAMES jsoncpp libjsoncpp
             HINTS "${CMAKE_PREFIX_PATH}/jsoncpp/lib"
             REQUIRED)
message("  -- using libjsoncpp from ${JSONCPP_LIBRARY}")

find_library(CURL_LIBRARY
             NAMES curl libcurl
             HINTS "${CMAKE_PREFIX_PATH}/curl/lib")
if (${CURL_LIBRARY} STREQUAL "CURL_LIBRARY-NOTFOUND")
  message("WARING: libcurl you do not have libcurl, skipping support for it.")
else()
  message("  -- using libcurl from ${CURL_LIBRARY}")
endif()

check_include_file("libproc.h" HAVE_LIBPROC)
check_include_file("mongoose/mongoose.h" HAVE_MONGOOSE)
check_include_file("openssl/ossl_typ.h" HAVE_OPENSSL)

check_cxx_source_compiles(
"#include <sys/stat.h>
int type = S_IRGRP | S_IWOTH;
" HAVE_UGO_PERMISSIONS)

check_cxx_source_compiles(
"#include <sys/stat.h>
int check_fstat(int fd) {
  stat64 info;
  return fstat64(fd, &info);
}" HAVE_FSTAT64)

#cmakedefine HAVE_FSTAT64
#cmakedefine HAVE_UGO_PERMISSIONS
#cmakedefine HAVE_LIBPROC
#cmakedefine HAVE_MONGOOSE
#cmakedefine HAVE_OPENSSL
