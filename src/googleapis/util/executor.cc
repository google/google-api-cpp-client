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
#include "googleapis/base/callback.h"
#include "googleapis/base/mutex.h"
#include "googleapis/base/once.h"
#include "googleapis/util/executor.h"

namespace googleapis {

namespace {
using thread::Executor;

static Executor* default_executor_ = NULL;
static Executor* global_inline_executor_ = NULL;

GoogleOnceType module_init_ = GOOGLE_ONCE_INIT;
Mutex module_mutex_(base::LINKER_INITIALIZED);

class InlineExecutor : public Executor {
 public:
  virtual ~InlineExecutor() {}

  virtual void Add(Closure* closure) {
    closure->Run();
  }
  virtual bool TryAdd(Closure* closure) {
    closure->Run();
    return true;
  }
  virtual int num_pending_closures() const {
    return 0;
  }
};

void InitModule() {
  global_inline_executor_ = new InlineExecutor;
  default_executor_ = global_inline_executor_;
}

}  // anonymous namespace

namespace thread {

Executor::Executor() {}
Executor::~Executor() {}

// static
Executor* Executor::DefaultExecutor() {
  GoogleOnceInit(&module_init_, InitModule);
  return default_executor_;
}

// static
void Executor::SetDefaultExecutor(Executor* executor) {
  GoogleOnceInit(&module_init_, InitModule);
  MutexLock l(&module_mutex_);
  default_executor_ = executor;
}

// static
Executor* NewInlineExecutor() {
  return new InlineExecutor;
}

// static
Executor* SingletonInlineExecutor() {
  GoogleOnceInit(&module_init_, InitModule);
  return global_inline_executor_;
}

}  // namespace thread

}  // namespace googleapis
