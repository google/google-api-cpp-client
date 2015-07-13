/*
 * \copyright Copyright 2013 Google Inc. All Rights Reserved.
 * \license @{
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * @}
 */
// This file provides some convienence routines for attempting to build
// under windows. The windows build is not supported at this time since
// it is not clear that the value is not worth the effort.
//
// The assumption for the windows port, at this time anyway, is that
// the data interfaces are the same as the unix-flavored OS's even though
// this is not the case at all for actual Windows APIs.
// This means that:
//    Files are delimited by '/' (APIs will translate paths internally only
//    to call into windows)
//
//    std::string uses char (APIs will convert to TCHAR only to call into
//    windows)
//
//    the time epoch is seconds since 1/1/1970 (APIs will convert to windows
//    epoch of 100 nanosecond units since 1/1/1601 as needed).
//
#if defined(_MSC_VER) && !defined(GOOGLEAPIS_BASE_WINDOWS_COMPATABILITY_H_)
#define GOOGLEAPIS_BASE_WINDOWS_COMPATABILITY_H_

#include <WinSock2.h>
#include <Windows.h>

#include <time.h>

#include <string>
using std::string;
#include "googleapis/base/port.h"
namespace googleapis {

inline struct tm* gmtime_r(const time_t *timep, struct tm* result) {
  return !gmtime_s(result, timep) ? NULL : result;
}

// windows.h is #defining DELETE which causes syntax errors for our
// DELETE enum values.
#ifdef DELETE
#undef DELETE
#endif

// windows.h is also #defining ERROR and although it doesn't conflict with
// of the library.
#ifdef ERROR
#undef ERROR
#endif

// Converts muti-byte utf8 encoding std::string into windows multibyte wide
// string.
const WCHAR* ToWindowsWideString(const string& from, string* to);

// std::string is probably a utf8 encoding of char
// but windows APIs uses strings of TCHAR which are wide characters
// if we are using UNICODE. So this method converts a std::string of utf8 char
// into a std::string of TCHAR (which might be wide).
inline const TCHAR* ToWindowsString(const string& from, string* to) {
#ifdef UNICODE
  return ToWindowsWideString(from, to);  // TCHAR is WCHAR
#else
  *to = from;
  return reinterpret_cast<const TCHAR *>(to->c_str());
#endif
}

// Converts from TCHAR windows strings into utf8 encoding std::string.
string FromWindowsStr(const TCHAR* windows);

// Converts '/' delimited paths into windows '\' delimited paths.
string ToWindowsPath(const string& str);

// Converts windows '\' delimited paths into standard '/' delimited paths.
string FromWindowsPath(const string& str);

}  // namespace googleapis
#endif  // defined(_MSC_VER) && !defined(GOOGLEAPIS_BASE_WINDOWS_COMPATABILITY_H_)
