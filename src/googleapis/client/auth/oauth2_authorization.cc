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


#include <sstream>
#include <string>
using std::string;
#include <vector>
using std::vector;

#include "googleapis/base/callback.h"
#include <glog/logging.h>
#include "googleapis/base/mutex.h"
#include "googleapis/base/scoped_ptr.h"
#include "googleapis/util/file.h"
#include "googleapis/client/auth/credential_store.h"
#include "googleapis/client/auth/oauth2_authorization.h"
#include "googleapis/client/data/data_reader.h"
#include "googleapis/client/transport/http_request.h"
#include "googleapis/client/transport/http_response.h"
#include "googleapis/client/transport/http_transport.h"
#include "googleapis/client/transport/http_types.h"
#include "googleapis/client/util/date_time.h"
#include "googleapis/client/util/file_utils.h"
#include "googleapis/client/util/status.h"
#include "googleapis/client/util/uri_utils.h"

// Note we are explicitly using jsoncpp here as an implementation detail.
// We are intentionally not using the jsoncpp_data package introduced by
// this package. Two reasons for this
//   1) This is an implementation detail not exposed externally
//   2) I want to keep JsonCppData decoupled so I can easily replace the
//      data model. Currently JsonCppData is introduced by the code generator
//      and not by the core runtime library.
#include <json/reader.h>
#include <json/value.h>

#include "googleapis/strings/case.h"
#include "googleapis/strings/join.h"
#include "googleapis/strings/strcat.h"
#include "googleapis/strings/util.h"
#include "googleapis/util/status.h"

namespace googleapis {

namespace {

using client::StatusInvalidArgument;
using client::StatusOk;
using client::StatusUnknown;

const char kDefaultAuthUri_[] =
    "https://accounts.google.com/o/oauth2/auth";
const char kDefaultTokenUri_[] =
    "https://accounts.google.com/o/oauth2/token";
const char kDefaultRevokeUri_[] =
    "https://accounts.google.com/o/oauth2/revoke";

}  // anonymous namespace

namespace client {

OAuth2TokenRequest::OAuth2TokenRequest(HttpRequest* request)
    : http_request_(request) {
}
OAuth2TokenRequest::~OAuth2TokenRequest() {}


void OAuth2AuthorizationFlow::ResetCredentialStore(CredentialStore* store) {
  credential_store_.reset(store);
}

class OAuth2AuthorizationFlow::SimpleJsonData {
 public:
  SimpleJsonData() {}
  util::Status Init(const StringPiece& json);
  string InitFromContainer(const StringPiece& json);
  bool GetString(const char* field, string* value) const;
  bool GetScalar(const char* field, int* value) const;
  bool GetBool(const char* field, bool* value) const;

  bool GetFirstArrayElement(const char* field, string* value) const;

