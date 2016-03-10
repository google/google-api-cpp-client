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
// An InstalledApplication handles boiler-plate framework code
// for the sample applications. It is responsible for managing
// the OAuth2 objects. Normally the InstalledServiceApplication
// subclass is used to setup and manage the ClientService.
//
// What makes this an installed application is that it assumes
// there is only one user so credentials are not scoped to users.
//
// TODO(user): 20130110
// It's probably worth folding this into the SDK but I want to
// explore it a bit more first. Especially to generalize it
// so that it has a sibling class WebApplication.
//
#ifndef GOOGLEAPIS_SAMPLES_INSTALLED_APPLICATION_H_
#define GOOGLEAPIS_SAMPLES_INSTALLED_APPLICATION_H_

#include <iostream>
using std::cout;
using std::endl;
using std::ostream;  // NOLINT
#include <memory>
#include <string>
using std::string;
#include <vector>

#include "googleapis/client/auth/oauth2_authorization.h"
#include "googleapis/client/auth/webserver_authorization_getter.h"
#include "googleapis/client/transport/http_transport.h"
#include "googleapis/client/util/status.h"
#include <glog/logging.h>
#include "googleapis/base/macros.h"
namespace googleapis {

// Maybe this will folded into the toolkit, but for now it's outside.
namespace sample {
using client::AbstractWebServer;
using client::OAuth2Credential;
using client::OAuth2AuthorizationFlow;
using client::HttpTransport;
using client::HttpTransportFactory;

// An InstalledApplication manages the OAuth2 setup and credentials
// for installed applications. What makes it specific for installed
// applications is that it assumes one user for everything.
//
// TODO(user): 20130118
// Eventually need way to convey errors back to application so they can
// choose how to communicate them.
class InstalledApplication {
 public:
  // Construct an installed application instance.
  // name is the name of our client application for logging
  // and tracing purposes. It has no semantic meaning.
  //
  // The caller must call Init() to finish initializing the instance
  // before using it.
  explicit InstalledApplication(const string& name);
  virtual ~InstalledApplication();

  // Returns the name this instance was constructed with.
  const string& name() const { return name_; }

  // Takes ownership of the credential.
  //
  // This call performs a lazy initialization of the credential if it was not
  // already explicitly set. If you are using this in multiple threads, you
  // should first ensure the credential is created (either ResetCredential or
  // getting credential() to force the lazy initialization) to avoid the
  // race condition.
  void ResetCredential(OAuth2Credential* credential);
  OAuth2Credential* credential();

  // Returns the OAuth2 flow created during Init()
  OAuth2AuthorizationFlow* flow() { return flow_.get(); }

  const std::vector<string>& default_oauth2_scopes() const {
    return default_scopes_;
  }

  std::vector<string>* mutable_default_oauth2_scopes() {
    return &default_scopes_;
  }

  // If true then the service credentials will be revoked when this application
  // is destroyed (i.e. application exists). This is not standard behavior
  // however may be convienent for tests and trials.
  void set_revoke_token_on_exit(bool on) { revoke_on_exit_ = on; }
  bool revoke_token_on_exit() const      { return revoke_on_exit_; }
  const string& user_name() const        { return user_name_; }

  // Change to the given user_name persona.
  // This will clear the current credential and load a new one.
  virtual googleapis::util::Status ChangeUser(const string& user_name);

  // Sets up flow and stuff. If you arent using a client_secrets file then
  // you at least need to create one defining the flow type then set the
  // Oauth2ClientSpec in flow()->mutable_client_spec().
  googleapis::util::Status Init(const string& client_secrets_path);

  // This isnt needed if the client_secrets path has a refresh token in it.
  // But if it doesnt, or was revoked, then you'll need to obtain another one.
  // This method assists that.
  virtual googleapis::util::Status AuthorizeClient();
  virtual googleapis::util::Status RevokeClient();

  virtual googleapis::util::Status StartupHttpd(
      int port, const string& path,
      client::WebServerAuthorizationCodeGetter::AskCallback* ask);
  virtual void ShutdownHttpd();

  const client::HttpTransportLayerConfig* config() const {
    return config_.get();
  }

  client::HttpTransportLayerConfig* mutable_config() {
    return config_.get();
  }


 protected:
  // Hook for derived classes to augment Init()
  virtual googleapis::util::Status InitHelper();


 private:
  string name_;                                // Only for logging and tracing
  string user_name_;                           // User owning credential.
  // Credentials for implied user.
  std::unique_ptr<OAuth2Credential> credential_;
  std::unique_ptr<OAuth2AuthorizationFlow> flow_;   // The OAuth2 flow
  std::vector<string> default_scopes_;         // When creating credentials
  std::unique_ptr<client::HttpTransportLayerConfig> config_;

  std::unique_ptr<AbstractWebServer> httpd_;        // Webserver for getter_
  std::unique_ptr<client::WebServerAuthorizationCodeGetter>
    authorization_code_getter_;
  bool revoke_on_exit_;

  DISALLOW_COPY_AND_ASSIGN(InstalledApplication);
};


// An installed application client to a specific service.
template<class SERVICE>
class InstalledServiceApplication : public InstalledApplication {
 public:
  explicit InstalledServiceApplication(const string& name)
      : InstalledApplication(name) {
  }
  virtual ~InstalledServiceApplication() {}

  SERVICE* service() { return service_.get(); }

 protected:
  // Hook for derived classes to augment Init().
  // This is instead of using InitHelper() offered by the base class.
  virtual googleapis::util::Status InitServiceHelper() {
    return client::StatusOk();
  }

  // Derived classes should override InitServiceHelper instead.
  virtual googleapis::util::Status InitHelper() {
    HttpTransportFactory* factory = config()->default_transport_factory();
    // This shouldn't happen since the Init method calling
    // this already checks.
    CHECK(factory);

    HttpTransport* transport = factory->New();
    service_.reset(new SERVICE(transport));

    googleapis::util::Status status = InitServiceHelper();
    if (!status.ok()) {
      delete service_.release();
    }

    return status;
  }

 private:
  std::unique_ptr<SERVICE> service_;

  DISALLOW_COPY_AND_ASSIGN(InstalledServiceApplication);
};

}  // namespace sample

}  // namespace googleapis
#endif  // GOOGLEAPIS_SAMPLES_INSTALLED_APPLICATION_H_
