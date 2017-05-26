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


#ifndef GOOGLEAPIS_AUTH_OAUTH2_SERVICE_AUTHORIZATION_H_
#define GOOGLEAPIS_AUTH_OAUTH2_SERVICE_AUTHORIZATION_H_

#include <string>
using std::string;

#include "googleapis/client/auth/oauth2_authorization.h"
#include "googleapis/client/util/status.h"
#include "googleapis/base/macros.h"
namespace googleapis {

namespace client {

/*
 * An OAuth 2.0 flow for service accounts to get access tokens.
 *
 * To create an instance of this flow you must explicitly instantiate one
 * then call the InitFromJson or InitFromClientSecretsPath with a project
 * that was created as a Service Account.
 *
 * The generic factory method
 * OAuth2AuthorizationFlow::MakeFlowFromClientSecretsPath
 * will not create one of these flows because the secrets file format returned
 * by the OAuth2.0 server is not explicit about being a service account.
 */
class OAuth2ServiceAccountFlow : public OAuth2AuthorizationFlow {
 public:
  explicit OAuth2ServiceAccountFlow(HttpTransport* transport);
  virtual ~OAuth2ServiceAccountFlow();

  /*
   * Sets the issuer (iss) attribute for the service account.
   *
   * This will be pulled from the client secrets file during Init() if present.
   */
  void set_client_email(const string& email) { client_email_ = email; }

  /*
   * Returns the service account (typically from the client secrets file).
   */
  const string& client_email() const         { return client_email_; }

  /*
   * Returns the project_id contained in the service account json file.
   */
  const string& project_id() const           { return project_id_; }

  /*
   * Sets the path of a Pkcs12 private key (typically from the API console).
   *
   * The actual key will be loaded later as needed but kept on disk.
   *
   * @return This method will fail if the path is not user-read only
   *         as a precaution.
   *
   * @see SetPrivateKeyFromPkcs12Path
   */
  googleapis::util::Status SetPrivateKeyPkcs12Path(const string& path);

  /*
   * Explicitly sets the private key.
   */
  void set_private_key(const string& key);

  /*
   * Refreshes the credential as an OAuth 2.0 Service Account.
   */
  virtual googleapis::util::Status PerformRefreshToken(
      const OAuth2RequestOptions& options, OAuth2Credential* credential);

 protected:
  virtual googleapis::util::Status InitFromJsonData(const SimpleJsonData* data);

  string MakeJwtClaims(const OAuth2RequestOptions& options) const;
  googleapis::util::Status ConstructSignedJwt(
      const string& plain_claims, string* result) const;

 private:
  string client_email_;
  string private_key_;  // typically disjoint with p12_path_
  string p12_path_;     // typically disjoint with private_key_
  string project_id_;

  googleapis::util::Status MakeJwt(const string& claims, string* jwt) const;
  DISALLOW_COPY_AND_ASSIGN(OAuth2ServiceAccountFlow);
};

}  // namespace client

}  // namespace googleapis
#endif  // GOOGLEAPIS_AUTH_OAUTH2_SERVICE_AUTHORIZATION_H_
