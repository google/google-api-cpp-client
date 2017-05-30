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


#include <memory>

#include "googleapis/config.h"
#include "googleapis/client/auth/jwt_builder.h"
#include "googleapis/client/auth/oauth2_service_authorization.h"

#include "googleapis/client/data/data_reader.h"
#include "googleapis/client/transport/http_transport.h"
#include "googleapis/client/transport/http_request.h"
#include "googleapis/client/transport/http_response.h"
#include "googleapis/client/util/date_time.h"
#include "googleapis/client/util/file_utils.h"
#include "googleapis/client/util/uri_utils.h"
#include "googleapis/client/util/status.h"
#include <glog/logging.h>
#include "googleapis/strings/strcat.h"
#include "googleapis/util/file.h"
#include <openssl/ossl_typ.h>
#include <openssl/bio.h>
#include <openssl/buffer.h>
#include <openssl/evp.h>
#include <openssl/err.h>
#include <openssl/pem.h>
#include <openssl/pkcs12.h>
#include <openssl/sha.h>
#include <openssl/x509.h>

namespace googleapis {

namespace client {

OAuth2ServiceAccountFlow::OAuth2ServiceAccountFlow(
    HttpTransport* transport)
    : OAuth2AuthorizationFlow(transport) {
}

OAuth2ServiceAccountFlow::~OAuth2ServiceAccountFlow() {
}

void OAuth2ServiceAccountFlow::set_private_key(const string& key) {
  DCHECK(p12_path_.empty());
  private_key_ = key;
}

util::Status OAuth2ServiceAccountFlow::SetPrivateKeyPkcs12Path(
    const string& path) {
  DCHECK(private_key_.empty());
  p12_path_.clear();
  googleapis::util::Status status = SensitiveFileUtils::VerifyIsSecureFile(path, false);
  if (!status.ok()) return status;
  p12_path_ = path;
  return StatusOk();
}

util::Status OAuth2ServiceAccountFlow::InitFromJsonData(
    const SimpleJsonData* data) {
  googleapis::util::Status status = OAuth2AuthorizationFlow::InitFromJsonData(data);
  if (!status.ok()) return status;

  if (!GetStringAttribute(data, "client_email", &client_email_)) {
    return StatusInvalidArgument(StrCat("Missing client_email attribute"));
  }
  VLOG(4) << "client_email: " << client_email_;

  GetStringAttribute(data, "private_key", &private_key_);
  VLOG(4) << "private_key: " << private_key_.substr(0, 40) << "...";

  GetStringAttribute(data, "project_id", &project_id_);
  VLOG(4) << "project_id: " << project_id_;

  return StatusOk();
}

util::Status OAuth2ServiceAccountFlow::PerformRefreshToken(
    const OAuth2RequestOptions& options, OAuth2Credential* credential) {
  string claims = MakeJwtClaims(options);
  VLOG(4) << "JWT claims: " << claims;
  string jwt;

  googleapis::util::Status status = MakeJwt(claims, &jwt);
  VLOG(4) << "JWT: " << jwt;
  if (!status.ok()) return status;

  string grant_type = "urn:ietf:params:oauth:grant-type:jwt-bearer";
  string content =
      StrCat("grant_type=", EscapeForUrl(grant_type), "&assertion=", jwt);

  std::unique_ptr<HttpRequest> request(
      transport()->NewHttpRequest(HttpRequest::POST));
  if (options.timeout_ms > 0) {
    request->mutable_options()->set_timeout_ms(options.timeout_ms);
  }
  request->set_url(client_spec().token_uri());
  request->set_content_type(HttpRequest::ContentType_FORM_URL_ENCODED);
  request->set_content_reader(NewUnmanagedInMemoryDataReader(content));

  status = request->Execute();
  if (status.ok()) {
    status = credential->Update(request->response()->body_reader());
  } else {
    VLOG(1) << "Failed to update credential";
  }
  return status;
}

string OAuth2ServiceAccountFlow::MakeJwtClaims(
    const OAuth2RequestOptions& options) const {
  time_t now = DateTime().ToEpochTime();
  int duration_secs = 60 * 60;  // 1 hour
  const string* scopes = &options.scopes;

  if (scopes->empty()) {
    scopes = &default_scopes();
    if (scopes->empty()) {
      LOG(WARNING) << "Making claims without any scopes";
    }
  }

  string claims = "{";
  const string sep(",");
  if (!options.email.empty()) {
    AppendJsonStringAttribute(&claims, sep, "prn", options.email);
  }

  AppendJsonStringAttribute(&claims, "", "scope", *scopes);
  AppendJsonStringAttribute(&claims, sep, "iss", client_email_);
  AppendJsonStringAttribute(&claims, sep, "aud", client_spec().token_uri());
  AppendJsonScalarAttribute(&claims, sep, "exp", now + duration_secs);
  AppendJsonScalarAttribute(&claims, sep, "iat", now);
  StrAppend(&claims, "}");

  return claims;
}

util::Status OAuth2ServiceAccountFlow::ConstructSignedJwt(
    const string& plain_claims, string* result) const {
  return MakeJwt(plain_claims, result);
}

util::Status OAuth2ServiceAccountFlow::MakeJwt(
    const string& claims, string* jwt) const {
  EVP_PKEY* pkey = NULL;
  if (!p12_path_.empty()) {
    DCHECK(private_key_.empty());
    VLOG(1) << "Loading private key from " << p12_path_;
    pkey = JwtBuilder::LoadPkeyFromP12Path(p12_path_.c_str());
  } else if (!private_key_.empty()) {
    pkey = JwtBuilder::LoadPkeyFromData(private_key_);
  } else {
    return StatusInternalError("PrivateKey not set");
  }

  if (!pkey) {
    return StatusInternalError("Could not load pkey");
  }
  googleapis::util::Status status = JwtBuilder::MakeJwtUsingEvp(claims, pkey, jwt);
  EVP_PKEY_free(pkey);
  return status;
}

}  // namespace client

}  // namespace googleapis
