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
#ifndef GOOGLEAPIS_ONCE_H_  // NOLINT
#define GOOGLEAPIS_ONCE_H_

#ifndef _MSC_VER
#include <pthread.h>
#endif
#include <glog/logging.h>
#include "googleapis/base/port.h"
namespace googleapis {

#ifndef _MSC_VER

typedef pthread_once_t GoogleOnceType;
#define GOOGLE_ONCE_INIT PTHREAD_ONCE_INIT

inline void GoogleOnceInit(GoogleOnceType* once, void (*initializer)()) {
  CHECK_EQ(0, pthread_once(once, initializer));
}

#else

// I cant get Windows INIT_ONCE to work, even setting _WIN32_WINNT = 0x0600
// So let's implement this ourselves.

typedef int64 GoogleOnceType;
#define GOOGLE_ONCE_INIT 0
void GoogleOnceInit(GoogleOnceType* once, void (*initializer)());

#endif

}  // namespace googleapis
#endif  // GOOGLEAPIS_ONCE_H_  NOLINT
