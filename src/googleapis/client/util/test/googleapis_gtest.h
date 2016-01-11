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
// Convience helpers for GTest.
// These require linking against googleapis_gtest_main instead of gtest_main

#ifndef GOOGLEAPIS_UTIL_GOOGLEAPIS_GTEST_H_
#define GOOGLEAPIS_UTIL_GOOGLEAPIS_GTEST_H_

#include <gtest/gtest.h>
namespace googleapis {

namespace client {

/*
 * Returns the temporary directory created for running the current test.
 *
 * This function is only avalable when using googleapis_test_main for tests.
 */
string GetTestingTempDir();

}  // namespace client

}  // namespace googleapis
#endif  // GOOGLEAPIS_UTIL_GOOGLEAPIS_GTEST_H_
