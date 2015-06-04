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

#include "googleapis/util/status.h"

namespace googleapis {

#define CASE(code)  case googleapis::util::error::code:  return #code
static std::string CodeToString(googleapis::util::error::Code code) {
  switch (code) {
    CASE(OK);
    CASE(CANCELLED);
    CASE(INVALID_ARGUMENT);
    CASE(DEADLINE_EXCEEDED);
    CASE(NOT_FOUND);
    CASE(ALREADY_EXISTS);
    CASE(PERMISSION_DENIED);
    CASE(RESOURCE_EXHAUSTED);
    CASE(FAILED_PRECONDITION);
    CASE(ABORTED);
    CASE(OUT_OF_RANGE);
    CASE(UNIMPLEMENTED);
    CASE(INTERNAL);
    CASE(UNAVAILABLE);
    CASE(DATA_LOSS);
    default:
      std::string result("Error #");
      result.append(std::to_string(code));
      return result;
  }
  // not reached
}
#undef CASE

namespace util {

std::string Status::ToString() const {
  std::string result = CodeToString(code_);
  if (!msg_.empty()) {
    result.append(": ");
    result.append(msg_);
  }

  return result;
}

}  // namespace util

}  // namespace googleapis
