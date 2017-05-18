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


#include <fstream>
#include <memory>
#include <sstream>
#include <string>
using std::string;
#include <vector>

#include "googleapis/base/callback.h"
#include <glog/logging.h>
#include "googleapis/base/mutex.h"
#include "googleapis/client/auth/credential_store.h"
#include "googleapis/client/auth/oauth2_authorization.h"
#include "googleapis/client/data/data_reader.h"
#include "googleapis/client/transport/http_request.h"
#include "googleapis/client/transport/http_response.h"
#include "googleapis/client/transport/http_transport.h"
#include "googleapis/client/transport/http_types.h"
#include "googleapis/strings/case.h"
#include "googleapis/client/util/date_time.h"
#include "googleapis/client/util/escaping.h"
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

#include "googleapis/strings/join.h"
#include "googleapis/strings/numbers.h"
#include "googleapis/strings/split.h"
#include "googleapis/strings/strcat.h"
#include "googleapis/strings/stringpiece.h"
#include "googleapis/strings/util.h"

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

void OAuth2AuthorizationFlow::ResetCredentialStore(CredentialStore* store) {
  credential_store_.reset(store);
}

class OAuth2AuthorizationFlow::SimpleJsonData {
 public:
  SimpleJsonData() {}
  googleapis::util::Status Init(const string& json);
  string InitFromContainer(const string& json);
  bool GetString(const char* field, string* value) const;
  bool GetScalar(const char* field, int* value) const;
  bool GetBool(const char* field, bool* value) const;

  bool GetFirstArrayElement(const char* field, string* value) const;

 private:
  mutable Json::Value json_;
};

util::Status OAuth2AuthorizationFlow::SimpleJsonData::Init(
     const string& json) {
  Json::Reader reader;
  std::istringstream stream(json);
  if (!reader.parse(stream, json_)) {
    googleapis::util::Status status(StatusInvalidArgument("Invalid JSON"));
    LOG(ERROR) << status.error_message();
    return status;
  }
  return StatusOk();
}
string OAuth2AuthorizationFlow::SimpleJsonData::InitFromContainer(
     const string& json) {
  if (!Init(json).ok() || json_.begin() == json_.end()) {
    return "";
  }
  string name = json_.begin().key().asString();
  json_ = *json_.begin();
  return name;
}
bool OAuth2AuthorizationFlow::SimpleJsonData::GetString(
    const char* field_name, string* value) const {
  if (!json_.isObject())
    return false;
  Json::Value nullValue;
  Json::Value result = json_.get(field_name, nullValue);
  if (!result)
    return false;
  *value = result.asString();
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
  if (!json_.isArray()) return false;
  if (!json_.isMember(field_name)) return false;

  Json::Value array = json_[field_name];
  if (array.size() == 0) return false;
  *value = (*array.begin()).asString();
  return true;
}

void OAuth2AuthorizationFlow::AppendJsonStringAttribute(
    string* to,
    const string& sep,
    const string& name,
    const string& value) {
  StrAppend(to, sep, "\"", name, "\":\"", value, "\"");
}

void OAuth2AuthorizationFlow::AppendJsonScalarAttribute(
    string* to,
    const string& sep,
    const string& name,
    int value) {
  StrAppend(to, sep, "\"", name, "\":", value);
}

