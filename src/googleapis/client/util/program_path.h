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

//
// Utility methods for finding the path to the executable.

#ifndef GOOGLEAPIS_UTIL_PROGRAM_PATH_H_
#define GOOGLEAPIS_UTIL_PROGRAM_PATH_H_

#include <string>
namespace googleapis {
using std::string;

namespace client {

std::string GetCurrentProgramFilenamePath();

/*
 * Returns the part of the path after the final "/".  If there is no
 * "/" in the path, the result is the same as the input.
 */
std::string Basename(const std::string& path);

/*
 * Returns the part of the path up through the final "/".  If there is no
 * "/" in the path, the result is an empty string.
 */
std::string StripBasename(const std::string& path);

/*
 * Returns the default application name assumed for this process.
 *
 * The default name will be the filename of the program that is curently
 * running (without other path elements).
 */
std::string DetermineDefaultApplicationName();

}  // namespace client

}  // namespace googleapis
#endif  // GOOGLEAPIS_UTIL_PROGRAM_PATH_H_
