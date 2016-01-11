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


#ifndef GOOGLEAPIS_AUTH_WEBSERVER_AUTHORIZATION_GETTER_H_
#define GOOGLEAPIS_AUTH_WEBSERVER_AUTHORIZATION_GETTER_H_
#include <memory>
#include <ostream>  // NOLINT
#include <string>
using std::string;

#include "googleapis/client/auth/oauth2_authorization.h"
#include "googleapis/client/util/abstract_webserver.h"
#include "googleapis/client/util/status.h"
#include "googleapis/base/callback.h"
#include "googleapis/base/integral_types.h"
#include "googleapis/base/macros.h"
#include "googleapis/base/mutex.h"
#include "googleapis/base/thread_annotations.h"
namespace googleapis {

namespace client {

/*
 * An adapter to use webserver with OAuth2AuthorizationFlows.
 * @ingroup AuthSupportOAuth2
 *
 * This class will likely change significantly or go away in a future release.
 *
 * It is here to support samples, experimentation, and testing
 * OAuth2 web flows.
 */
class WebServerAuthorizationCodeGetter {
 public:
  /*
   * Callback used to prompt for authorization.
   * Receiving authorization will happen through a web server handler.
   */
  typedef ResultCallback1< googleapis::util::Status, const string& > AskCallback;

  /*
   * Standard constructor.
   * @param[in] ask_callback Must be a non-NULL repeatable callback.
   *                         takes ownership.
   */
  explicit WebServerAuthorizationCodeGetter(AskCallback* ask_callback);

  /*
   * Standard destructor.
   */
  virtual ~WebServerAuthorizationCodeGetter();

  /*
   * How long we'll wait for authorization.
   *
   * @returns Time in milliseconds.
   */
  int64 timeout_ms() const       { return timeout_ms_; }

  /*
   * Set how long we'll weait.
   *
   * @param[in] ms Time in milliseconds.
   */
  void set_timeout_ms(int64 ms)  { timeout_ms_ = ms; }

  /*
   * Gets the ask callback.
   *
   * @return reference. This instance retains ownership.
   */
  AskCallback* ask_callback() { return ask_callback_.get(); }

  /*
   * Returns a repeatable callback for flow to get an authorization code.
   *
   * @param[in] flow A reference to the flow is used to generate urls.
   * @return Ownershp is apssed back to the caller.
   */
  OAuth2AuthorizationFlow::AuthorizationCodeCallback*
    MakeAuthorizationCodeCallback(OAuth2AuthorizationFlow* flow);

  /*
   * @param[in] path The path that uri_redirects are expected on.
   * @param[in] httpd The webserver to process the redirects wtih
   */
  virtual void  AddReceiveAuthorizationCodeUrlPath(
      const string& path, AbstractWebServer* httpd);

  virtual googleapis::util::Status PromptForAuthorizationCode(
      OAuth2AuthorizationFlow* flow,
      const OAuth2RequestOptions& options,
      string* authorization_code);

  /*
   * A suitable function for an asker that execute a command (e.g. a browser).
   */
  static googleapis::util::Status PromptWithCommand(
      const string& program, const string& args, const string& url);

  /*
   * A suitable function for an asker that prompts a console.
   */
  static googleapis::util::Status PromptWithOstream(
       std::ostream* ostream, const string& prompt, const string& url);

 protected:
  virtual googleapis::util::Status AskForAuthorization(const string& url);

 private:
  int64 timeout_ms_;
  std::unique_ptr<AskCallback> ask_callback_;
  Mutex mutex_;
  CondVar authorization_condvar_        GUARDED_BY(mutex_);
  string authorization_code_            GUARDED_BY(mutex_);
  googleapis::util::Status authorization_status_  GUARDED_BY(mutex_);

  googleapis::util::Status ReceiveAuthorizationCode(WebServerRequest* request);

  DISALLOW_COPY_AND_ASSIGN(WebServerAuthorizationCodeGetter);
};

}  // namespace client

}  // namespace googleapis
#endif  // GOOGLEAPIS_AUTH_WEBSERVER_AUTHORIZATION_GETTER_H_
