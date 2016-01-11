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

// Copied over from code written mostly by jenny@ for util/hash/hash.h
//
// DEPRECATED(jyrki): Do not use these in new code.
// Alternative approaches are (in preference order):
// 1) Build case-dependent systems instead.
// 2) Normalize your keys before hashing instead of changing how hashing works.
//
// Often, the code should run with unicode for handling case (i18n/utf/...)
// The code in this file supports only 7-bit ASCII case.

#ifndef UTIL_HASH_CASE_INSENSITIVE_HASH_H_
#define UTIL_HASH_CASE_INSENSITIVE_HASH_H_

#include <stddef.h>
#ifndef _MSC_VER
#include <strings.h>
#endif

#include <string>
using std::string;

#include "googleapis/base/integral_types.h"
#include "googleapis/base/macros.h"
#include "googleapis/base/port.h"
#include "googleapis/strings/ascii_ctype.h"
namespace googleapis {

// Functors for hashing c-strings with case-insensitive semantics.
struct CStringCaseHash {
  size_t operator()(const char *str) const {
    unsigned long hash_val = 0;
    while (*str) {
      hash_val = 5*hash_val + ascii_tolower(*str);
      str++;
    }
    return (size_t)hash_val;
  }
};

struct CStringCaseEqual {
  bool operator()(const char *str1, const char *str2) const {
    return !strcasecmp(str1, str2);
  }
};

// These functors, in addition to being case-insensitive, ignore all
// non-alphanumeric characters.  This is useful when we want all variants of
// a string -- where variants can differ in puncutation and whitespace -- to
// map to the same value.
struct CStringAlnumCaseHash {
  size_t operator()(const char *str) const {
    unsigned long hash_val = 0;
    while (*str) {
      if (ascii_isalnum(*str)) {
        hash_val = 5*hash_val + ascii_tolower(*str);
      }
      str++;
    }
    return (size_t)hash_val;
  }
};

struct CStringAlnumCaseEqual {
  bool operator()(const char *str1, const char *str2) const {
    while (true) {
      // Skip until each pointer is pointing to an alphanumeric char or '\0'
      while (!ascii_isalnum(*str1) && (*str1 != '\0')) {
        str1++;
      }
      while (!ascii_isalnum(*str2) && (*str2 != '\0')) {
        str2++;
      }
      if (ascii_tolower(*str1) != ascii_tolower(*str2)) {
        return false;       // mismatch on alphanumeric char or '\0'
      }
      if (*str1 == '\0') {  // in which case *str2 must be '\0' as well
        return true;        // reached '\0' in both strings without mismatch
      }
      str1++;
      str2++;
    }
  }
};

// Functors for hashing strings with case-insensitive semantics.
// Note that this hash function uses CStringCaseHash, and is different from
// hash<string> even on the same lowercase strings.
// A common use is in hashtables:
//     hash_map<string, int, StringCaseHash, StringCaseEqualTo> ht;
struct StringCaseHash {
  size_t operator()(const string& str) const {
    return CStringCaseHash()(str.c_str());
  }
};

// We should have called this StringCaseEqual, but the name has been used by
// strings/case.h as a function.
struct StringCaseEqualTo {
  bool operator()(const string& str1, const string& str2) const {
    return !strcasecmp(str1.c_str(), str2.c_str());
  }
};

}  // namespace googleapis
#endif  // UTIL_HASH_CASE_INSENSITIVE_HASH_H_
