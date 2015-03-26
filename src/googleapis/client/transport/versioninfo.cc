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


#include <string>
using std::string;

#if !defined(WIN32) && !defined(_WIN32)
# include <sys/utsname.h>
#endif
#include "googleapis/config.h"
#include "googleapis/client/transport/versioninfo.h"
#include "googleapis/base/port.h"
#include "googleapis/strings/strcat.h"

namespace googleapis {

namespace client {
using std::string;

const int VersionInfo::kMajorVersionNumber = googleapis_VERSION_MAJOR;
const int VersionInfo::kMinorVersionNumber = googleapis_VERSION_MINOR;
const int VersionInfo::kPatchVersionNumber = googleapis_VERSION_PATCH;
const char VersionInfo::kVersionDecorator[] = googleapis_VERSION_DECORATOR;

const string VersionInfo::GetVersionString() {
  // If patch version is 0 then just ommit it.
  // So 1.0.0 is just 1.0
  string patch_version =
      kPatchVersionNumber == 0 ? "" : StrCat(".", kPatchVersionNumber);
  const char* decorator_sep = (kVersionDecorator[0] ? "-" : "");
  return StrCat(kMajorVersionNumber, ".", kMinorVersionNumber,
                patch_version, decorator_sep, kVersionDecorator);
}

const string VersionInfo::GetPlatformString() {
#if !defined(WIN32) && !defined(_WIN32)
  struct utsname uts;
  if (uname(&uts)) return "Unix";
  return StrCat(uts.sysname, "/", uts.release);
#else
  OSVERSIONINFOEX info;
  info.dwOSVersionInfoSize = sizeof(info);
  if (!GetVersionEx((LPOSVERSIONINFO)&info)) return "Windows";
  const char* type =
      info.wProductType == VER_NT_WORKSTATION
      ? "W"
      : info.wProductType == VER_NT_SERVER
      ? "S"
      : info.wProductType == VER_NT_DOMAIN_CONTROLLER
      ? "C"
      : "U";
  return StrCat("Windows/", info.dwMajorVersion, ".",
                info.dwMinorVersion, type);
#endif
}

}  // namespace client

}  // namespace googleapis
