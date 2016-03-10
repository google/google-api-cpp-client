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


#include "googleapis/strings/split.h"

#include <vector>

#include "googleapis/strings/stringpiece.h"

namespace googleapis {

namespace strings {

std::vector<StringPiece> Split(const StringPiece& source,
                               const StringPiece& delim) {
  std::vector<StringPiece> result;

  if (source.empty()) {
    return result;
  }

  if (delim.empty()) {
    // Split on every char.
    for (const char* pc = source.data();
         pc < source.data() + source.size();
         ++pc) {
      result.push_back(StringPiece(pc, 1));
    }
    return result;
  }

  int offset = 0;
  for (int next = 0; offset < source.size(); offset = next + delim.size()) {
    next = source.find(delim, offset);
    if (next == StringPiece::npos) {
      next = source.size();
    }
    result.push_back(source.substr(offset, next - offset));
  }

  if (offset == source.size()) {
    // Source terminated exactly on a delim, so push an empty dude to end.
    // Keep the pointer monotonically increasing for kicks.
    result.push_back(source.substr(source.size() - 1, 0));
  }

  return result;
}

}  // namespace strings

}  // namespace googleapis
