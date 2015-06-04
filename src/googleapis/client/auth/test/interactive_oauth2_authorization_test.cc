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
// This is really an integration test using CurlHttpTransport,
// so it isnt really being unit tested in isolation.
//
// WARNING this test requires human interaction so is not applicable for TAP
#include <iostream>
using std::cout;
using std::endl;
using std::ostream;
#include <memory>
#include <string>
using std::string;

#include <gflags/gflags.h>
#include <glog/logging.h>
#include "googleapis/client/auth/oauth2_authorization.h"

#include "googleapis/client/auth/webserver_authorization_getter.h"
#include "googleapis/client/data/data_reader.h"
#include "googleapis/client/transport/curl_http_transport.h"
#include "googleapis/client/transport/http_request.h"
#include "googleapis/client/transport/http_response.h"
#include "googleapis/client/transport/http_transport.h"
#include "googleapis/client/util/mongoose_webserver.h"
#include "googleapis/client/util/status.h"
#include "googleapis/client/util/uri_utils.h"
#include "googleapis/util/file.h"
#include "googleapis/util/filesystem.h"
#include "googleapis/strings/join.h"
#include "googleapis/strings/util.h"
#include <gtest/gtest.h>

namespace googleapis {

DEFINE_int32(port, 1234,
             "Port for embedded httpd registered with Google APIs console");

// We're not using SSL by default to avoid failures due to the webserver
// not being configured for SSL support since this test is not intended to
// test the server configuration, only the client library behavior.
// Therefore the web client secrets should be registered with an http url,
// not https.
DEFINE_string(web_client_secrets_path,
"./web_client_secrets.json",
              "Path to clients secrets file for web flow");

DEFINE_string(installed_client_secrets_path,
"./installed_client_secrets.json",
              "Path to clients secrets file for installed flow");

DEFINE_bool(test_localhost, false, "Run localhost test");

using client::CurlHttpTransportFactory;
using client::HttpRequest;
using client::HttpResponse;
using client::HttpTransport;
using client::HttpTransportLayerConfig;
using client::HttpTransportOptions;
using client::MongooseWebServer;
using client::OAuth2AuthorizationFlow;
using client::OAuth2ClientSpec;
using client::OAuth2Credential;
using client::OAuth2RequestOptions;
using client::ParsedUrl;
using client::StatusDeadlineExceeded;
using client::StatusOk;
using client::StatusUnknown;
using client::WebServerAuthorizationCodeGetter;

const char kScope[] = "https://www.googleapis.com/auth/userinfo.profile"
                      " https://www.googleapis.com/auth/calendar.readonly"
                      " https://www.googleapis.com/auth/calendar";

const char kProtectedUrl[] = "https://www.googleapis.com/userinfo/v2/me";

static std::unique_ptr<HttpTransportLayerConfig> config_;
static std::unique_ptr<OAuth2AuthorizationFlow> installed_flow_;
static std::unique_ptr<OAuth2AuthorizationFlow> web_flow_;
static std::unique_ptr<OAuth2Credential> credential_;

class OAuth2TestFixture : public testing::Test {
 public:
  static void SetUpTestCase() {
    CHECK(!FLAGS_installed_client_secrets_path.empty());
    CHECK(!FLAGS_web_client_secrets_path.empty());
    CHECK_OK(
        file::Exists(FLAGS_installed_client_secrets_path, file::Defaults()))
        << "To run this test you must register an installed client with the "
        << "Google APIs Console then download the client secrets and set the "
        << "--installed_client_secrets_path. The current path is: "
        << FLAGS_installed_client_secrets_path;
    CHECK_OK(file::Exists(FLAGS_web_client_secrets_path, file::Defaults()))
        << "To run this test you must register an web client with the "
        << "Google APIs Console then download the client secrets and set the "
        << "--web_client_secrets_path. The current path is: "
        << FLAGS_web_client_secrets_path;

    config_.reset(new HttpTransportLayerConfig);
    config_->mutable_default_transport_options()->set_cacerts_path(
        HttpTransportOptions::kDisableSslVerification);
    config_->ResetDefaultTransportFactory(
        new CurlHttpTransportFactory(config_.get()));
    credential_.reset(new OAuth2Credential);

    googleapis::util::Status status;
    installed_flow_.reset(
        OAuth2AuthorizationFlow::MakeFlowFromClientSecretsPath(
            FLAGS_installed_client_secrets_path,
            config_->NewDefaultTransportOrDie(),
            &status));
    CHECK(status.ok()) << status.error_message();
    installed_flow_->set_default_scopes(kScope);
    web_flow_.reset(
        OAuth2AuthorizationFlow::MakeFlowFromClientSecretsPath(
            FLAGS_web_client_secrets_path,
            config_->NewDefaultTransportOrDie(),
            &status));
    web_flow_->set_default_scopes(kScope);
    CHECK(status.ok()) << status.error_message();
  }