bool OAuth2AuthorizationFlow::GetStringAttribute(
    const SimpleJsonData* data, const char* key, string* value) {
  return data->GetString(key, value);
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

const char OAuth2Credential::kOAuth2CredentialType[] = "OAuth2";

OAuth2Credential::OAuth2Credential()
    : flow_(NULL), email_verified_(false) {
  // If we dont know an expiration then assume it never will.
  expiration_timestamp_secs_.set(kint64max);
}

OAuth2Credential::~OAuth2Credential() {
}

const string OAuth2Credential::type() const {
  return kOAuth2CredentialType;
}

void OAuth2Credential::Clear() {
  access_token_.clear();
  refresh_token_.clear();
  expiration_timestamp_secs_.set(kint64max);
  email_.clear();
  email_verified_ = false;
}

util::Status OAuth2Credential::Refresh() {
  if (!flow_) {
    return StatusFailedPrecondition("No flow bound.");
  }
  return flow_->PerformRefreshToken(OAuth2RequestOptions(), this);
}

void OAuth2Credential::RefreshAsync(Callback1<util::Status>* callback) {
  if (!flow_) {
    callback->Run(StatusFailedPrecondition("No flow bound."));
    return;
  }
  flow_->PerformRefreshTokenAsync(OAuth2RequestOptions(), this, callback);
}

util::Status OAuth2Credential::Load(DataReader* reader) {
  Clear();
  return Update(reader);
}

util::Status OAuth2Credential::Update(DataReader* reader) {
  string json = reader->RemainderToString();
  if (reader->error()) {
    googleapis::util::Status status(util::error::UNKNOWN, "Invalid credential");
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
    OAuth2AuthorizationFlow::AppendJsonStringAttribute(
        &json, sep, "access_token", access_token);
    sep = ",";
  }
  if (!refresh_token.empty()) {
    OAuth2AuthorizationFlow::AppendJsonStringAttribute(
        &json, sep, "refresh_token", refresh_token);
    sep = ",";
  }
  if (expires_at != kint64max) {
    OAuth2AuthorizationFlow::AppendJsonStringAttribute(
        &json, sep, "expires_at", SimpleItoa(expires_at));
    sep = ",";
  }
  if (!email_.empty()) {
    OAuth2AuthorizationFlow::AppendJsonStringAttribute(
        &json, sep, "email", email_);
    // OAuth returns this bool as "true"/"false" string, not a bool.
    // We'll keep it that way too for consistency.
    OAuth2AuthorizationFlow::AppendJsonStringAttribute(
        &json, sep, "email_verified", email_verified_ ? "true" : "false");
  }
  json.append("}");
  return NewManagedInMemoryDataReader(json);
}

util::Status OAuth2Credential::AuthorizeRequest(HttpRequest* request) {
  if (!access_token_.empty()) {
    string bearer = "Bearer ";
    access_token_.AppendTo(&bearer);
    VLOG(4) << HttpRequest::HttpHeader_AUTHORIZATION << ": " << bearer;
    request->AddHeader(HttpRequest::HttpHeader_AUTHORIZATION, bearer);
  }
  return StatusOk();
}

util::Status OAuth2Credential::UpdateFromString(const string& json) {
  OAuth2AuthorizationFlow::SimpleJsonData data;
  googleapis::util::Status status = data.Init(json);
  if (!status.ok()) return status;

  string str_value;
  int int_value;

  if (data.GetString("refresh_token", &str_value)) {
    VLOG(1) << "Updating refresh token";
    refresh_token_.set(str_value);
  }
  if (data.GetString("access_token", &str_value)) {
    access_token_.set(str_value);
    VLOG(1) << "Updating access token";
  }
  if (data.GetString("expires_at", &str_value)
      || data.GetString("exp", &str_value)) {
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
  if (data.GetString("email", &str_value)) {
    string bool_str;
    // Read this as a string because OAuth2 server returns it as
    // a true/false string.
    data.GetString("email_verified", &bool_str);

    email_ = str_value;
    email_verified_ = bool_str == "true";
  }

  if (data.GetString("id_token", &str_value)) {
    // Extract additional stuff from the JWT claims.
    // We dont need to verify the signature since we already know
    // this is coming from the OAuth2 server and is secure with https.
    // see https://developers.google.com/accounts/docs/OAuth2Login
    //     #validatinganidtoken
    int dot_positions[3];
    int n_dots = 0;
    for (size_t i = 0; i < str_value.size(); ++i) {
      if (str_value[i] == '.') {
        dot_positions[n_dots] = i;
        ++n_dots;
        if (n_dots == 3) break;
      }
    }
    if (n_dots != 2) {
      return StatusUnknown("Invalid id_token attribute - not a JWT");
    }
    string claims;
    const char *claims_start = str_value.data() + dot_positions[0] + 1;
    size_t claims_len = dot_positions[1] - dot_positions[0] - 1;
    if (!googleapis_util::Base64Unescape(claims_start, claims_len, &claims)) {
      return StatusUnknown("id_token claims not base-64 encoded");
    }
    return UpdateFromString(claims);
  }

  return StatusOk();
}


OAuth2AuthorizationFlow::OAuth2AuthorizationFlow(HttpTransport* transport)
    : check_email_(false), transport_(transport) {
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

util::Status OAuth2AuthorizationFlow::PerformExchangeAuthorizationCode(
    const string& authorization_code,
    const OAuth2RequestOptions& options,
    OAuth2Credential* credential) {
  if (authorization_code.empty()) {
    return StatusInvalidArgument("Missing authorization code");
  }
  if (client_spec_.client_id().empty()) {
    return StatusFailedPrecondition("Missing client ID");
  }
  if (client_spec_.client_secret().empty()) {
    return StatusFailedPrecondition("Missing client secret");
  }

  const string redirect = options.redirect_uri.empty()
      ? client_spec_.redirect_uri()
      : options.redirect_uri;
  string content =
      StrCat("code=", EscapeForUrl(authorization_code),
             "&client_id=", EscapeForUrl(client_spec_.client_id()),
             "&client_secret=", EscapeForUrl(client_spec_.client_secret()),
             "&redirect_uri=", EscapeForUrl(redirect),
             "&grant_type=authorization_code");

  std::unique_ptr<HttpRequest> request(
      transport_->NewHttpRequest(HttpRequest::POST));
  if (options.timeout_ms > 0) {
    request->mutable_options()->set_timeout_ms(options.timeout_ms);
  }
  request->set_url(client_spec_.token_uri());
  request->set_content_type(HttpRequest::ContentType_FORM_URL_ENCODED);
  request->set_content_reader(NewUnmanagedInMemoryDataReader(content));

  googleapis::util::Status status = request->Execute();
  if (status.ok()) {
    status = credential->Update(request->response()->body_reader());
    if (status.ok() && check_email_ && !options.email.empty()) {
      if (options.email != credential->email()) {
        status = StatusUnknown(
            StrCat("Credential email address mismatch. Expected [",
                   options.email, "] but got [", credential->email(), "]"));
        credential->Clear();
      }
    }
  }
  return status;
}

util::Status OAuth2AuthorizationFlow::PerformRefreshToken(
     const OAuth2RequestOptions& options, OAuth2Credential* credential) {
  googleapis::util::Status tokenStatus = ValidateRefreshToken_(credential);
  if (!tokenStatus.ok()) {
    return tokenStatus;
  }

  std::unique_ptr<HttpRequest> request(
      ConstructRefreshTokenRequest_(options, credential));

  googleapis::util::Status status = request->Execute();
  if (status.ok()) {
    status = credential->Update(request->response()->body_reader());
  }

  if (!status.ok()) {
    LOG(ERROR) << "Refresh failed with " << status.error_message();
  }

  return status;
}

void OAuth2AuthorizationFlow::PerformRefreshTokenAsync(
    const OAuth2RequestOptions& options,
    OAuth2Credential* credential,
    Callback1<util::Status>* callback) {
  googleapis::util::Status status = ValidateRefreshToken_(credential);
  if (!status.ok()) {
    callback->Run(status);
    return;
  }

  HttpRequest* request = ConstructRefreshTokenRequest_(options, credential);

  HttpRequestCallback* cb =
      NewCallback(this, &OAuth2AuthorizationFlow::UpdateCredentialAsync,
                  credential, callback);

  request->DestroyWhenDone();
  request->ExecuteAsync(cb);
}

HttpRequest* OAuth2AuthorizationFlow::ConstructRefreshTokenRequest_(
    const OAuth2RequestOptions& options,
    OAuth2Credential* credential) {
  HttpRequest* request(transport_->NewHttpRequest(HttpRequest::POST));
  if (options.timeout_ms > 0) {
    request->mutable_options()->set_timeout_ms(options.timeout_ms);
  }
  request->set_url(client_spec_.token_uri());
  request->set_content_type(HttpRequest::ContentType_FORM_URL_ENCODED);

  string* content = BuildRefreshTokenContent_(credential);
  request->set_content_reader(NewManagedInMemoryDataReader(content));
  return request;
}

util::Status OAuth2AuthorizationFlow::ValidateRefreshToken_(
    OAuth2Credential* credential) const {
  if (client_spec_.client_id().empty()) {
    return StatusFailedPrecondition("Missing client ID");
  }
  if (client_spec_.client_secret().empty()) {
    return StatusFailedPrecondition("Missing client secret");
  }
  if (credential->refresh_token().empty()) {
    return StatusInvalidArgument("Missing refresh token");
  }
  return StatusOk();
}

string* OAuth2AuthorizationFlow::BuildRefreshTokenContent_(
    OAuth2Credential* credential) {
  string* content =
      new string(
          StrCat("client_id=", client_spec_.client_id(),
                 "&client_secret=", client_spec_.client_secret(),
                 "&grant_type=refresh_token",
                 "&refresh_token="));
  credential->refresh_token().AppendTo(content);
  return content;
}

void OAuth2AuthorizationFlow::UpdateCredentialAsync(
    OAuth2Credential* credential,
    Callback1<util::Status>* callback,
    HttpRequest* request) {
  googleapis::util::Status status = request->response()->status();
  if (status.ok()) {
    status = credential->Update(request->response()->body_reader());
  }

  if (!status.ok()) {
    LOG(ERROR) << "Refresh failed with " << status.error_message();
  }

  if (NULL != callback) {
    callback->Run(status);
  }
}

util::Status OAuth2AuthorizationFlow::PerformRevokeToken(
     bool access_token_only, OAuth2Credential* credential) {
  std::unique_ptr<HttpRequest> request(
      transport_->NewHttpRequest(HttpRequest::POST));
  request->set_url(client_spec_.revoke_uri());
  request->set_content_type(HttpRequest::ContentType_FORM_URL_ENCODED);

  ThreadsafeString* token =
      access_token_only
      ? credential->mutable_access_token()
      : credential->mutable_refresh_token();
  string* content = new string("token=");
  token->AppendTo(content);
  request->set_content_reader(NewManagedInMemoryDataReader(content));
  googleapis::util::Status status = request->Execute();
  if (status.ok()) {
    token->set("");
  }
  return status;
}

util::Status OAuth2AuthorizationFlow::RefreshCredentialWithOptions(
     const OAuth2RequestOptions& options, OAuth2Credential* credential) {
  string refresh_token = credential->refresh_token().as_string();

  if (refresh_token.empty() && credential_store_.get()) {
    // If we dont have a refresh token, try reloading from the store.
    // This could because we havent yet loaded the credential.
    // If this fails, we'll just handle it as a first-time case.
    googleapis::util::Status status =
        credential_store_->InitCredential(options.email, credential);
    if (status.ok()) {
      if (check_email_ && credential->email() != options.email) {
        status = StatusUnknown(
            StrCat(
                "Stored credential email address mismatch. Expected [",
                options.email, "] but got [", credential->email(), "]"));
        credential->Clear();
      } else if (credential->email().empty()) {
        credential->set_email(options.email, false);
      }
      if (status.ok()) {
        refresh_token = credential->refresh_token().as_string();
      }
    }
  }

  // Default status is not ok, meaning we did not make any attempts yet.
  googleapis::util::Status refresh_status = StatusUnknown("Do not have authorization");
  if (!refresh_token.empty()) {
    if (options.email != credential->email()) {
      string error = "Email does not match credential's email";
      LOG(ERROR) << error;
      return StatusInvalidArgument(error);
    }

    // Maybe this will be ok, maybe not. If not we'll continue as if we
    // never had a refresh token in case it is invalid or revoked.
    refresh_status = PerformRefreshToken(options, credential);

    // Try executing this request. If it fails, we'll continue as if we
    // did not find the token.
    if (!refresh_status.ok()) {
      LOG(ERROR) << "Could not refresh existing credential: "
                 << refresh_status.error_message() << "\n"
                 << "Trying to obtain a new one instead.";
    }
  }

  if (refresh_status.ok()) {
    // Do nothing until common code below.
  } else if (!authorization_code_callback_.get()) {
    const char error[] = "No prompting mechanism provided to get authorization";
    LOG(ERROR) << error;
    return StatusUnimplemented(error);
  } else {
    // If we still dont have a credential then we need to kick off
    // authorization again to get an access (and refresh) token.
    string auth_code;
    OAuth2RequestOptions actual_options = options;
    if (actual_options.scopes.empty()) {
      actual_options.scopes = default_scopes_;
    }
    if (actual_options.redirect_uri.empty()) {
      actual_options.redirect_uri = client_spec_.redirect_uri();
    }
    googleapis::util::Status status =
          authorization_code_callback_->Run(actual_options, &auth_code);
    if (!status.ok()) return status;

    refresh_status =
        PerformExchangeAuthorizationCode(auth_code, options, credential);

    // TODO(user): 20130301
    // Add an attribute to the flow where it will validate users.
    // If that's set then make another oauth2 call here to validate the user.
    // We'll need to add the oauth2 scope to the set of credentials so we
    // can make that oauth2 sevice call.
  }

  // Now that we have the request, execute it and write the result into the
  // credential store if successful.
  if (refresh_status.ok()
      && !options.email.empty()) {
    credential->set_email(options.email, false);
    if (credential_store_.get()) {
      // TODO(user): 20130301
      // if we havent verified the email yet, then attempt to do so first.
      googleapis::util::Status status =
          credential_store_->Store(options.email, *credential);
      if (!status.ok()) {
        LOG(WARNING) << "Could not store credential: "
                     << status.error_message();
      }
    }
  }

  return refresh_status;
}

string OAuth2AuthorizationFlow::GenerateAuthorizationCodeRequestUrlWithOptions(
    const OAuth2RequestOptions& options) const {
  string default_redirect(client_spec_.redirect_uri());
  string actual_scopes;
  string scopes =
      options.scopes.empty() ? default_scopes_ : options.scopes;
  if (check_email_
      && !(scopes.find("email ") == 0)
      && scopes.find(" email") == string::npos) {
    // Add "email" scope if it isnt already present
    actual_scopes = StrCat("email ", scopes);
    scopes = actual_scopes;
  }

  const string redirect = options.redirect_uri.empty()
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

// static
string OAuth2AuthorizationFlow::JoinScopes(
    const std::vector<string>& scopes) {
  return strings::Join(scopes, " ");
}

// static
OAuth2AuthorizationFlow* OAuth2AuthorizationFlow::MakeFlowFromClientSecretsPath(
    const string& path, HttpTransport* transport,
    googleapis::util::Status* status) {
  string json(std::istreambuf_iterator<char>(std::ifstream(path).rdbuf()),
              std::istreambuf_iterator<char>());
  return MakeFlowFromClientSecretsJson(json, transport, status);
}

// static
OAuth2AuthorizationFlow*
OAuth2AuthorizationFlow::MakeFlowFromClientSecretsJson(
    const string& json,
    HttpTransport* transport,
    googleapis::util::Status* status) {
  std::unique_ptr<HttpTransport> transport_deleter(transport);
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

  std::unique_ptr<OAuth2AuthorizationFlow> flow;

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

util::Status OAuth2AuthorizationFlow::InitFromJson(const string& json) {
  SimpleJsonData data;
  util::Status status = data.Init(json);
  if (!status.ok())
    return status;

  return InitFromJsonData(&data);
}

util::Status OAuth2AuthorizationFlow::InitFromJsonData(
     const OAuth2AuthorizationFlow::SimpleJsonData* data) {
  OAuth2ClientSpec* spec = mutable_client_spec();

  string value;
  if (data->GetString("client_id", &value)) {
    spec->set_client_id(value);
    VLOG(4) << "client_id: " << value;
  }
  if (data->GetString("client_secret", &value)) {
    spec->set_client_secret(value);
    char secret_chars[] = { value[0], value[1], value[2], value[3], 0 };
    VLOG(4) << "client_secret: " << secret_chars << "...";
  }
  if (data->GetString("auth_uri", &value)) {
    spec->set_auth_uri(value);
    VLOG(4) << "auth_uri: " << value;
  }
  if (data->GetString("token_uri", &value)) {
    spec->set_token_uri(value);
    VLOG(4) << "token_uri: " << value;
  }
  if (data->GetFirstArrayElement("redirect_uris", &value)) {
    spec->set_redirect_uri(value);
    VLOG(4) << "redirect_uri: " << value;
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
    const string& scope) const {
  return OAuth2AuthorizationFlow::GenerateAuthorizationCodeRequestUrl(scope);
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
    const string& scope) const {
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
  googleapis::util::Status status = OAuth2AuthorizationFlow::InitFromJsonData(data);

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

void ThreadsafeString::set(const StringPiece& value) {
  MutexLock l(&mutex_);
  value_ = value.as_string();
}

}  // namespace client

}  // namespace googleapis
