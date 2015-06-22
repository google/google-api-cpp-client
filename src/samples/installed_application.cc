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

#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <iostream>
using std::cout;
using std::endl;
using std::ostream;  // NOLINT
#include "googleapis/config.h"

#include "samples/installed_application.h"

#include "googleapis/client/auth/file_credential_store.h"
#include "googleapis/client/auth/oauth2_authorization.h"
#ifdef googleapis_HAVE_OPENSSL
#include "googleapis/client/data/openssl_codec.h"
#endif
#include "googleapis/client/transport/curl_http_transport.h"
#include "googleapis/client/transport/http_transport.h"
#include "googleapis/client/util/mongoose_webserver.h"
#include "googleapis/client/util/status.h"

#include <glog/logging.h>
#include "googleapis/strings/strcat.h"

namespace googleapis {

using std::endl;
using std::istream;
using std::ostream;

using client::CredentialStore;
using client::FileCredentialStoreFactory;
using client::MongooseWebServer;
using client::OAuth2AuthorizationFlow;
using client::OAuth2ClientSpec;
using client::OAuth2Credential;
using client::OAuth2RequestOptions;
#ifdef googleapis_HAVE_OPENSSL
using client::OpenSslCodecFactory;
#endif
using client::StatusCanceled;
using client::StatusInvalidArgument;
using client::StatusOk;
using client::StatusUnknown;
using client::WebServerAuthorizationCodeGetter;

static googleapis::util::Status PromptShellForAuthorizationCode(
    OAuth2AuthorizationFlow* flow,
    const client::OAuth2RequestOptions& options,
    string* authorization_code) {
  // TODO(user): Normally one would not get from the commandline,
  // rather you would do something interactive within their browser/display.
  string url = flow->GenerateAuthorizationCodeRequestUrlWithOptions(options);

  std::cout << "Enter the following url into a browser:\n" << url << std::endl;
  std::cout << "Enter the browser's response: ";

  authorization_code->clear();
  std::cin >> *authorization_code;
  if (authorization_code->empty()) {
    return StatusCanceled("Canceled");
  } else {
    return StatusOk();
  }
}

static googleapis::util::Status ValidateUserName(const string& name) {
  if (name.find("/") != string::npos) {
    return StatusInvalidArgument("UserNames cannot contain '/'");
  } else if (name == "." || name == "..") {
    return StatusInvalidArgument(
        StrCat("'", name, "' is not a valid user name"));
  }
  return StatusOk();
}

namespace sample {

InstalledApplication::InstalledApplication(const string& name)
    : name_(name) , user_name_(""), revoke_on_exit_(false) {
  config_.reset(new client::HttpTransportLayerConfig);
  client::HttpTransportFactory* factory =
      new client::CurlHttpTransportFactory(config_.get());
  config_->ResetDefaultTransportFactory(factory);
}

InstalledApplication::~InstalledApplication() {
  if (revoke_on_exit_) {
    VLOG(1) << "Revoking access on exit";
    googleapis::util::Status status = flow_->PerformRevokeToken(true, credential_.get());
    if (!status.ok()) {
      LOG(ERROR) << "Error revoking access token: " << status.error_message();
    }
  }
}

util::Status InstalledApplication::ChangeUser(const string& user_name) {
  if (user_name != user_name_) {
    string proposed_name = user_name;
    googleapis::util::Status status = ValidateUserName(proposed_name);
    if (!status.ok()) return status;

    credential_.reset(NULL);

    // Use the application name instead of its client_id
    if (!status.ok()) {
      string error =
          StrCat(status.ToString(),
                 ": Could not change to username=", proposed_name,
                 ". You are no longer an authorized user.\n");
      LOG(WARNING) << error;
      return StatusUnknown(error);
    }
    user_name_ = proposed_name;
    std::cout << "Changed to username=" << user_name_ << endl;
  }
  return StatusOk();
}

OAuth2Credential* InstalledApplication::credential() {
  if (!credential_.get()) {
    credential_.reset(flow_->NewCredential());
  }
  return credential_.get();
}

void InstalledApplication::ResetCredential(
    OAuth2Credential* credential) {
  VLOG(1) << "Resetting credential";
  credential_.reset(credential);
}

util::Status InstalledApplication::InitHelper() { return StatusOk(); }

util::Status InstalledApplication::Init(const string& secrets_path) {
  googleapis::util::Status status;

  HttpTransport* transport = config_->NewDefaultTransport(&status);

  if (!transport) {
    return status;
  }

  flow_.reset(OAuth2AuthorizationFlow::MakeFlowFromClientSecretsPath(
      secrets_path, transport, &status));
  if (!status.ok()) {
    return status;
  }

  // A derived class can always change this again later after init if it has
  // an embedded server to redirect to.
  flow_->mutable_client_spec()->set_redirect_uri(
       OAuth2AuthorizationFlow::kOutOfBandUrl);
  flow_->set_authorization_code_callback(
      NewPermanentCallback(&PromptShellForAuthorizationCode, flow_.get()));
  flow_->set_check_email(true);

#ifdef googleapis_HAVE_OPENSSL
  OpenSslCodecFactory* openssl_factory = new OpenSslCodecFactory;
  status = openssl_factory->SetPassphrase(
      flow_->client_spec().client_secret());
  if (status.ok()) {
    string home_path;
    status = FileCredentialStoreFactory::GetSystemHomeDirectoryStorePath(
        &home_path);
    if (status.ok()) {
      FileCredentialStoreFactory store_factory(home_path);
      store_factory.set_codec_factory(openssl_factory);
      flow_->ResetCredentialStore(
          store_factory.NewCredentialStore(name_, &status));
    }
  }
  if (!status.ok()) {
    LOG(ERROR) << "Could not use credential store: "
               << status.error_message() << endl;
  }
#endif

  status = InitHelper();
  if (!status.ok()) {
    delete flow_.release();
  }

  return status;
}

util::Status InstalledApplication::AuthorizeClient() {
  OAuth2RequestOptions options;
  options.scopes = OAuth2AuthorizationFlow::JoinScopes(default_oauth2_scopes());
  options.email = user_name_;
  googleapis::util::Status status =
        flow_->RefreshCredentialWithOptions(options, credential());
  if (!status.ok()) {
    std::cout << status.error_message() << endl;
  }
  return status;
}

util::Status InstalledApplication::RevokeClient() {
  return flow_->PerformRevokeToken(true, credential_.get());
}

util::Status InstalledApplication::StartupHttpd(
     int port, const string& path,
     WebServerAuthorizationCodeGetter::AskCallback* asker) {
  if (path.at(0) != '/') {
    return StatusInvalidArgument(
        StrCat("Path must be absolute. got path=", path));
  }
  if (port <= 0) {
    return StatusInvalidArgument(StrCat("Invalid port=", port));
  }

  httpd_.reset(new MongooseWebServer(port));
  authorization_code_getter_.reset(
      new WebServerAuthorizationCodeGetter(asker));
  authorization_code_getter_->AddReceiveAuthorizationCodeUrlPath(
      path, httpd_.get());
  googleapis::util::Status status = httpd_->Startup();
  if (status.ok()) {
    // Change the flow so that it uses a browser and httpd server.
    flow_->mutable_client_spec()->set_redirect_uri(
        httpd_->MakeEndpointUrl(true, path));

    flow_->set_authorization_code_callback(
        NewPermanentCallback(
            authorization_code_getter_.get(),
            &WebServerAuthorizationCodeGetter::PromptForAuthorizationCode,
            flow_.get()));
  }

  return status;
}

void InstalledApplication::ShutdownHttpd() {
  if (httpd_.get()) {
    httpd_->Shutdown();
    httpd_.reset(NULL);
  }

  if (authorization_code_getter_.get()) {
    delete authorization_code_getter_.release();
    // Change the flow so that it uses the shell.
    flow_->mutable_client_spec()->set_redirect_uri(
        OAuth2AuthorizationFlow::kOutOfBandUrl);
    flow_->set_authorization_code_callback(
        NewPermanentCallback(&PromptShellForAuthorizationCode, flow_.get()));
  }
}

}  // namespace sample

}  // namespace googleapis