  static googleapis::util::Status PromptShellForAuthorizationCode(
      OAuth2AuthorizationFlow* flow,
      const OAuth2RequestOptions& options,
      string* authorization_code) {
    string url = flow->GenerateAuthorizationCodeRequestUrl(kScope);
    std::cout << "Enter the following url into a browser:\n" << url << "\n";
    std::cout << "Now enter the browser's response: ";
    std::cin >> *authorization_code;
    return StatusOk();
  }

  void VerifyCredential(OAuth2Credential* credential) {
    std::unique_ptr<HttpTransport> transport(
        config_->NewDefaultTransportOrDie());
    std::unique_ptr<HttpRequest> http_request(
        transport->NewHttpRequest(HttpRequest::GET));
    http_request->set_url("https://www.googleapis.com/userinfo/v2/me");

    http_request->set_credential(credential);

    EXPECT_TRUE(http_request->Execute().ok());
    EXPECT_TRUE(http_request->response()->ok());
    if (http_request->response()->body_reader()) {
      LOG(INFO)
          << "Got "
          << http_request->response()->body_reader()->RemainderToString();
    }
  }

  void TestRefreshCredentials(OAuth2AuthorizationFlow* flow) {
    OAuth2Credential credential;

    OAuth2RequestOptions options;
    googleapis::util::Status status =
          flow->RefreshCredentialWithOptions(options, &credential);
    EXPECT_TRUE(status.ok()) << status.ToString();
    EXPECT_FALSE(credential.access_token().empty());
    VerifyCredential(&credential);
  }
};

TEST_F(OAuth2TestFixture, VerifyProtectedUrl) {
  // Just make sure the url we are using to verify the credential really
  // does require a credential.
  std::unique_ptr<HttpTransport> transport(config_->NewDefaultTransportOrDie());
  std::unique_ptr<HttpRequest> http_request(
      transport->NewHttpRequest(HttpRequest::GET));
  http_request->set_url(kProtectedUrl);
  EXPECT_FALSE(http_request->Execute().ok());
  EXPECT_EQ(401, http_request->response()->http_code());
}


TEST_F(OAuth2TestFixture, TestRedirectToOutOfBand) {
  googleapis::util::Status status;
  std::unique_ptr<OAuth2AuthorizationFlow> flow(
      OAuth2AuthorizationFlow::MakeFlowFromClientSecretsPath(
          FLAGS_installed_client_secrets_path,
          config_->NewDefaultTransportOrDie(),
          &status));
  CHECK(status.ok()) << status.error_message();
  flow->mutable_client_spec()->set_redirect_uri(
      OAuth2AuthorizationFlow::kOutOfBandUrl);
  flow->set_authorization_code_callback(
      NewPermanentCallback(
          &OAuth2TestFixture::PromptShellForAuthorizationCode, flow.get()));
  TestRefreshCredentials(flow.get());
}

TEST_F(OAuth2TestFixture, TestRefreshInstalledFlowCredential) {
  if (FLAGS_test_localhost) {
    WebServerAuthorizationCodeGetter getter(
        NewPermanentCallback(
            WebServerAuthorizationCodeGetter::PromptWithOstream,
            &std::cout,
            "Enter the following URL into a browser:\n$URL\n"));
    getter.set_timeout_ms(2 * 60 * 1000);  // 2 mins

    MongooseWebServer httpd(FLAGS_port);
    getter.AddReceiveAuthorizationCodeUrlPath("/oauth", &httpd);
    EXPECT_TRUE(httpd.Startup().ok());

    installed_flow_->mutable_client_spec()->set_redirect_uri(
        httpd.MakeEndpointUrl(true, "/oauth"));
    installed_flow_->set_authorization_code_callback(
        getter.MakeAuthorizationCodeCallback(installed_flow_.get()));
    TestRefreshCredentials(web_flow_.get());
  }
}

TEST_F(OAuth2TestFixture, TestRefreshWebFlowCredential) {
  WebServerAuthorizationCodeGetter getter(
      NewPermanentCallback(
          WebServerAuthorizationCodeGetter::PromptWithOstream,
          &std::cout,
          "Enter the following URL into a browser:\n$URL\n"));
  getter.set_timeout_ms(2 * 60 * 1000);  // 2 mins

  MongooseWebServer httpd(FLAGS_port);
  getter.AddReceiveAuthorizationCodeUrlPath("/oauth", &httpd);
  EXPECT_TRUE(httpd.Startup().ok());
  web_flow_->mutable_client_spec()->set_redirect_uri(
      httpd.MakeEndpointUrl(false, "/oauth"));
  web_flow_->set_authorization_code_callback(
      getter.MakeAuthorizationCodeCallback(web_flow_.get()));

  TestRefreshCredentials(web_flow_.get());
  httpd.Shutdown();
}

}  // namespace googleapis
