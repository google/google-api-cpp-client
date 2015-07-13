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
/*
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

// Refactored from contributions of various authors in strings/strutil.h
//
// This file contains string processing functions related to
// uppercase, lowercase, etc.
//
// These functions are for ASCII only. If you need to process UTF8 strings,
// take a look at files in i18n/utf8.

#ifndef GOOGLEAPIS_STRINGS_CASE_H_
#define GOOGLEAPIS_STRINGS_CASE_H_

#include <string.h>
#ifndef _MSC_VER
#ifndef _MSC_VER
#include <strings.h>
#endif
#else
#include "googleapis/base/windows_compatability.h"
#endif
#include <functional>
using std::binary_function;
using std::less;
#include <string>
namespace googleapis {
using std::string;

// These are function objects (this is the kind of thing that STL
// uses).  This provides a comparison function appropriate for
// char *s.  A common use is in hashtables:
//
//   hash_map<const char*, int, CStringCaseHash, strcaseeq> ht;
//
// Note that your hash function must also be case-insensitive.  CStringCaseHash
// is suitable, and defined in util/hash/case_insensitive_hash.h
//
// Case-insensitive hashing is not recommended for new code. See
// util/hash/case_insensitive_hash.h
struct strcaseeq : public std::binary_function<const char*, const char*, bool> {
  bool operator()(const char* s1, const char* s2) const {
    return ((s1 == 0 && s2 == 0) ||
            (s1 && s2 && strcasecmp(s1, s2) == 0));
  }
};

// For strcaselt, sorting would put NULL string last.
struct strcaselt : public std::binary_function<const char*, const char*, bool> {
  bool operator()(const char* s1, const char* s2) const {
    return (s1 != s2) && (s2 == 0 || (s1 != 0 && strcasecmp(s1, s2) < 0));
  }
};

// ----------------------------------------------------------------------
// GetCapitalization()
//    Return a value indicating whether the string is entirely
//    lowercase, entirely uppercase, first letter uppercase, or
//    mixed case.  As returned by ascii_islower() and ascii_isupper().
// ----------------------------------------------------------------------

enum CapsType {
  CAPS_LOWER,
  CAPS_UPPER,
  CAPS_FIRST,
  CAPS_MIXED,
  CAPS_NOALPHA,
};

CapsType GetCapitalization(const char* s);

// Case-insensitive string comparison, uses C/POSIX locale.
// Returns:
//    less than 0:    if s1 < s2
//    equal to 0:     if s1 == s2
//    greater than 0: if s1 > s2
inline int StringCaseCompare(const std::string& s1, const std::string& s2) {
  return strcasecmp(s1.c_str(), s2.c_str());
}

// Returns true if the two strings are equal, case-insensitively speaking.
// Uses C/POSIX locale.
inline bool StringCaseEqual(const std::string& s1, const std::string& s2) {
  return strcasecmp(s1.c_str(), s2.c_str()) == 0;
}

// Case-insensitive less-than string comparison, uses C/POSIX locale.
// Useful as a template parameter for STL set/map of strings, if uniqueness of
// keys is case-insensitive.
struct StringCaseLess {
  bool operator()(const std::string& s1, const std::string& s2) const {
    return strcasecmp(s1.c_str(), s2.c_str()) < 0;
  }
};

// ----------------------------------------------------------------------
// LowerString()
// LowerStringToBuf()
//    Convert the characters in "s" to lowercase.
//    Works only with ASCII strings; for UTF8, see ToLower in
//    util/utf8/public/unilib.h
//    Changes contents of "s".  LowerStringToBuf copies at most
//    "n" characters (including the terminating '\0')  from "s"
//    to another buffer.
// ----------------------------------------------------------------------

void LowerString(char* s);
void LowerString(std::string* s);
void LowerStringToBuf(const char* s, char* buf, int n);

// ----------------------------------------------------------------------
// UpperString()
// UpperStringToBuf()
//    Convert the characters in "s" to uppercase.
//    Works only with ASCII strings; for UTF8, see ToUpper in
//    util/utf8/public/unilib.h
//    UpperString changes "s". UpperStringToBuf copies at most
//    "n" characters (including the terminating '\0')  from "s"
//    to another buffer.
// ----------------------------------------------------------------------

void UpperString(char* s);
void UpperString(std::string* s);
void UpperStringToBuf(const char* s, char* buf, int n);

}  // namespace googleapis
#endif  // GOOGLEAPIS_STRINGS_CASE_H_
