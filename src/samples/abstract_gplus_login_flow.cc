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

//
// This implementation is generally based on
// https://developers.google.com/+/web/signin/#using_the_client-side_flow
//
// If we had a templating engine then we should use it here.
// However for the time being I'm trying to keep dependencies down.
// Since this is not yet part of the core library, I'm not introducing
// a templating engine for it. Instead I tried to use a simple variable
// substitution to make the templated strings more readable, then perform
// string substitution on the variables.

#include <string>
using std::string;
#include "samples/abstract_gplus_login_flow.h"
#include "googleapis/client/auth/oauth2_authorization.h"
#include "googleapis/client/transport/http_types.h"
#include "googleapis/client/util/abstract_webserver.h"
#include "googleapis/client/util/status.h"
#include "googleapis/strings/strcat.h"
#include "googleapis/strings/util.h"

namespace googleapis {

using client::AbstractWebServer;
using client::HttpStatusCode;
using client::OAuth2AuthorizationFlow;
using client::OAuth2Credential;
using client::ParsedUrl;
using client::StatusUnknown;
using client::WebServerRequest;
using client::WebServerResponse;

namespace sample {

AbstractGplusLoginFlow::AbstractGplusLoginFlow(
    const string& cookie_name,
    const string& redirect_name,
    OAuth2AuthorizationFlow* flow)
    : AbstractLoginFlow(cookie_name, redirect_name, flow) {
}

AbstractGplusLoginFlow::~AbstractGplusLoginFlow() {}

// This is pretty much from Step 2 on:
// https://developers.google.com/+/web/signin/#using_the_client-side_flow
string AbstractGplusLoginFlow::GetPrerequisiteHeadHtml() {
  const string kFetchScript =
      "<script type='text/javascript'>\n"
      "(function() {\n"
      "  var po = document.createElement('script');\n"
      "  po.type = 'text/javascript'; po.async = false;\n"  // !!!
      "  po.src = 'https://apis.google.com/js/client:plusone.js';\n"
      "  var s = document.getElementsByTagName('script')[0];\n"
      "  s.parentNode.insertBefore(po, s);\n"
      "})();\n"
      "</script>\n";
  return kFetchScript;
}

// This is pretty much from Step 3 on:
// https://developers.google.com/+/web/signin/#using_the_client-side_flow
//
// This is different in that the button is invisible by default.
// We're also going to make sure the renderer was configured correctly.
// If the renderer is not configured correctly it will render errors instead
// of the button.
string AbstractGplusLoginFlow::GetSigninButtonHtml(bool default_visible) {
  string error;
  if (access_token_url().empty()) {
    error.append("<li>Did not AddPokeUrl.");
  }
  if (client_id_.empty()) {
    error.append("<li>Missing 'client_id' config.");
  }
  if (!error.empty()) {
    return StrCat("<ol>", error, "</ol>");
  }

  string html = StrCat(
      "<span id='signinButton'$VISIBLE>"
      "<span"
      " class='g-signin'"
      " data-callback='signinCallback'"
      " data-clientid='", client_id_, "'"
      " data-cookiepolicy='single_host_origin'"
      " data-requestvisibleactions=''"
      " data-scope='", scopes_, "'>"
      "</span>"
      "</span>");
  string style = default_visible ? "" : " style='display:none'";
  return StringReplace(html, "$VISIBLE", style, false);
}

// This is based on Step 4 from:
// https://developers.google.com/+/web/signin/#using_the_client-side_flow
//
// When we get a login we're going to poke the data into the server.
// We're going to use an additional state parameter so the server can
// correlate the credential with the user since the poke is an unsolicited
// HTTP GET call.
//
// On success we'll execute the success_block parameter after setting the
// credential so it can redirect.
// On failure we'll execute the failure block.
//
// Success an failure will make the button visible and hidden.
string AbstractGplusLoginFlow::GetSigninCallbackJavascriptHtml(
    const string& state,
    const string& immediate_block,
    const string& success_block,
    const string& failure_block) {
  string javascript_template =
      "<script type='text/javascript'>\n"
      "function signinCallback(authResult) {\n"
      "  if (authResult['access_token']) {\n"
      "    document.getElementById('signinButton')"
           ".setAttribute('style', 'display:none');\n"
      "    var url = '$POKE_URL'\n"
      "            + '?state=$STATE'\n"
      "            + '&access_token=' + authResult['access_token']\n"
      "            + '&id_token=' + authResult['id_token'];\n"
      "$MAYBE_LOG_ACCESS_TOKEN_AND_GET_URL"
      "    var xmlHttp = new XMLHttpRequest();\n"
      "    xmlHttp.open('GET', url, false);\n"
      "    xmlHttp.send(null);\n"
      "$MAYBE_LOG_GOT_URL"
      "    if (xmlHttp.responseText == 'LOGIN') {\n"
      "      document.location.reload(true);\n"
      "    }\n"
      "$SUCCESS_BLOCK"
      "  }\n"
      "  else if (authResult['error']) {\n"
      "$MAYBE_LOG_ERROR"
      "    if (authResult['error'] == 'immediate_failed') {\n"
      "$IMMEDIATE_FAILURE"
      "    } else {\n"
      "$FAILURE_BLOCK"
      "    }\n"
      "    document.getElementById('signinButton')"
           ".setAttribute('style', 'display:inline');\n"
      "  }\n"
      "}\n"
      "</script>\n";

  string html = javascript_template;
  if (!immediate_block.empty()) {
    const string reload_javascript =
        "    var url = '$POKE_URL'\n"
        "            + '?state=$STATE'\n"
        "            + '&access_token=' + authResult['access_token']\n"
        "            + '&id_token=' + authResult['id_token'];\n"
        "$MAYBE_LOG_CLEAR_ACCESS_TOKEN_AND_GET_URL"
        "    var xmlHttp = new XMLHttpRequest();\n"
        "    xmlHttp.open('GET', url, false);\n"
        "    xmlHttp.send(null);\n"
        "$MAYBE_LOG_GOT_URL"
        "    if (xmlHttp.responseText == 'LOGIN') {\n"
        "      document.location.reload(true);\n"
        "    }\n";
    html = StringReplace(html, "$IMMEDIATE_FAILURE", reload_javascript, false);
  } else {
    html = StringReplace(html, "$IMMEDIATE_FAILURE", "", false);
  }

  html = StringReplace(html, "$POKE_URL", access_token_url(), true);
  html = StringReplace(html, "$STATE", state, true);

  if (success_block.empty()) {
    html = StringReplace(html, "$SUCCESS_BLOCK", "", false);
  } else {
    html = StringReplace(
        html, "$SUCCESS_BLOCK", StrCat("    ", success_block), false);
  }
  if (failure_block.empty()) {
    html = StringReplace(html, "$FAILURE_BLOCK", "", false);
  } else {
    html = StringReplace(
        html, "$FAILURE_BLOCK",
        StrCat("    var error = authResult['error'];\n",
               "    ", failure_block, ";\n"),
        false);
  }

  if (log_to_console_) {
    html = StringReplace(
        html, "$MAYBE_LOG_ACCESS_TOKEN_AND_GET_URL",
        "    console.log('GOT Access Token');\n"
        "    console.log('GET ' + url);\n",
        false);
    html = StringReplace(
        html, "$MAYBE_LOG_CLEAR_ACCESS_TOKEN_AND_GET_URL",
        "    console.log('CLEAR Access Token');\n"
        "    console.log('GET ' + url);\n",
        false);
    html = StringReplace(
        html, "$MAYBE_LOG_ERROR",
        "    console.log('Signin Error: ' + authResult['error']);\n",
        false);
    html = StringReplace(
        html, "$MAYBE_LOG_GOT_URL",
        "    console.log('GOT ' + xmlHttp.status "
        "+ ' ' + xmlHttp.responseText);\n",
        true);
  } else {
    html = StringReplace(html, "$MAYBE_LOG_ACCESS_TOKEN_AND_GET_URL",
                         "", false);
    html = StringReplace(html, "$MAYBE_LOG_CLEAR_ACCESS_TOKEN_AND_GET_URL",
                         "", false);
    html = StringReplace(html, "$MAYBE_LOG_ERROR", "", false);
    html = StringReplace(html, "$MAYBE_LOG_GOT_URL", "", true);
  }

  return html;
}

util::Status AbstractGplusLoginFlow::DoInitiateAuthorizationFlow(
    WebServerRequest* request, const string& redirect_url) {
  return DoRespondWithNotLoggedInPage(GetCookieId(request), request);
}

// It is specific to our local user repository.
util::Status AbstractGplusLoginFlow::DoHandleAccessTokenUrl(
     WebServerRequest* request) {
  VLOG(1) << "Poke url handlerr=" << request->parsed_url().url();
  int http_code;
  string msg;

  string access_token;
  string cookie_id;
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
    OAuth2Credential* credential = flow()->NewCredential();
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

}  // namespace sample

}  // namespace googleapis
