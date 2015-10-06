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
// Return the path to the program executable.

#ifndef GOOGLEAPIS_UTIL_PROGRAM_PATH_H_
#define GOOGLEAPIS_UTIL_PROGRAM_PATH_H_

#include <string>
namespace googleapis {
using std::string;

namespace client {

std::string GetCurrentProgramFilenamePath();

}  // namespace client

}  // namespace googleapis
#endif  // GOOGLEAPIS_UTIL_PROGRAM_PATH_H_
