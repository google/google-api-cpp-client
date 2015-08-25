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
// TODO(user): 20130723
//
// Path handling is not right in general.
//   - Need to handle share paths that start with '//'
//   - We dont handle device specifiers ('C:' etc)
// The code in here is sufficient just for existing tests and smoke testing
// samples. Additional concepts introduced by Windows are generally not
// yet accomodated.
//
// Need to do a thorough scrubbing on TCHAR in the code.
// And also the use of _MSC_VER is the appropriate guard in each
// porting situation.

#include "googleapis/base/windows_compatability.h"

#include <memory>
#include <string>
using std::string;

#include <glog/logging.h>
#include "googleapis/strings/util.h"
#include "googleapis/base/port.h"

namespace googleapis {

using std::string;

#ifdef _MSC_VER

string FromWindowsPath(const string& path) {
  return StringReplace(path, "\\", "/", true);
}

string ToWindowsPath(const string& str) {
  return StringReplace(str, "/", "\\", true);
}

const WCHAR* ToWindowsWideString(const string& from, string* to) {
  int buffer_size = from.size() + 1;
  std::unique_ptr<WCHAR[]> wide_string(new WCHAR[buffer_size]);

  int len = MultiByteToWideChar(
      CP_UTF8,
      0,
      from.c_str(),
      from.size(),
      wide_string.get(),
      buffer_size - 1);
  CHECK_NE(0, len) << "Could not convert " << from
                   << " error=" << GetLastError();
  wide_string.get()[len] = 0;
  to->assign(reinterpret_cast<char*>(wide_string.get()),
             (len + 1) * sizeof(WCHAR));
  return reinterpret_cast<const WCHAR*>(to->c_str());
}

string FromWindowsStr(const TCHAR* windows) {
#ifndef UNICODE
  return string(windows);
#else
  int data_size = wcslen(windows);
  std::unique_ptr<char[]> data(new char[2 * data_size + 2]);
  int len = WideCharToMultiByte(CP_UTF8, 0,
                                windows, data_size,
                                data.get(), 2 * data_size + 1,
                                NULL, NULL);
  CHECK_NE(0, len) << "Failed with " << GetLastError();
  return string(data.get(), len);
#endif
}

// These call the windows _vsnprintf, but always NUL-terminate.
int base_port_MSVC_vsnprintf(char *str, size_t size, const char *format,
                             va_list ap) {
  if (size == 0)        // not even room for a \0?
    return -1;          // not what C99 says to do, but what windows does
  str[size - 1] = '\0';
  return _vsnprintf(str, size - 1, format, ap);
}

int base_port_MSVC_snprintf(char *str, size_t size, const char *format, ...) {
  va_list ap;
  va_start(ap, format);
  const int r = base_port_MSVC_vsnprintf(str, size, format, ap);
  va_end(ap);
  return r;
}

#endif  // _MSC_VER

}  // namespace googleapis
