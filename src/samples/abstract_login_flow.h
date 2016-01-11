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


#ifndef GOOGLEAPIS_SAMPLES_ABSTRACT_LOGIN_FLOW_H_
#define GOOGLEAPIS_SAMPLES_ABSTRACT_LOGIN_FLOW_H_

#include <string>
using std::string;
#include "googleapis/client/util/abstract_webserver.h"
#include "googleapis/client/util/status.h"
#include "googleapis/base/callback.h"
#include "googleapis/base/macros.h"
namespace googleapis {

namespace client {
class OAuth2AuthorizationFlow;
class OAuth2Credential;
}  // namespace client

namespace sample {
using client::AbstractWebServer;
using client::WebServerRequest;

/*
 * Component that manages obtaining credentials for user http sessions.
 * @ingroup Samples
 *
 * This class is just for experimenting and illustrative purposes.
 * It is an abstract base class providing an interface and control flow
 * for managing credentials within login/logout commands on a webserver.
 *
 * I'm not sure how much sense it makes but seems convienent for playing
 * with different mechanisms and supporting samples. This is not tied
 * to a user, only "active" cookie and credential. Presumably the cookie
 * maps into some application level user object that also manages the
 * credential and this is just abstracting a process flow independent of that.
 *
 * The class is thread-safe for managing concurrent login flows for different
 * users.
 *
 * To use this class you must subclass it and override a few methods to render
 * pages as you wish and to make credentials available to your application.
 * <ul>
 *   <li>DoReceiveCredentialForCookieId to pass the credentials received
 *       from the OAuth2 server up to your application.
 *
 *   <li>DoGetCredentialForCookieId to get access to credentials from your
 *       application so the flow can make decisions.
 *
 *   <li>DoRespondWithWelcomPage to display a page when credentials are
 *       obtained without a redirect for the page that requested them.
 *       This means you've logged in from the login page directly as opposed
 *       to passing through the login page on your way to protected content.
 *
 *   <li>DoRespondWithNotLoggedInPage to display the login page when
 *       credentials are not available and there is no redirect. At the
 *       minimum this page should contain a control that, when clicked,
 *       will call InitiateAuthorizationFlow. Note that when there is
 *       a redirect there is a redirect the page will pass through to the
 *       OAuth2.0 server to obtain credentials and log in.
 *
 *   <li>DoRespondWithLoginErrorPage to display the login page when
 *       a login attempt fails (or is canceled).
 * </ul>
 *
 *
 * Additionally you must make the following calls to set this up:
 * <ul>
 *   <li>AddLoginUrl to hook the login processing into your webserver if you
 *       want the component to perform the login flow.
 *
 *   <li>AddLogoutUrl to hook the logout processing into your webserver if you
 *       want the component to perform the logout flow.
 *
 *   <li>AddReceiveAccessTokenUrl to hook up the OAuth2 token callback and
 *       receive credentials from the server.
 * </ul>
 *
 * Finally, call InitiateAuthorizationFlow to initiate a flow.
 * This is usualy from an action on the page returned by
 * DoRespondWithLoginPage.
 */
class AbstractLoginFlow {
 public:
  /*
   * Standard constructor.
   *
   * @param[in] cookie_name The name of the cookie to use for the cookie_id.
   * @param[in] redirect_name The name of the redirect query parameter.
   * @param[in] flow The flow for talking to the OAuth 2.0 server when we
   *                 need to exchange an authorization code for access token.
   */
  AbstractLoginFlow(
      const string& cookie_name,
      const string& redirect_name,
      client::OAuth2AuthorizationFlow* flow);

  /*
   * Standard destructor.
   */
  virtual ~AbstractLoginFlow();

  googleapis::util::Status InitiateAuthorizationFlow(
      WebServerRequest* request, const string& redirect_url = "");

  /*
   * Return the cookie_id cooke bound in the constructor.
   */
  const string& cookie_name() const  { return cookie_name_; }

  /*
   * Return the redirect url parameter bound in the constructor.
   */
  const string& redirect_param_name() const { return redirect_name_; }

  /*
   * Return the OAuth2 Authorization flow bound in the constructor.
   */
  client::OAuth2AuthorizationFlow* flow() const { return flow_; }

  /*
   * Adds a login handler with the given URL to the web server.
   *
   * @param[in] url The url that triggers the handler.
   * @param[ni] httpd The webserver to register then handler on.
   *
   * @see DoRespondWithWelcomePage
   * @see DoRespondWithNotLoggedInPage
   * @see DoRespondWithLoginErrorPage
   */
  void AddLoginUrl(const string& url, AbstractWebServer* httpd);

  /*
   * Adds a logout handler with the given URL to the web server.
   *
   * @param[in] url The url that triggers the handler.
   * @param[ni] httpd The webserver to register then handler on.
   */
  void AddLogoutUrl(const string& url, AbstractWebServer* httpd);

