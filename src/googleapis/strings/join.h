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

#ifndef GOOGLEAPIS_STRINGS_JOIN_H_
#define GOOGLEAPIS_STRINGS_JOIN_H_

#include <string>
using std::string;
#include "googleapis/strings/strcat.h"
#include "googleapis/strings/stringpiece.h"
namespace googleapis {

namespace strings {

// ----------------------------------------------------------------------
// Join()
//    This merges a container of any type supported by StrAppend() with delim
//    inserted as separators between components.  This is essentially a
//    templatized version of JoinUsingToBuffer().
//
// JoinElementsIterator()
//    Same as JoinElements(), except that the input elements are specified
//    with a pair of forward iterators.
// ----------------------------------------------------------------------

template <class ITERATOR>
void JoinElementsIterator(ITERATOR first,
                          ITERATOR last,
                          StringPiece delim,
                          string* result) {
  result->clear();
  for (ITERATOR it = first; it != last; ++it) {
    if (it != first) {
      StrAppend(result, delim);
    }
    StrAppend(result, *it);
  }
}

template <class ITERATOR>
string JoinElementsIterator(ITERATOR first,
                            ITERATOR last,
                            StringPiece delim) {
  string result;
  JoinElementsIterator(first, last, delim, &result);
  return result;
}

template <class CONTAINER>
inline void Join(const CONTAINER& components,
                 StringPiece delim,
                 string* result) {
  JoinElementsIterator(components.begin(), components.end(), delim, result);
}

template <class CONTAINER>
inline string Join(const CONTAINER& components, StringPiece delim) {
  string result;
  Join(components, delim, &result);
  return result;
}

}  // namespace strings
}  // namespace googleapis
#endif  // GOOGLEAPIS_STRINGS_JOIN_H_
