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
#include "googleapis/base/mutex.h"
#include "googleapis/base/once.h"

namespace googleapis {

#ifdef _MSC_VER
void GoogleOnceInit(GoogleOnceType* once, void (*initializer)()) {
  static Mutex mutex_(base::LINKER_INITIALIZED);
  MutexLock l(&mutex_);
  if (!*once) {
    *once = true;
    initializer();
  }
}
#endif

}  // namespace googleapis
