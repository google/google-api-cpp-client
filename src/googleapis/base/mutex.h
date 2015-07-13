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
#ifndef GOOGLEAPIS_BASE_MUTEX_H_
#define GOOGLEAPIS_BASE_MUTEX_H_
#define GOOGLEAPIS_MUTEX_H_

#if defined(_MSC_VER)
#include "googleapis/base/windows_compatability.h"
# include <condition_variable>  // NOLINT
#else
# include <errno.h>
# include <pthread.h>
# include <sys/time.h>
#endif
#include <stdint.h>

#include "googleapis/base/integral_types.h"
#include "googleapis/base/macros.h"
#include <glog/logging.h>
#include "googleapis/base/thread_annotations.h"
namespace googleapis {

namespace base {

#if !defined(_MSC_VER)
class PThreadCondVar;
class LOCKABLE PThreadMutex {
 public:
  explicit PThreadMutex(base::LinkerInitialized) {
    pthread_mutex_init(&mutex_, NULL);
  }
  PThreadMutex()   { pthread_mutex_init(&mutex_, NULL); }
  ~PThreadMutex()  { pthread_mutex_destroy(&mutex_); }

  void Lock()     { CHECK_EQ(0, pthread_mutex_lock(&mutex_)); }
  void Unlock()   { CHECK_EQ(0, pthread_mutex_unlock(&mutex_)); }

 private:
  friend class PThreadCondVar;
  pthread_mutex_t mutex_;

  DISALLOW_COPY_AND_ASSIGN(PThreadMutex);
};

class PThreadCondVar {
 public:
  PThreadCondVar()  { pthread_cond_init(&cv_, NULL); }
  ~PThreadCondVar() { CHECK_EQ(0, pthread_cond_destroy(&cv_)); }

  void Signal()        { CHECK_EQ(0, pthread_cond_signal(&cv_)); }
  void SignalAll()     { CHECK_EQ(0, pthread_cond_broadcast(&cv_)); }
  void Wait(PThreadMutex* mu) {
    CHECK_EQ(0, pthread_cond_wait(&cv_, &mu->mutex_));
  }
  bool WaitWithTimeout(PThreadMutex* mu, int32_t millis) {
    struct timeval tv;
    struct timespec ts;
    gettimeofday(&tv, NULL);
    ts.tv_sec = tv.tv_sec + millis / 1000;
    ts.tv_nsec = millis % 1000;
    int result = pthread_cond_timedwait(&cv_, &mu->mutex_, &ts);
    if (!result) return true;

    CHECK_EQ(ETIMEDOUT, result);
    return false;
  }

 private:
  pthread_cond_t cv_;
  DISALLOW_COPY_AND_ASSIGN(PThreadCondVar);
};

typedef PThreadCondVar CondVar;
typedef PThreadMutex Mutex;
#else
class MsvcCondVar;
class MsvcMutex {
 public:
  explicit MsvcMutex(base::LinkerInitialized ignore) {
    InitializeCriticalSection(&mutex_);
  }
  MsvcMutex()   {
    InitializeCriticalSection(&mutex_);
  }
  ~MsvcMutex()  {}

  void Lock() {
    EnterCriticalSection(&mutex_);
  }
  void Unlock() {
    LeaveCriticalSection(&mutex_);
  }

 private:
  friend class MsvcCondVar;
  CRITICAL_SECTION mutex_;

  DISALLOW_COPY_AND_ASSIGN(MsvcMutex);
};

class MsvcCondVar {
 public:
  MsvcCondVar()  { InitializeConditionVariable(&cv_); }
  ~MsvcCondVar() {}

  void Signal()        { WakeConditionVariable(&cv_); }
  void SignalAll()     { WakeAllConditionVariable(&cv_); }
  void Wait(MsvcMutex* mu) {
    SleepConditionVariableCS(&cv_, &mu->mutex_, INFINITE);
  }
  bool WaitWithTimeout(MsvcMutex* mu, int64 millis) {
    return SleepConditionVariableCS(&cv_, &mu->mutex_, millis);
  }

 private:
  CONDITION_VARIABLE cv_;
  DISALLOW_COPY_AND_ASSIGN(MsvcCondVar);
};

typedef MsvcCondVar CondVar;
typedef MsvcMutex Mutex;
#endif

class MutexLock {
 public:
  explicit MutexLock(Mutex* mutex) : mutex_(mutex) { mutex_->Lock(); }
  ~MutexLock() { mutex_->Unlock(); }

 private:
  Mutex* mutex_;

  DISALLOW_COPY_AND_ASSIGN(MutexLock);
};

}  // namespace base


using base::CondVar;
using base::Mutex;
using base::MutexLock;

}  // namespace googleapis
#endif  // GOOGLEAPIS_BASE_MUTEX_H_
