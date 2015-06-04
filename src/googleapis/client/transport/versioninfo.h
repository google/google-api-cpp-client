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


#ifndef GOOGLEAPIS_TRANSPORT_VERSIONINFO_H_
#define GOOGLEAPIS_TRANSPORT_VERSIONINFO_H_

#include <string>
namespace googleapis {
using std::string;

namespace client {

class VersionInfo {
 public:
  /*
   * The major version number is used for compatability purposes.
   */
  static const int kMajorVersionNumber;

  /*
   * The minor version number is used for incremental fixes and enhancements.
   */
  static const int kMinorVersionNumber;

  /*
   * The patch version number is used to patch releases.
   */
  static const int kPatchVersionNumber;

  /*
   * The version decorator is used to mark unofficial versions. The intent
   * is that this will be empty for official releases but something else
   * for builds from the head, etc.
   */
  static const char kVersionDecorator[];

  /*
   * Returns a string with the complete version string.
   *
   * @return string in the form major.minor-decorator
   */
  static const std::string GetVersionString();

  /*
   * Returns platform that we are running on
   */
  static const std::string GetPlatformString();
};

}  // namespace client

}  // namespace googleapis
#endif  // GOOGLEAPIS_TRANSPORT_VERSIONINFO_H_
