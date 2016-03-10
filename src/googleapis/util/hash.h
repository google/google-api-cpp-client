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

#ifndef UTILS_HASH_HASH_H_  // NOLINT
#define UTILS_HASH_HASH_H_
#include <stddef.h>
#include <unordered_set>

#include <string>
using std::string;
#include <utility>
#include "googleapis/base/port.h"
#ifdef _WIN32
#include "googleapis/base/windows_compatability.h"
#endif
#include "googleapis/base/integral_types.h"
namespace googleapis {

static inline void mix(uint32& a, uint32& b, uint32& c) {  // 32bit version NOLINT
  a -= b; a -= c; a ^= (c >> 13);
  b -= c; b -= a; b ^= (a << 8);
  c -= a; c -= b; c ^= (b >> 13);
  a -= b; a -= c; a ^= (c >> 12);
  b -= c; b -= a; b ^= (a << 16);
  c -= a; c -= b; c ^= (b >> 5);
  a -= b; a -= c; a ^= (c >> 3);
  b -= c; b -= a; b ^= (a << 10);
  c -= a; c -= b; c ^= (b >> 15);
}

static inline void mix(uint64& a, uint64& b, uint64& c) {  // 64bit version NOLINT
  a -= b; a -= c; a ^= (c >> 43);
  b -= c; b -= a; b ^= (a << 9);
  c -= a; c -= b; c ^= (b >> 8);
  a -= b; a -= c; a ^= (c >> 38);
  b -= c; b -= a; b ^= (a << 23);
  c -= a; c -= b; c ^= (b >> 5);
  a -= b; a -= c; a ^= (c >> 35);
  b -= c; b -= a; b ^= (a << 49);
  c -= a; c -= b; c ^= (b >> 11);
  a -= b; a -= c; a ^= (c >> 12);
  b -= c; b -= a; b ^= (a << 18);
  c -= a; c -= b; c ^= (b >> 22);
}

inline uint32 Hash32NumWithSeed(uint32 num, uint32 c) {
  uint32 b = 0x9e3779b9UL;            // the golden ratio; an arbitrary value
  mix(num, b, c);
  return c;
}

inline uint64 Hash64NumWithSeed(uint64 num, uint64 c) {
  uint64 b = GG_ULONGLONG(0xe08c1d668b756f82);   // more of the golden ratio
  mix(num, b, c);
  return c;
}

uint64 Hash64StringWithSeed(const char *s, size_t len, uint64 seed);

// not implemented but referenced by stringpiece
uint32 Hash32StringWithSeedReferenceImplementation(const char *s, size_t len,
                                                   uint32 seed);

inline size_t HashTo32(const char* s, size_t len) {
  return Hash32StringWithSeedReferenceImplementation(s, len, 42);
}

inline size_t HashStringThoroughly(const char* s, size_t len) {
  return HashTo32(s, len);
}

// MOE::strip_line   The rest of this file is a subset of hash.h
// MOE::strip_line   But if it doesnt port, steal guts of protobuf's hash.h

}  // namespace googleapis
#endif  // UTILS_HASH_HASH_H_  NOLINT
