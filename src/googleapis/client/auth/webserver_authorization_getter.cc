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


#include <ostream>  // NOLINT
#include <string>
using std::string;
#include "googleapis/client/auth/oauth2_authorization.h"
#include "googleapis/client/auth/webserver_authorization_getter.h"
#include "googleapis/client/transport/http_types.h"
#include "googleapis/client/util/status.h"
#include "googleapis/base/callback.h"
#include "googleapis/base/integral_types.h"
#include <glog/logging.h>
#include "googleapis/base/mutex.h"
#include "googleapis/base/time.h"
#include "googleapis/strings/strcat.h"
#include "googleapis/strings/util.h"

namespace googleapis {

namespace client {

WebServerAuthorizationCodeGetter::WebServerAuthorizationCodeGetter(
    AskCallback* callback)
    : timeout_ms_(60 * 1000), ask_callback_(callback) {
  ask_callback_->CheckIsRepeatable();
}

WebServerAuthorizationCodeGetter::~WebServerAuthorizationCodeGetter() {
}

// static
util::Status WebServerAuthorizationCodeGetter::PromptWithOstream(
     std::ostream* ostream, const string& prompt, const string& url) {
  string display = prompt;
  GlobalReplaceSubstring("$URL", url, &display);
  *ostream << display;
  return StatusOk();
}

// static
util::Status WebServerAuthorizationCodeGetter::PromptWithCommand(
     const string& program, const string& args, const string& url) {
  string real_args = args;
  GlobalReplaceSubstring("$URL", url, &real_args);

  string cmd = StrCat(program, " ", real_args, ">& /dev/null&");
  VLOG(1) << "Running " << cmd;
  int err = system(cmd.c_str());
  if (err == 0) return StatusOk();
  return StatusUnknown(StrCat("Failed with ", err));
}

util::Status WebServerAuthorizationCodeGetter::AskForAuthorization(
     const string& url) {
  return ask_callback_->Run(url);
}

OAuth2AuthorizationFlow::AuthorizationCodeCallback*
WebServerAuthorizationCodeGetter::MakeAuthorizationCodeCallback(
    OAuth2AuthorizationFlow* flow) {
  return NewPermanentCallback(
      this, &WebServerAuthorizationCodeGetter::PromptForAuthorizationCode,
      flow);
}

util::Status WebServerAuthorizationCodeGetter::PromptForAuthorizationCode(
      OAuth2AuthorizationFlow* flow,
      const OAuth2RequestOptions& options,
      string* authorization_code) {
  string url = flow->GenerateAuthorizationCodeRequestUrlWithOptions(options);
  {
    MutexLock l(&mutex_);
    authorization_code_.clear();
  }
  googleapis::util::Status status = AskForAuthorization(url);
  if (status.ok()) {
    MutexLock l(&mutex_);
    if (authorization_code_.empty()) {
      authorization_status_ =
          StatusDeadlineExceeded("Did not receive authorization in time");
      authorization_condvar_.WaitWithTimeout(&mutex_,
                                             base::Milliseconds(timeout_ms_));
    }
    status = authorization_status_;
    *authorization_code = authorization_code_;
  }
  return status;
}

util::Status WebServerAuthorizationCodeGetter::ReceiveAuthorizationCode(
    WebServerRequest* request) {
  string code;
  string error;
  const ParsedUrl& parsed_url = request->parsed_url();
  bool have_code = parsed_url.GetQueryParameter("code", &code);
  bool have_error = parsed_url.GetQueryParameter("error", &error);
  googleapis::util::Status status;
  if (have_error) {
    status = StatusUnknown(StrCat("Did not authorize: ", error));
  }

  {
    // When we get the anturhozation code, we're in a different
    // thead than the one waiting for the code. So we're going to
    // pass back the code and status in instance variables and signal
    // the other thread that we're ready.
    MutexLock l(&mutex_);
    authorization_status_ = status;
    if (have_code) {
      authorization_code_ = code;
    }
    authorization_condvar_.Signal();
  }

  // While the application is continuing in another thread, we'll
  // confirm back to the OAuth 2.0 webserver that we've received the message.
  string result_body;
  int http_code;
  if (have_code) {
    result_body = "Thanks!";
    http_code = HttpStatusCode::OK;
  } else {
    result_body = "No authorization code.";
    http_code = HttpStatusCode::NOT_FOUND;
  }
  string html = StrCat(
      "<html><body><p>", result_body,
      "</p><p>"
      "You can close this browser now."
      "</p></body></html>");
  return request->response()->SendHtml(http_code, html);
}

void WebServerAuthorizationCodeGetter::AddReceiveAuthorizationCodeUrlPath(
    const string& path, AbstractWebServer* httpd) {
  AbstractWebServer::PathHandler* path_handler =
      NewPermanentCallback(
          this, &WebServerAuthorizationCodeGetter::ReceiveAuthorizationCode);
  httpd->AddPathHandler(path, path_handler);
}

}  // namespace client

}  // namespace googleapis