  /*
   * Adds the handler receiving OAuth2 access tokens using the given URL.
   *
   * @param[in] url The url that triggers the handler.
   * @param[ni] httpd The webserver to register then handler on.
   */
  void AddReceiveAccessTokenUrl(
      const string& url, AbstractWebServer* httpd);

 protected:
  /*
   * Handler called when the component receives a login credential
   * (or failure).
   *
   * This should just update application state. The resopnse will be handled
   * elsewhere.
   *
   * @param[in] cookie_id The user cookie this notification is for.
   * @param[in] status Ok if we have a credential, otherwise the error.
   * @parma[in] credential Ownership is passed (NULL on failure).
   *
   * @return true if this cookie was known already, false if first time.
   */
  virtual bool DoReceiveCredentialForCookieId(
      const string& cookie_id,
      const googleapis::util::Status& status,
      client::OAuth2Credential* credential) = 0;

  /*
   * Returns the current credential for the given cookie_id
   *
   * @param[in] cookie_id The cookie id from the cookie.
   *
   * @return NULL if no credential is available.
   */
  virtual client::OAuth2Credential*
  DoGetCredentialForCookieId(const string& cookie_id) = 0;

  virtual googleapis::util::Status DoInitiateAuthorizationFlow(
      WebServerRequest* request, const string& redirect_url)  = 0;

  /*
   * Handler after we've successfully logged in without a redirect.
   *
   * The default implementation fails. You must override this if you
   * add the login url handler.
   *
   * @param[in] cookie_id The user cookie that logged in.
   * @param[in] requset_data The request we are responding to.
   *
   * @return ok or reason for failure.
   *
   * @see AddLoginUrl
   */
  virtual googleapis::util::Status DoRespondWithWelcomePage(
      const string& cookie_id, WebServerRequest* request) = 0;

  /*
   * Handler for login page when we are not logged in and have no redirect.
   *
   * The default implementation fails. You must override this if you
   * add the login url handler.
   *
   * @param[in] cookie_id The cookie for the user that is not logged in.
   * @param[in] requset_data The request we are responding to.
   *
   * @return ok or reason for failure.
   */
  virtual googleapis::util::Status DoRespondWithNotLoggedInPage(
      const string& cookie_id,
      WebServerRequest* request) = 0;

  /*
   * Handler for login page when we encounter a login error.
   *
   * The default implementation fails. You must override this if you
   * add the login url handler.
   *
   * @param[in] cookie_id The cookie for the user that is not logged in.
   * @param[in] requset_data The request we are responding to.
   *
   * @return ok or reason for failure.
   */
  virtual googleapis::util::Status DoRespondWithLoginErrorPage(
      const string& cookie_id,
      const googleapis::util::Status& status,
      WebServerRequest* request) = 0;

  virtual googleapis::util::Status DoHandleAccessTokenUrl(WebServerRequest* request);
  virtual googleapis::util::Status DoHandleLoginUrl(WebServerRequest* request);
  virtual googleapis::util::Status DoHandleLogoutUrl(WebServerRequest* request);

 protected:
  /*
   * Responds to a request by redirecting to a url.
   *
   * @param[in] url The url we want to redirect to.
   * @param[in] cookie_id The cookie_id to write into a cookie.
   * @param[in] request The request we're responding to.
   *
   * @return ok or reason for failure.
   */
  googleapis::util::Status RedirectToUrl(
      const string& url, const string& cookie_id,
      WebServerRequest* request);

  googleapis::util::Status ReceiveAuthorizationCode(
    const string& cookie_id, const string& want_url,
    WebServerRequest* request);

  /*
   * Extracts the user id from the cookie in the request
   *
   * @return empty string if there is no cookie.
   */
  string GetCookieId(WebServerRequest* request);

  const string& login_url() const { return login_url_; }
  const string& logout_url() const { return logout_url_; }
  const string& access_token_url() const { return access_token_url_; }

 private:
  /*
   * The name of the cookie we're using for the cookie_id value.
   */
  string cookie_name_;
  string redirect_name_;
  string login_url_;
  string logout_url_;
  string access_token_url_;

  /*
   * A reference to the flow that we're using to get Authorization Tokens.
   * We do not own this.
   */
  client::OAuth2AuthorizationFlow* flow_;

  /*
   * This callback is used to resolve the requests from the OAuth 2.0 server
   * that gives us the authentication codes (or responses) that we asked for.
   */
  googleapis::util::Status HandleAccessTokenUrl(WebServerRequest* request);

  /*
   * This callback implements the login process flow added by AddLoginUrl
   */
  googleapis::util::Status HandleLoginUrl(WebServerRequest* request);

  /*
   * This callback implements the logout process flow added by AddLogoutUrl
   */
  googleapis::util::Status HandleLogoutUrl(WebServerRequest* request);

  DISALLOW_COPY_AND_ASSIGN(AbstractLoginFlow);
};

}  // namespace sample

}  // namespace googleapis
#endif  // GOOGLEAPIS_SAMPLES_ABSTRACT_LOGIN_FLOW_H_
