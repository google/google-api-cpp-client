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


#ifndef GOOGLEAPIS_SAMPLES_ABSTRACT_WEBSERVER_LOGIN_FLOW_H_
#define GOOGLEAPIS_SAMPLES_ABSTRACT_WEBSERVER_LOGIN_FLOW_H_

#include <memory>
#include <string>
using std::string;
#include "googleapis/client/auth/oauth2_pending_authorizations.h"
#include "samples/abstract_login_flow.h"
#include "googleapis/client/util/status.h"
#include "googleapis/base/callback.h"
#include "googleapis/base/macros.h"
namespace googleapis {

namespace client {
class OAuth2AuthorizationFlow;
class OAuth2Credential;
}  // namespace client

namespace sample {
using client::WebServerRequest;
using client::WebServerResponse;

/*
 * Component that manages obtaining credentials for user http sessions.
 * @ingroup Samples
 *
 * This class is just for experimenting and illustrative purposes.
 * Often it is simpler and perhaps more desirable to use something like
 * the Google+ javascript button (https://developers.google.com/+/web/signin/).
 *
 * Unlike the javascript button component, this mechanism is purely server
 * side code without any use of javascript.
 *
 * The class is thread-safe for managing concurrent login flows for different
 * users.
 *
 * Call InitiateAuthorizationFlow to initiate a flow.
 * This is usualy from an action on the page returned by
 * DoRespondWithLoginPage.
 *
 * Despite the name, this class is still abstract leaving the following
 * methods for managing user credetials and procuring page content.
 *    DoReceiveCredentialForCookieId
 *    DoGetCredentialForCookieId
 *    DoRespondWithWelcomePage
 *    DoRespondWithNotLoggedInPage
 *    DoRespondWithLoginErrorPage
 */
class AbstractWebServerLoginFlow : public AbstractLoginFlow {
 public:
  /*
   * @param[in] RequestData The request received when resolved.
   * @return ok or reason for failure.
   */
  typedef ResultCallback1< googleapis::util::Status, WebServerRequest*>
      PendingAuthorizationHandler;

  /*
   * Standard constructor.
   *
   * @param[in] cookie_name The name of the cookie to use for the cookie_id.
   * @param[in] flow The flow for talking to the OAuth 2.0 server when we
   *                 need to exchange an authorization code for access token.
   * @param[in] notifier The injected callback for pulling out the access codes
   *                     that we obtain for different users.
   */
  AbstractWebServerLoginFlow(const string& cookie_name,
                             const string& redirect_name,
                             client::OAuth2AuthorizationFlow* flow);

  /*
   * Standard destructor.
   */
  virtual ~AbstractWebServerLoginFlow();

 protected:
  /*
   * This callback is used to resolve the requests from the OAuth 2.0 server
   * that gives us the authentication codes (or responses) that we asked for.
   */
  virtual googleapis::util::Status DoHandleAccessTokenUrl(WebServerRequest* request);

 private:
  /*
   * Store of pending authorizations so we can correlate the tokens back
   * to their credentials.
   */
  std::unique_ptr<client::OAuth2PendingAuthorizations<
               PendingAuthorizationHandler> >
    pending_;

  virtual googleapis::util::Status DoInitiateAuthorizationFlow(
      WebServerRequest* request, const string& redirect_url);

  /*
   * AbstractWebServer handler that gets access tokens for authorization codes.
   *
   * This is used as a callback.
   *
   * @param[in] cookie_id this is typically curried into the callback.
   * @param[in] want_url to redirect to is typically curried as well.
   *
   * @param[in] request_data is the inbound request we're receiving
   *            that contains the authorization code.
   *
   * @return ok or reason for failure.
   */
  googleapis::util::Status ReceiveAuthorizationCode(
      const string& cookie_id, const string& want_url,
      WebServerRequest* request);

  DISALLOW_COPY_AND_ASSIGN(AbstractWebServerLoginFlow);
};

}  // namespace sample

}  // namespace googleapis
#endif  // GOOGLEAPIS_SAMPLES_ABSTRACT_WEBSERVER_LOGIN_FLOW_H_
