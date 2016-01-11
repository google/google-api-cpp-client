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


#include <string>
using std::string;
#include "samples/abstract_webserver_login_flow.h"
#include "googleapis/client/auth/oauth2_authorization.h"
#include "googleapis/client/auth/oauth2_pending_authorizations.h"
#include "googleapis/client/transport/http_request.h"
#include "googleapis/client/transport/http_types.h"
#include "googleapis/client/util/status.h"
#include "googleapis/base/callback.h"
#include <glog/logging.h>
#include "googleapis/strings/strcat.h"
#include "googleapis/util/hash.h"

namespace googleapis {

using client::WebServerRequest;
using client::OAuth2AuthorizationFlow;
using client::OAuth2PendingAuthorizations;
using client::OAuth2RequestOptions;
using client::OAuth2Credential;
using client::ParsedUrl;
using client::StatusUnknown;

namespace sample {

AbstractWebServerLoginFlow::AbstractWebServerLoginFlow(
    const string& cookie_name,
    const string& redirect_name,
    OAuth2AuthorizationFlow* flow)
    : AbstractLoginFlow(cookie_name, redirect_name, flow) {
  pending_.reset(new OAuth2PendingAuthorizations<PendingAuthorizationHandler>);
}

AbstractWebServerLoginFlow::~AbstractWebServerLoginFlow() {
}


util::Status AbstractWebServerLoginFlow::DoInitiateAuthorizationFlow(
    WebServerRequest* request, const string& redirect_url) {
  string cookie_id = GetCookieId(request);
  string want_url;
  if (redirect_url.empty()) {
    want_url = request->parsed_url().url();
  } else {
    want_url = redirect_url;
  }
  VLOG(1) << "No credential for cookie=" << cookie_id
          << " so save " << want_url << " while we ask";

  OAuth2RequestOptions options;
  string authorize_url =
      flow()->GenerateAuthorizationCodeRequestUrlWithOptions(options);
  int key = pending_->AddAuthorizationCodeHandler(
      NewCallback(this, &AbstractWebServerLoginFlow::ReceiveAuthorizationCode,
                  cookie_id, want_url));
  authorize_url.append("&state=%x");
  char tmp[50];
  snprintf(tmp, sizeof(tmp), "%x", key);
  authorize_url.append(tmp);

  VLOG(1)
      << "Redirecting cookie=" << cookie_id << " to authorize";
  return RedirectToUrl(authorize_url, cookie_id, request);
}

util::Status AbstractWebServerLoginFlow::ReceiveAuthorizationCode(
    const string& cookie_id, const string& want_url,
    WebServerRequest* request) {
  string error;
  string code;
  const ParsedUrl& parsed_url = request->parsed_url();
  bool have_code = parsed_url.GetQueryParameter("code", &code);
  bool have_error = parsed_url.GetQueryParameter("error", &error);
  googleapis::util::Status status;
  if (have_error) {
    status = StatusUnknown(StrCat("Did not authorize: ", error));
  } else if (!have_code) {
    status = StatusUnknown("Missing authorization code");
  }

  OAuth2Credential* new_credential = NULL;
  if (status.ok()) {
    LOG(INFO) << "Received AuthorizationCode for cookie=" << cookie_id;
    OAuth2RequestOptions options;
    std::unique_ptr<OAuth2Credential> credential(flow()->NewCredential());
    status = flow()->PerformExchangeAuthorizationCode(
        code, options, credential.get());
    if (status.ok()) {
      LOG(INFO) << "Got credential for cookie=" << cookie_id;
      new_credential = credential.release();
    } else {
      LOG(INFO) << "Failed to get credential for cookie=" << cookie_id;
    }
  }

  DoReceiveCredentialForCookieId(cookie_id, status, new_credential);

  if (!want_url.empty()) {
    LOG(INFO) << "Restoring continuation for cookie=" << cookie_id;
    if (status.ok()) {
      return RedirectToUrl(want_url, cookie_id, request);
    } else {
      return DoRespondWithLoginErrorPage(cookie_id, status, request);
    }
  } else {
    return DoRespondWithWelcomePage(cookie_id, request);
  }
}

// This callback is used to resolve the requests from the OAuth 2.0 server
// that gives us the authentication codes (or responses) that we asked for.
util::Status AbstractWebServerLoginFlow::DoHandleAccessTokenUrl(
     WebServerRequest* request) {
  string state;
  request->parsed_url().GetQueryParameter("state", &state);
  int32 key;
  bool valid = safe_strto32_base(state, &key, 16);
  PendingAuthorizationHandler* handler =
    valid ? pending_->FindAndRemoveHandlerForKey(key) : NULL;
  if (handler == NULL) {
    LOG(INFO) << "Got unexpected authorization code";
    string result_body = StrCat("Unexpected state=", state);
    return request->response()->SendReply(
        client::HttpRequest::ContentType_TEXT,
        client::HttpStatusCode::NOT_FOUND, result_body);
  }
  return handler->Run(request);
}

}  // namespace sample

}  // namespace googleapis
