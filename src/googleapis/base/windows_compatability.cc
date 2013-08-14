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
// TODO(ewiseblatt): 20130723
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
#include <string>
using std::string;
#include <glog/logging.h>
#include "googleapis/base/port.h"
#include "googleapis/base/scoped_ptr.h"
#include "googleapis/base/windows_compatability.h"
#include "googleapis/strings/util.h"

namespace googleapis {

#ifdef _MSC_VER

string FromWindowsPath(const string& path) {
  return StringReplace(path, "\\", "/", true);
}

string ToWindowsPath(const string& str) {
  return StringReplace(str, "/", "\\", true);
}

const WCHAR* ToWindowsWideString(const string& from, string* to) {
  int buffer_size = from.size() + 1;
  scoped_ptr<WCHAR[]> wide_string(new WCHAR[buffer_size]);

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
  scoped_ptr<char[]> data(new char[2 * data_size + 2]);
  int len = WideCharToMultiByte(CP_UTF8, 0,
                                windows, data_size,
                                data.get(), 2 * data_size + 1,
                                NULL, NULL);
  CHECK_NE(0, len) << "Failed with " << GetLastError();
  return string(data.get(), len);
#endif
}

#endif  // _MSC_VER

} // namespace googleapis