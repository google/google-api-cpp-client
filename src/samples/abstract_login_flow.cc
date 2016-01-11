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
#include "samples/abstract_login_flow.h"
#include "googleapis/client/auth/oauth2_authorization.h"
#include "googleapis/client/transport/http_request.h"
#include "googleapis/client/transport/http_types.h"
#include "googleapis/client/util/abstract_webserver.h"
#include "googleapis/client/util/status.h"
#include "googleapis/base/callback.h"
#include <glog/logging.h>
#include "googleapis/strings/strcat.h"
#include "googleapis/util/hash.h"

namespace googleapis {

using client::AbstractWebServer;
using client::OAuth2AuthorizationFlow;
using client::OAuth2RequestOptions;
using client::OAuth2Credential;
using client::HttpStatusCode;
using client::ParsedUrl;
using client::WebServerRequest;
using client::WebServerResponse;
using client::StatusUnknown;

namespace sample {
AbstractLoginFlow::AbstractLoginFlow(
    const string& cookie_name,
    const string& redirect_name,
    OAuth2AuthorizationFlow* flow)
    : cookie_name_(cookie_name),
      redirect_name_(redirect_name),
      flow_(flow) {
}

AbstractLoginFlow::~AbstractLoginFlow() {
}

void AbstractLoginFlow::AddLoginUrl(
    const string& url, AbstractWebServer* httpd) {
  CHECK(login_url_.empty());
  login_url_ = url;
  httpd->AddPathHandler(
      login_url_,
      NewPermanentCallback(
          this, &AbstractLoginFlow::HandleLoginUrl));
}

void AbstractLoginFlow::AddLogoutUrl(
    const string& url, AbstractWebServer* httpd) {
  CHECK(logout_url_.empty());
  logout_url_ = url;
  httpd->AddPathHandler(
      logout_url_,
      NewPermanentCallback(
          this, &AbstractLoginFlow::HandleLogoutUrl));
}

void AbstractLoginFlow::AddReceiveAccessTokenUrl(
    const string& url, AbstractWebServer* httpd) {
  CHECK(access_token_url_.empty());
  access_token_url_ = url;
  httpd->AddPathHandler(
      access_token_url_,
      NewPermanentCallback(
          this, &AbstractLoginFlow::HandleAccessTokenUrl));
}


util::Status AbstractLoginFlow::ReceiveAuthorizationCode(
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
    std::unique_ptr<OAuth2Credential> credential(flow_->NewCredential());
    status = flow_->PerformExchangeAuthorizationCode(
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

util::Status AbstractLoginFlow::InitiateAuthorizationFlow(
    WebServerRequest* request, const string& redirect_url) {
  return DoInitiateAuthorizationFlow(request, redirect_url);
}

util::Status AbstractLoginFlow::HandleAccessTokenUrl(
     WebServerRequest* request) {
  return DoHandleAccessTokenUrl(request);
}

util::Status AbstractLoginFlow::DoHandleAccessTokenUrl(
     WebServerRequest* request) {
  string cookie_id;
  string access_token;
  string msg;
  string error;
  int http_code;
  const ParsedUrl& parsed_url = request->parsed_url();
  if (!parsed_url.GetQueryParameter("access_token", &access_token)) {
    http_code = HttpStatusCode::BAD_REQUEST;
    msg = "No access_token provided";
  } else if (!parsed_url.GetQueryParameter("state", &cookie_id)
             || cookie_id.empty()) {
    http_code = HttpStatusCode::BAD_REQUEST;
    msg = "No state param";
  } else if (access_token.empty()) {
    http_code = HttpStatusCode::OK;
    msg = "Revoked permissions";
    DoReceiveCredentialForCookieId(
        cookie_id, client::StatusOk(), NULL);
  } else {
    OAuth2Credential* credential = flow_->NewCredential();
    credential->set_access_token(access_token);
    googleapis::util::Status status;
    if (!DoReceiveCredentialForCookieId(
            cookie_id, client::StatusOk(), credential)) {
      msg = "LOGIN";
    } else {
      msg = "Welcome back.";
    }
    http_code = 200;
  }
  return request->response()->SendText(http_code, msg);
}

util::Status AbstractLoginFlow::HandleLoginUrl(WebServerRequest* request) {
  return DoHandleLoginUrl(request);
}

util::Status AbstractLoginFlow::DoHandleLoginUrl(WebServerRequest* request) {
  VLOG(1) << "Handling " << request->parsed_url().url();

  const string& cookie_id = GetCookieId(request);
  OAuth2Credential* credential = DoGetCredentialForCookieId(cookie_id);
  string redirect_url;
  request->parsed_url().GetQueryParameter(redirect_name_, &redirect_url);

  if (credential) {
    if (!redirect_url.empty()) {
      return RedirectToUrl(redirect_url, cookie_id, request);
    }
    if (!credential->access_token().empty()) {
      return DoRespondWithWelcomePage(cookie_id, request);
    }
  }
  if (redirect_url.empty()) {
    return DoRespondWithNotLoggedInPage(cookie_id, request);
  }

  return InitiateAuthorizationFlow(request, redirect_url);
}

util::Status AbstractLoginFlow::HandleLogoutUrl(WebServerRequest* request) {
  return DoHandleLogoutUrl(request);
}

util::Status AbstractLoginFlow::DoHandleLogoutUrl(
     WebServerRequest* request) {
  VLOG(1) << "Handling " << request->parsed_url().url();

  const string& cookie_id = GetCookieId(request);
  OAuth2Credential* credential = DoGetCredentialForCookieId(cookie_id);

  if (!credential) {
    VLOG(1) << "Ignored because there was no known credential to revoke.";
  } else {
    string access_token;
    string refresh_token;
    credential->access_token().AppendTo(&access_token);
    credential->refresh_token().AppendTo(&refresh_token);

    if (access_token.empty() && refresh_token.empty()) {
      VLOG(1) << "Not logged into sample app yet";
    } else {
      googleapis::util::Status status =
          flow_->PerformRevokeToken(refresh_token.empty(), credential);
      VLOG(1) << "Clearing credential for " << cookie_id;
      DoReceiveCredentialForCookieId(
          cookie_id, client::StatusOk(), NULL);
      if (status.ok()) {
        VLOG(1) << "Revoked "
                << (refresh_token.empty() ? "Access" : "Refresh")
                << "Token";
      } else {
        LOG(ERROR) << status.error_message();
      }
    }
  }
  return RedirectToUrl(login_url_, cookie_id, request);
}


// Responds to a request by redirecting to a url.
util::Status AbstractLoginFlow::RedirectToUrl(
    const string& url, const string& cookie_id, WebServerRequest* request) {
  LOG(INFO) << "Redirecting cookie=" << cookie_id << " to " << url;

  WebServerResponse* response = request->response();
  googleapis::util::Status status = response->AddCookie(cookie_name_, cookie_id);
  if (!status.ok()) return status;

  return response->SendRedirect(HttpStatusCode::TEMPORARY_REDIRECT, url);
}

string AbstractLoginFlow::GetCookieId(WebServerRequest* request) {
  string cookie;
  if (!request->GetCookieValue(cookie_name_.c_str(), &cookie)) {
    VLOG(1) << "Missing cookie_id cookie=" << cookie_name_;
  }
  return cookie;
}

}  // namespace sample

}  // namespace googleapis