 private:
  mutable Json::Value json_;
};

util::Status OAuth2AuthorizationFlow::SimpleJsonData::Init(
     const StringPiece& json) {
  Json::Reader reader;
  std::istringstream stream(json.as_string());
  if (!reader.parse(stream, json_)) {
    util::Status status(StatusInvalidArgument("Invalid JSON"));
    LOG(ERROR) << status.error_message();
    return status;
  }
  return StatusOk();
}
string OAuth2AuthorizationFlow::SimpleJsonData::InitFromContainer(
     const StringPiece& json) {
  if (!Init(json).ok() || json_.begin() == json_.end()) {
    return "";
  }
  string name = json_.begin().key().asString();
  json_ = *json_.begin();
  return name;
}
bool OAuth2AuthorizationFlow::SimpleJsonData::GetString(
    const char* field_name, string* value) const {
  if (!json_.isMember(field_name)) return false;

  *value = json_[field_name].asString();
  return true;
}
bool OAuth2AuthorizationFlow::SimpleJsonData::GetScalar(
    const char* field_name, int* value) const {
  if (!json_.isMember(field_name)) return false;

  *value = json_[field_name].asInt();
  return true;
}
bool OAuth2AuthorizationFlow::SimpleJsonData::GetBool(
    const char* field_name, bool* value) const {
  if (!json_.isMember(field_name)) return false;

  *value = json_[field_name].asBool();
  return true;
}
bool OAuth2AuthorizationFlow::SimpleJsonData::GetFirstArrayElement(
    const char* field_name, string* value) const {
  if (!json_.isMember(field_name)) return false;

  Json::Value array = json_[field_name];
  if (array.size() == 0) return false;
  *value = (*array.begin()).asString();
  return true;
}


const char OAuth2AuthorizationFlow::kOutOfBandUrl[] =
  "urn:ietf:wg:oauth:2.0:oob";
const char OAuth2AuthorizationFlow::kGoogleAccountsOauth2Url[] =
  "https://accounts.google.com/o/oauth2";

OAuth2ClientSpec::OAuth2ClientSpec()
    : auth_uri_(kDefaultAuthUri_),
      token_uri_(kDefaultTokenUri_),
      revoke_uri_(kDefaultRevokeUri_) {
}

OAuth2ClientSpec::~OAuth2ClientSpec() {
}

const StringPiece OAuth2Credential::kOAuth2CredentialType = "OAuth2";

OAuth2Credential::OAuth2Credential()
    : flow_(NULL), user_id_verified_(false) {
  // If we dont know an expiration then assume it never will.
  expiration_timestamp_secs_.set(kint64max);
}

OAuth2Credential::~OAuth2Credential() {
}

const StringPiece OAuth2Credential::type() const {
  return kOAuth2CredentialType;
}

void OAuth2Credential::Clear() {
  access_token_.clear();
  refresh_token_.clear();
  expiration_timestamp_secs_.set(kint64max);
  user_id_.clear();
  user_id_verified_ = false;
}

util::Status OAuth2Credential::Refresh() {
  if (!flow_) {
    return StatusFailedPrecondition("No flow bound.");
  }
  return flow_->PerformRefreshToken(OAuth2RequestOptions(), this);
}

util::Status OAuth2Credential::Load(DataReader* reader) {
  Clear();
  return Update(reader);
}

util::Status OAuth2Credential::Update(DataReader* reader) {
  string json = reader->RemainderToString();
  if (reader->error()) {
    util::Status status(util::error::UNKNOWN, "Invalid credential");
    LOG(ERROR) << status.error_message();
    return status;
  }

  return UpdateFromString(json);
}

DataReader* OAuth2Credential::MakeDataReader() const {
  string refresh_token = refresh_token_.as_string();
  string access_token = access_token_.as_string();
  int64 expires_at = expiration_timestamp_secs_.get();
  string json = "{";
  const char* sep = "";

  if (!access_token.empty()) {
    StrAppend(&json, sep, "\"access_token\":\"", access_token, "\"");
    sep = ",";
  }
  if (!refresh_token.empty()) {
    StrAppend(&json, sep, "\"refresh_token\":\"", refresh_token, "\"");
    sep = ",";
  }
  if (expires_at != kint64max) {
    StrAppend(&json, sep, "\"expires_at\":\"", expires_at, "\"");
    sep = ",";
  }
  if (!user_id_.empty()) {
    StrAppend(&json, sep, "\"user_id\":\"", user_id_, "\"");
    StrAppend(&json, sep,
              "\"user_id_verified\":", user_id_verified_ ? "true" : "false");
  }
  json.append("}");
  return NewManagedInMemoryDataReader(json);
}

util::Status OAuth2Credential::AuthorizeRequest(HttpRequest* request) {
  if (!access_token_.empty()) {
    string bearer = "Bearer ";
    access_token_.AppendTo(&bearer);
    request->AddHeader(HttpRequest::HttpHeader_AUTHORIZATION, bearer);
  }
  return StatusOk();
}

util::Status OAuth2Credential::UpdateFromString(const StringPiece& json) {
  OAuth2AuthorizationFlow::SimpleJsonData data;
  util::Status status = data.Init(json);
  if (!status.ok()) return status;

  string str_value;
  int int_value;
  bool bool_value;

  if (data.GetString("refresh_token", &str_value)) {
    VLOG(1) << "Updating refresh token";
    refresh_token_.set(str_value);
  }
  if (data.GetString("access_token", &str_value)) {
    access_token_.set(str_value);
    VLOG(1) << "Updating access token";
  }
  if (data.GetString("expires_at", &str_value)) {
    int64 timestamp;
    if (!safe_strto64(str_value.c_str(), &timestamp)) {
      LOG(ERROR) << "Invalid timestamp=[" << str_value << "]";
    } else {
      expiration_timestamp_secs_.set(timestamp);
      VLOG(1) << "Updating access token expiration";
    }
  } else if (data.GetScalar("expires_in", &int_value)) {
    int64 now = DateTime().ToEpochTime();
    int64 expiration = now + int_value;
    expiration_timestamp_secs_.set(expiration);
    VLOG(1) << "Updating access token expiration";
  }

  if (data.GetString("user_id", &str_value)) {
    if (!data.GetBool("is_validated", &bool_value)) {
      bool_value = false;
    }
    user_id_ = str_value;
    user_id_verified_ = bool_value;
  }

  return StatusOk();
}

OAuth2RevokeTokenRequest::OAuth2RevokeTokenRequest(
    HttpTransport* transport,
    const OAuth2ClientSpec* client,
    ThreadsafeString* token)
    : OAuth2TokenRequest(transport->NewHttpRequest(HttpRequest::POST)),
      client_(client), token_(token) {
}

util::Status OAuth2RevokeTokenRequest::Execute() {
  HttpRequest* request = http_request();
  request->set_url(client_->revoke_uri());
  request->set_content_type(HttpRequest::ContentType_FORM_URL_ENCODED);

  string* content = new string("token=");
  token_->AppendTo(content);
  request->set_content_reader(
      NewManagedInMemoryDataReader(
          StringPiece(*content), DeletePointerClosure(content)));

  util::Status status = request->Execute();
  if (status.ok()) {
    token_->set("");
  }
  return status;
}

OAuth2ExchangeAuthorizationCodeRequest::OAuth2ExchangeAuthorizationCodeRequest(
    HttpTransport* transport,
    const StringPiece& authorization_code,
    const OAuth2ClientSpec& client,
    const OAuth2RequestOptions& options,
    OAuth2Credential* credential)
    : OAuth2TokenRequest(transport->NewHttpRequest(HttpRequest::POST)),
      credential_(credential) {
  CHECK(!authorization_code.empty());
  CHECK(!client.client_id().empty());
  CHECK(!client.client_secret().empty());

  const StringPiece redirect = options.redirect_uri.empty()
      ? client.redirect_uri()
      : options.redirect_uri;
  content_ = StrCat("code=", EscapeForUrl(authorization_code),
                    "&client_id=", EscapeForUrl(client.client_id()),
                    "&client_secret=", EscapeForUrl(client.client_secret()),
                    "&redirect_uri=", EscapeForUrl(redirect),
                    "&grant_type=authorization_code");
  token_uri_ = client.token_uri();
}

util::Status OAuth2ExchangeAuthorizationCodeRequest::Execute() {
  HttpRequest* request = http_request();
  request->set_url(token_uri_);
  request->set_content_type(HttpRequest::ContentType_FORM_URL_ENCODED);
  request->set_content_reader(NewUnmanagedInMemoryDataReader(content_));

  util::Status status = request->Execute();
  if (status.ok()) {
    status = credential_->Update(request->response()->body_reader());
  }
  return status;
}

OAuth2RefreshTokenRequest::OAuth2RefreshTokenRequest(
    HttpTransport* transport,
    const OAuth2ClientSpec* client,
    OAuth2Credential* credential)
    : OAuth2TokenRequest(transport->NewHttpRequest(HttpRequest::POST)),
      client_(client), credential_(credential) {
  CHECK(!client_->client_id().empty());
  CHECK(!client_->client_secret().empty());
  CHECK(!credential_->refresh_token().empty());
}

util::Status OAuth2RefreshTokenRequest::Execute() {
  HttpRequest* request = http_request();
  request->set_url(client_->token_uri());
  request->set_content_type(HttpRequest::ContentType_FORM_URL_ENCODED);
  string* content =
      new string(
          StrCat("client_id=", client_->client_id(),
                 "&client_secret=", client_->client_secret(),
                 "&grant_type=refresh_token",
                 "&refresh_token="));
  credential_->refresh_token().AppendTo(content);
  request->set_content_reader(
      NewManagedInMemoryDataReader(
          StringPiece(*content), DeletePointerClosure(content)));

  util::Status status = request->Execute();
  if (status.ok()) {
    status = credential_->Update(http_response()->body_reader());
  }

  if (!status.ok()) {
    LOG(ERROR) << "Refresh failed with " << status.error_message();
  }

  return status;
}

OAuth2AuthorizationFlow::OAuth2AuthorizationFlow(HttpTransport* transport)
    : transport_(transport) {
}

OAuth2AuthorizationFlow::~OAuth2AuthorizationFlow() {
}

OAuth2Credential* OAuth2AuthorizationFlow::NewCredential() {
  OAuth2Credential* credential = new OAuth2Credential;
  credential->set_flow(this);
  return credential;
}

void OAuth2AuthorizationFlow::set_authorization_code_callback(
    AuthorizationCodeCallback* callback) {
  if (callback) callback->CheckIsRepeatable();
  authorization_code_callback_.reset(callback);
}

util::Status
OAuth2AuthorizationFlow::PerformExchangeAuthorizationCode(
     const string& authorization_code,
     const OAuth2RequestOptions& options,
     OAuth2Credential* credential) {
  scoped_ptr<OAuth2TokenRequest> request(
      NewExchangeAuthorizationCodeRequestWithOptions(
          authorization_code, options, credential));
  return request->Execute();
}

util::Status OAuth2AuthorizationFlow::PerformRefreshToken(
     const OAuth2RequestOptions& options, OAuth2Credential* credential) {
  if (credential->refresh_token().empty()) {
    // The constructor for OAuth2RefreshTokenRequest segfaults if the
    //  refresh token is empty.
    return StatusInvalidArgument(
        "The credential doesn't contain a refresh token so we can't refresh.");
  }
  scoped_ptr<OAuth2TokenRequest> request(NewRefreshTokenRequest(credential));
  return request->Execute();
}

util::Status OAuth2AuthorizationFlow::PerformRevokeToken(
     bool access_token_only, OAuth2Credential* credential) {
  scoped_ptr<OAuth2TokenRequest> request(
      access_token_only
      ? NewRevokeAccessTokenRequest(credential)
      : NewRevokeRefreshTokenRequest(credential));
  return request->Execute();
}

util::Status OAuth2AuthorizationFlow::RefreshCredentialWithOptions(
     const OAuth2RequestOptions& options, OAuth2Credential* credential) {
  scoped_ptr<OAuth2TokenRequest> token_request;
  string refresh_token = credential->refresh_token().as_string();

  if (refresh_token.empty() && credential_store_.get()) {
    // If we dont have a refresh token, try reloading from the store.
    // This could because we havent yet loaded the credential.
    // If this fails, we'll just handle it as a first-time case.
    util::Status status =
        credential_store_->InitCredential(options.user_id, credential);
    if (status.ok()) {
      if (credential->user_id().empty()) {
        credential->set_user_id(options.user_id, false);
      }
      refresh_token = credential->refresh_token().as_string();
    }
  }

  bool refreshed_ok = false;
  if (!refresh_token.empty()) {
    if (options.user_id != credential->user_id()) {
      string error = "UserID does not match credential's user_id";
      LOG(ERROR) << error;
      return StatusInvalidArgument(error);
    }
    token_request.reset(NewRefreshTokenRequest(credential));

    // Try executing this request. If it fails, we'll continue as if we
    // did not find the token.
    refreshed_ok = token_request->Execute().ok();
    if (!refreshed_ok) {
      LOG(ERROR) << "Could not refresh existing credential.\n"
                 << "Trying to obtain a new one instead.";
    }
  }

  if (refreshed_ok) {
    // Do nothing until common code below.
  } else if (!authorization_code_callback_.get()) {
    StringPiece error = "No prompting mechanism provided to get authorization";
    LOG(ERROR) << error;
    return StatusUnimplemented(error);
  } else {
    // If we still dont have a credeitnal then we need to kick off
    // authorization again to get an access (and refresh) token.
    string auth_code;
    OAuth2RequestOptions actual_options = options;
    if (actual_options.scopes.empty()) {
      actual_options.scopes = default_scopes_;
    }
    if (actual_options.redirect_uri.empty()) {
      actual_options.redirect_uri = client_spec_.redirect_uri();
    }
    util::Status status =
          authorization_code_callback_->Run(actual_options, &auth_code);
    if (!status.ok()) return status;

    token_request.reset(
        NewExchangeAuthorizationCodeRequestWithOptions(
            auth_code, options, credential));

    // TODO(ewiseblatt): 20130301
    // Add an attribute to the flow where it will validate users.
    // If that's set then make another oauth2 call here to validate the user.
    // We'll need to add the oauth2 scope to the set of credentials so we
    // can make that oauth2 sevice call.
  }

  // Now that we have the request, execute it and write the result into the
  // credential store if successful.
  if (!refreshed_ok) {
    if (!token_request->Execute().ok()) {
      VLOG(1) << "Failed to get access token: "
              << token_request->http_response()->status().ToString();
      return token_request->http_response()->status();
    }
  }

  // If we have a user_id then record this and maybe store it.
  if (!options.user_id.empty()) {
    credential->set_user_id(options.user_id, false);
    if (credential_store_.get()) {
      // TODO(ewiseblatt): 20130301
      // if we havent verified the user_id yet, then attempt to do so first.
      util::Status status =
          credential_store_->Store(options.user_id, *credential);
      if (!status.ok()) {
        LOG(WARNING) << "Could not store credential: "
                     << status.error_message();
      }
    }
  }

  return StatusOk();
}

string OAuth2AuthorizationFlow::GenerateAuthorizationCodeRequestUrlWithOptions(
    const OAuth2RequestOptions& options) const {
  StringPiece default_redirect(client_spec_.redirect_uri());
  const StringPiece scopes =
      options.scopes.empty() ? default_scopes_ : options.scopes;
  const StringPiece redirect = options.redirect_uri.empty()
      ? client_spec_.redirect_uri()
      : options.redirect_uri;

  CHECK(!scopes.empty());
  CHECK(!client_spec_.client_id().empty()) << "client_id not set";

  return StrCat(client_spec_.auth_uri(),
                "?client_id=", EscapeForUrl(client_spec_.client_id()),
                "&redirect_uri=", EscapeForUrl(redirect),
                "&scope=", EscapeForUrl(scopes),
                "&response_type=code");
}

OAuth2TokenRequest*
OAuth2AuthorizationFlow::NewExchangeAuthorizationCodeRequestWithOptions(
    const StringPiece& authorization_code,
    const OAuth2RequestOptions& options,
    OAuth2Credential* credential) {
  return new OAuth2ExchangeAuthorizationCodeRequest(
      transport_.get(), authorization_code, client_spec_, options, credential);
}

OAuth2TokenRequest* OAuth2AuthorizationFlow::NewRefreshTokenRequest(
    OAuth2Credential* credential) {
  return new OAuth2RefreshTokenRequest(
      transport_.get(), &client_spec_, credential);
}

OAuth2TokenRequest* OAuth2AuthorizationFlow::NewRevokeRefreshTokenRequest(
    OAuth2Credential* credential) {
  return new OAuth2RevokeTokenRequest(
      transport_.get(), &client_spec_, credential->mutable_refresh_token());
}

OAuth2TokenRequest* OAuth2AuthorizationFlow::NewRevokeAccessTokenRequest(
    OAuth2Credential* credential) {
  return new OAuth2RevokeTokenRequest(
      transport_.get(), &client_spec_, credential->mutable_access_token());
}

// static
string OAuth2AuthorizationFlow::JoinScopes(const vector<StringPiece>& scopes) {
  return strings::Join(scopes, " ");
}

// static
OAuth2AuthorizationFlow*
OAuth2AuthorizationFlow::MakeFlowFromClientSecretsPath(
    const StringPiece& path,
    HttpTransport* transport,
    util::Status* status) {
  *status = SensitiveFileUtils::VerifyIsSecureFile(path.as_string(), false);
  if (!status->ok()) return NULL;

  string json;
*status = File::ReadPath(path.as_string(), &json);
  if (!status->ok()) return NULL;

  return MakeFlowFromClientSecretsJson(json, transport, status);
}

// static
OAuth2AuthorizationFlow*
OAuth2AuthorizationFlow::MakeFlowFromClientSecretsJson(
    const StringPiece& json,
    HttpTransport* transport,
    util::Status* status) {
  scoped_ptr<HttpTransport> transport_deleter(transport);
  if (!transport) {
    *status = StatusInvalidArgument("No transport instance provided");
    return NULL;
  }

  SimpleJsonData data;
  string root_name = data.InitFromContainer(json);
  if (root_name.empty()) {
    *status = StatusInvalidArgument("Invalid JSON");
    return NULL;
  }

  scoped_ptr<OAuth2AuthorizationFlow> flow;

  if (StringCaseEqual(root_name, "installed")) {
    flow.reset(
        new OAuth2InstalledApplicationFlow(transport_deleter.release()));
  } else if (StringCaseEqual(root_name, "web")) {
    flow.reset(new OAuth2WebApplicationFlow(transport_deleter.release()));
  } else {
    *status =
        StatusInvalidArgument(StrCat("Unhandled OAuth2 flow=", root_name));
    return NULL;
  }

  *status = flow->InitFromJsonData(&data);
  if (status->ok()) {
    return flow.release();
  }
  return NULL;
}

util::Status OAuth2AuthorizationFlow::InitFromJson(const StringPiece& json) {
  SimpleJsonData data;
  util::Status status = data.Init(json);
  if (!status.ok()) return status;
  return InitFromJsonData(&data);
}

util::Status OAuth2AuthorizationFlow::InitFromJsonData(
     const OAuth2AuthorizationFlow::SimpleJsonData* data) {
  OAuth2ClientSpec* spec = mutable_client_spec();

  string value;
  if (data->GetString("client_id", &value)) {
    spec->set_client_id(value);
  }
  if (data->GetString("client_secret", &value)) {
    spec->set_client_secret(value);
  }
  if (data->GetString("auth_uri", &value)) {
    spec->set_auth_uri(value);
  }
  if (data->GetString("token_uri", &value)) {
    spec->set_token_uri(value);
  }
  if (data->GetFirstArrayElement("redirect_uris", &value)) {
    spec->set_redirect_uri(value);
  }

  return StatusOk();
}

OAuth2InstalledApplicationFlow::OAuth2InstalledApplicationFlow(
    HttpTransport* transport)
    : OAuth2AuthorizationFlow(transport) {
}

OAuth2InstalledApplicationFlow::~OAuth2InstalledApplicationFlow() {
}

string OAuth2InstalledApplicationFlow::GenerateAuthorizationCodeRequestUrl(
    const StringPiece& scope) const {
  return OAuth2AuthorizationFlow::GenerateAuthorizationCodeRequestUrl(
      scope.as_string());
}

util::Status OAuth2InstalledApplicationFlow::InitFromJsonData(
     const OAuth2AuthorizationFlow::SimpleJsonData* data) {
  return OAuth2AuthorizationFlow::InitFromJsonData(data);
}


OAuth2WebApplicationFlow::OAuth2WebApplicationFlow(
    HttpTransport* transport)
    : OAuth2AuthorizationFlow(transport),
      offline_access_type_(false), force_approval_prompt_(false) {
}

OAuth2WebApplicationFlow::~OAuth2WebApplicationFlow() {
}

string OAuth2WebApplicationFlow::GenerateAuthorizationCodeRequestUrl(
    const StringPiece& scope) const {
  string url =
      OAuth2AuthorizationFlow::GenerateAuthorizationCodeRequestUrl(scope);
  if (force_approval_prompt_) {
    StrAppend(&url, "&approval_prompt=force");
  }
  if (offline_access_type_) {
    StrAppend(&url, "&access_type=offline");
  }
  return url;
}

util::Status OAuth2WebApplicationFlow::InitFromJsonData(
     const OAuth2AuthorizationFlow::SimpleJsonData* data) {
  util::Status status = OAuth2AuthorizationFlow::InitFromJsonData(data);

  if (status.ok()) {
    string value;
    if (data->GetString("access_type", &value)) {
      offline_access_type_ = (value == "offline");
      if (!offline_access_type_ && value != "online") {
        return StatusInvalidArgument(
            StrCat("Invalid access_type=", value));
      }
    }
    if (data->GetString("approval_prompt", &value)) {
      force_approval_prompt_ = (value == "force");
      if (!force_approval_prompt_ && value != "auto") {
        return StatusInvalidArgument(
            StrCat("Invalid approval_prompt=", value));
      }
    }
  }

  return status;
}

}  // namespace client

} // namespace googleapis