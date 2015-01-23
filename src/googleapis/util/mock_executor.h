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


#ifndef THREAD_MOCK_EXECUTOR_H_
#define THREAD_MOCK_EXECUTOR_H_

#include <gmock/gmock.h>
#include "googleapis/util/executor.h"
namespace googleapis {

namespace thread {

class MockExecutor : public Executor {
 public:
  MOCK_METHOD1(Add, void(Closure* callback));
  MOCK_METHOD1(TryAdd, bool(Closure* closure));
  MOCK_METHOD1(AddIfReadyToRun, bool(Closure* closure));
  MOCK_METHOD2(AddAfter, void(int ms, Closure *closure));
  MOCK_CONST_METHOD0(num_pending_closures, int());
};

}  // namespace thread

}  // namespace googleapis
#endif  // THREAD_MOCK_EXECUTOR_H_
