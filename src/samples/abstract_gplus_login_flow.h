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


#ifndef GOOGLEAPIS_SAMPLES_ABSTRACT_GPLUS_LOGIN_FLOW_H_
#define GOOGLEAPIS_SAMPLES_ABSTRACT_GPLUS_LOGIN_FLOW_H_

#include <string>
using std::string;
#include "samples/abstract_login_flow.h"
#include "googleapis/client/util/abstract_webserver.h"
#include "googleapis/base/macros.h"
namespace googleapis {

namespace client {
class OAuth2Credential;
}

namespace sample {
using client::WebServerRequest;

/*
 * Component for having the browser fetch access tokens using G+ sign-in.
 * @ingroup Samples
 */
class AbstractGplusLoginFlow : public AbstractLoginFlow {
 public:
  /*
   * Standard constructor.
   */
  explicit AbstractGplusLoginFlow(
      const string& cookie_name,
      const string& redirect_name,
      client::OAuth2AuthorizationFlow* flow);

  /*
   * Standard destructor.
   */
  virtual ~AbstractGplusLoginFlow();

  void set_client_id(const string& id)  { client_id_ = id; }
  const string& client_id() const       { return client_id_; }

  void set_scopes(const string& s)   { scopes_ = s; }
  const string& scopes() const       { return scopes_; }

  bool log_to_console() const      { return log_to_console_; }
  void set_log_to_console(bool on) { log_to_console_ = on; }

  /*
   * Render HTML javascript stuff that goes in the head block.
   */
  virtual string GetPrerequisiteHeadHtml();

  /*
   * Render the callback javascript HTML block.
   *
   * This includes the script tags so can go anywhere.
   *
   * @param[in] state The state query parameter value when redirecting.
   * @param[in] immediate_block Javascript statement to call when
   *            immediate fails (after revoking tokens).
   * @param[in] success_block Javascript statement to execute on login.
   *            This is usually something like window.location='...'.
   *            Can be empty.
   * @param[in] failure_block Javascript statement to execute on failure.
   *            The error description is in a javascript variable 'error'.
   *            Can be empty.
   */
  virtual string GetSigninCallbackJavascriptHtml(
      const string& state,
      const string& immediate_block,
      const string& success_block,
      const string& failure_block);

  /*
   * Returns HTML rendering the G+ Sign-in buton.
   *
   * @param[in] default_visible If true make the button visible by default.
   *            The button will be made visible if login is needed,
   *            and invisible once logged in.
   */
  virtual string GetSigninButtonHtml(bool default_visible);

 protected:
  virtual googleapis::util::Status DoInitiateAuthorizationFlow(
      WebServerRequest* request, const string& redirect_url);

  /*
   * Handles the poke callback when toens are received.
   *
   * Updates (or creates) user data for the user this request is on behalf of.
   *
   * @param[in] request Contains state, access_token and
   *                     query parametersa. Has cooke for our cookie_id.
   *
   * @return MHD_YES or MHD_NO for MicroHttpd API
   */
  virtual googleapis::util::Status DoHandleAccessTokenUrl(WebServerRequest* request);

 private:
  string client_id_;
  string scopes_;
  bool log_to_console_;

  DISALLOW_COPY_AND_ASSIGN(AbstractGplusLoginFlow);
};

}  // namespace sample

}  // namespace googleapis
#endif  // GOOGLEAPIS_SAMPLES_ABSTRACT_GPLUS_LOGIN_FLOW_H_
