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


#ifndef GOOGLEAPIS_AUTH_OAUTH2_PENDING_AUTHORIZATIONS_H_
#define GOOGLEAPIS_AUTH_OAUTH2_PENDING_AUTHORIZATIONS_H_

#include <map>
#include <string>
using std::string;

#include "googleapis/client/util/status.h"
#include "googleapis/base/callback.h"
#include "googleapis/base/macros.h"
#include "googleapis/base/mutex.h"
#include "googleapis/base/thread_annotations.h"
namespace googleapis {

namespace client {

/*
 * Callback used to process authorization codes received
 * from the OAuth 2.0 server.
 * @ingroup AuthSupportOAuth2
 *
 * In practice this is used for the response handling for Authorization
 * Codes where we give a redirect_uri to the OAuth 2.0 server for where
 * to forward the code. The url will be the response with the state parameter
 * used to map back to the original request.
 *
 * In practice you'll probably want to curry additional data parameters
 * with the callback so that you have additional context to the inquiry.
 *
 * @param[in] status If not ok then we're cancelling the request for some
 *                   reason, such as a timeout.
 * @param[in] code The authorization code if ok, otherwise ignored.
 */
typedef Callback2<const googleapis::util::Status&, const string&>
  OAuth2BasicAuthorizationCodeNotificationHandler;

/*
 * Manages callbacks for outstanding authorization code requests.
 *
 * This class is threadsafe.
 */
template<class CALLBACK>
class OAuth2PendingAuthorizations {
 public:
  /*
   * Standard constructor.
   */
  OAuth2PendingAuthorizations() {}

  /*
   * Standard destructor.
   */
  virtual ~OAuth2PendingAuthorizations() {
    for (typename std::map<int, CALLBACK*>::iterator it = map_.begin();
         it != map_.end();
         ++it) {
      CancelCallback(it->second);
    }
  }

  void CancelCallback(CALLBACK* callback) { delete callback; }

  /*
   * Adds a notification handler, returning the state value to associate with.
   *
   * @param[in] handler Will eventually be called exactly once
   *
   * @return value to use for the state query parameter in the url. This
   *         will be used later as the key to retrieve the callback to invoke.
   */
  int AddAuthorizationCodeHandler(CALLBACK* handler) LOCKS_EXCLUDED(mutex_) {
    MutexLock l(&mutex_);
    int key;
    for (key = random(); map_.find(key) != map_.end(); key = random()) {
      // find unused key
    }
    map_.insert(std::make_pair(key, handler));
    return key;
  }

  /*
   * Looks up registered handler for key and removes it if found.
   *
   * This will remove the handler if it was found so it is only returned once.
   *
   * @param[in] key Returned by AddAuthorizationCodeHandler
   * @return The handler added for the key or NULL.
   */
  CALLBACK* FindAndRemoveHandlerForKey(int key) LOCKS_EXCLUDED(mutex_) {
    CALLBACK* handler = NULL;
    MutexLock l(&mutex_);
    typename std::map<int, CALLBACK*>::iterator it = map_.find(key);
    if (it != map_.end()) {
      handler = it->second;
      map_.erase(it);
    }
    return handler;
  }

 private:
  Mutex mutex_;
  std::map<int, CALLBACK*> map_ GUARDED_BY(mutex_);

  DISALLOW_COPY_AND_ASSIGN(OAuth2PendingAuthorizations);
};

}  // namespace client

}  // namespace googleapis
#endif  // GOOGLEAPIS_AUTH_OAUTH2_PENDING_AUTHORIZATIONS_H_
