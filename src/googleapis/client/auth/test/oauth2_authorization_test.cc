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
#include <iostream>
using std::cout;
using std::endl;
using std::ostream;
#include <memory>
#include <string>
using std::string;
#include <vector>

#include <gflags/gflags.h>
#include <glog/logging.h>
#include "googleapis/client/auth/oauth2_authorization.h"
#include "googleapis/client/data/data_reader.h"
#include "googleapis/client/data/data_writer.h"
#include "googleapis/client/transport/http_request.h"
#include "googleapis/client/transport/http_response.h"
#include "googleapis/client/transport/test/mock_http_transport.h"
#include "googleapis/client/util/date_time.h"
#include "googleapis/client/util/escaping.h"
#include "googleapis/client/util/status.h"
#include "googleapis/strings/join.h"
#include "googleapis/strings/util.h"
#include <gmock/gmock.h>
#include <gtest/gtest.h>

namespace googleapis {

using client::DataReader;
using client::HttpRequest;
using client::HttpResponse;
using client::HttpTransport;
using client::MockHttpTransport;
using client::OAuth2AuthorizationFlow;
using client::OAuth2ClientSpec;
using client::OAuth2Credential;
using client::OAuth2InstalledApplicationFlow;
using client::OAuth2WebApplicationFlow;
using client::StatusFromHttp;
using client::StatusOk;
using testing::_;
using testing::DoAll;
using testing::Invoke;
using testing::Return;

// Will be owned by global_flow_ but accessible externally so it can be
// configured per test.
static MockHttpTransport* global_mock_transport_;


static const char kTestAuthorizationCode[] = "TestAuthorizationCode";
static const char kTestClientId[] = "TestClientID";
static const char kTestClientSecret[] = "TestClientSecret";
static const char kTestRedirectUri[] = "testUrn://TestRedirectUriPrefix";
static const char kTestEncodedRedirectUri[] =
    "testUrn%3A%2F%2FTestRedirectUriPrefix";
static const char kTestScope[] = "https://FirstScope https://SecondScope";
static const char kReturnedAccessToken[] = "ReturnedAccessToken";
static const char kReturnedRefreshToken[] = "ReturnedRefreshToken";
static const int  kReturnedExpiresInSecs = 1234;

static void VerifyHeader(
    const HttpRequest* request,
    const string& name,
    const string& value) {
  const string* got = request->FindHeaderValue(name);
  EXPECT_TRUE(got != NULL) << name;
  if (got) {
    EXPECT_EQ(value, *got);
  }
}

class FakeExchangeAuthorizationCodeHttpRequest : public HttpRequest {
 public:
  explicit FakeExchangeAuthorizationCodeHttpRequest(
      HttpTransport* transport, const string& encoded_redirect)
      : HttpRequest(HttpRequest::POST, transport),
        encoded_redirect_(encoded_redirect) {
  }

  void DoExecute(HttpResponse* http_response) {
    EXPECT_FALSE(http_response->done());
    EXPECT_EQ("https://accounts.google.com/o/oauth2/token", url());
    VerifyHeader(this,
                 HttpRequest::HttpHeader_CONTENT_TYPE,
                 HttpRequest::ContentType_FORM_URL_ENCODED);

    EXPECT_EQ(
        StrCat("code=", kTestAuthorizationCode,
               "&client_id=", kTestClientId,
               "&client_secret=", kTestClientSecret,
               "&redirect_uri=", encoded_redirect_,
               "&grant_type=authorization_code"),
        content_reader()->RemainderToString());

    EXPECT_TRUE(http_response->body_writer()->Write(
        StrCat("{"
               "\n  \"access_token\": \"", kReturnedAccessToken, "\",",
               "\n  \"refresh_token\": \"", kReturnedRefreshToken, "\",",
               "\n  \"expires_in\": ", kReturnedExpiresInSecs,
               "\n}")).ok());
    http_response->set_http_code(200);
  }

 private:
  string encoded_redirect_;
};

class FakeRefreshTokenHttpRequest : public HttpRequest {
 public:
  explicit FakeRefreshTokenHttpRequest(HttpTransport* transport)
      : HttpRequest(HttpRequest::POST, transport) {
  }

  void DoExecute(HttpResponse* http_response) {
    EXPECT_FALSE(http_response->done());
    EXPECT_EQ("https://accounts.google.com/o/oauth2/token", url());
    EXPECT_EQ(
        StrCat("client_id=", kTestClientId,
               "&client_secret=", kTestClientSecret,
               "&grant_type=refresh_token",
               "&refresh_token=", kReturnedRefreshToken),
        content_reader()->RemainderToString());

    EXPECT_TRUE(http_response->body_writer()->Write(
        StrCat("{"
               "\n  \"expires_in\": ", kReturnedExpiresInSecs, ",",
               "\n  \"access_token\": \"", kReturnedAccessToken, "\"",
               "\n}")).ok());
    http_response->set_http_code(200);
  }
};

class FakeRevokeTokenHttpRequest : public HttpRequest {
 public:
  FakeRevokeTokenHttpRequest(HttpTransport* transport, const char* expect_token)
      : HttpRequest(HttpRequest::POST, transport),
        expect_token_(expect_token) {
  }

  void DoExecute(HttpResponse* http_response) {
    EXPECT_FALSE(http_response->done());
    EXPECT_EQ("https://accounts.google.com/o/oauth2/revoke", url());
    VerifyHeader(this,
                 HttpRequest::HttpHeader_CONTENT_TYPE,
                 HttpRequest::ContentType_FORM_URL_ENCODED);
    EXPECT_EQ(StrCat("token=", expect_token_),
              content_reader()->RemainderToString());

    http_response->set_http_code(200);
  }

 private:
  string expect_token_;
};


class OAuth2TestFixture : public testing::Test {
 public:
  static void SetUpTestCase() {
    global_mock_transport_ = new MockHttpTransport;
    global_flow_.reset(new OAuth2AuthorizationFlow(global_mock_transport_));

    // NOTE(user): 20121008
    // This client is a personal client I created for purposes of this test.
    // It should eventually be replaced with something more widely internal
    // (such as a test user). Generating a new client and giving it access
    // to the requested kScope should be sufficient to switch the test over.
    OAuth2ClientSpec* client_spec = global_flow_->mutable_client_spec();
    client_spec->set_client_id(kTestClientId);
    client_spec->set_client_secret(kTestClientSecret);
    client_spec->set_redirect_uri(kTestRedirectUri);
  }

  static void TearDownTestCase() {
    global_flow_.reset(NULL);
  }

 protected:
  static std::unique_ptr<OAuth2AuthorizationFlow> global_flow_;
};

class FakeFailedHttpRequest : public HttpRequest {
 public:
  FakeFailedHttpRequest(HttpTransport* transport, int http_code)
      : HttpRequest(HttpRequest::POST, transport), http_code_(http_code) {
  }

  void DoExecute(HttpResponse* http_response) {
    EXPECT_FALSE(http_response->done());
    http_response->set_http_code(http_code_);
  }

 private:
  int http_code_;
};

std::unique_ptr<OAuth2AuthorizationFlow> OAuth2TestFixture::global_flow_;


TEST_F(OAuth2TestFixture, TestClientSpecFromJson) {
  string json_template =
      "{\n"
      "  \"FLOW_TYPE\": {\n"
      "    \"client_id\": \"asdfjasdljfasdkjf\",\n"
      "    \"client_secret\": \"1912308409123890\",\n"
      "    \"redirect_uris\": [\"https://www.example.com/oauth2callback\"],\n"
      "    \"auth_uri\": \"https://test/auth\",\n"
      "    \"token_uri\": \"https://test/token\"\n"
      "  }\n"
      "}\n";
  const char* types[] = { "web", "installed" };
  for (int test = 0; test < arraysize(types); ++test) {
    std::unique_ptr<MockHttpTransport> transport(new MockHttpTransport);
    string json = StringReplace(json_template, "FLOW_TYPE", types[test], false);
    googleapis::util::Status status;
    OAuth2AuthorizationFlow* flow =
        OAuth2AuthorizationFlow::MakeFlowFromClientSecretsJson(
            json, transport.release(), &status);
    ASSERT_TRUE(status.ok())
        << status.ToString()
        << ": test=" << types[test];
    std::unique_ptr<OAuth2AuthorizationFlow> delete_when_done(flow);

    const OAuth2ClientSpec& spec = flow->client_spec();
    EXPECT_EQ("asdfjasdljfasdkjf", spec.client_id());
    EXPECT_EQ("1912308409123890", spec.client_secret());
    EXPECT_EQ("https://test/auth", spec.auth_uri());
    EXPECT_EQ("https://test/token", spec.token_uri());
    EXPECT_EQ("https://www.example.com/oauth2callback", spec.redirect_uri());
  }
}

TEST_F(OAuth2TestFixture, TestConstruct) {
  OAuth2Credential credential;
  EXPECT_EQ(OAuth2Credential::kOAuth2CredentialType, credential.type());
  EXPECT_EQ("", credential.access_token().as_string());
  EXPECT_EQ("", credential.refresh_token().as_string());
  EXPECT_EQ(kint64max, credential.expiration_timestamp_secs());
}

TEST_F(OAuth2TestFixture, TestGenerateAuthorizationCodeRequestUrl) {
  string url = global_flow_->GenerateAuthorizationCodeRequestUrl(kTestScope);
  EXPECT_EQ(
      StrCat("https://accounts.google.com/o/oauth2/auth",
             "?client_id=", kTestClientId,
             "&redirect_uri=", kTestEncodedRedirectUri,
             "&scope=https%3A%2F%2FFirstScope%20https%3A%2F%2FSecondScope",
             "&response_type=code"),
      url);
}

TEST_F(OAuth2TestFixture, TestGenerateWebAuthorizationCodeRequestUrl) {
  OAuth2WebApplicationFlow web(new MockHttpTransport);
  OAuth2ClientSpec* client_spec = web.mutable_client_spec();
  client_spec->set_client_id(kTestClientId);
  client_spec->set_client_secret(kTestClientSecret);
  client_spec->set_redirect_uri(kTestRedirectUri);
  string basic_url =
      StrCat("https://accounts.google.com/o/oauth2/auth",
             "?client_id=", kTestClientId,
             "&redirect_uri=", kTestEncodedRedirectUri,
             "&scope=https%3A%2F%2FFirstScope%20https%3A%2F%2FSecondScope",
             "&response_type=code");
  EXPECT_EQ(basic_url, web.GenerateAuthorizationCodeRequestUrl(kTestScope));

  web.set_force_approval_prompt(true);
  web.set_offline_access_type(true);

  EXPECT_EQ(StrCat(basic_url, "&approval_prompt=force&access_type=offline"),
            web.GenerateAuthorizationCodeRequestUrl(kTestScope));
}

TEST_F(OAuth2TestFixture, TestExchangeAuthorizationCodeRequest) {
  OAuth2Credential credential;

  // Ownership passed back to caller of NewHttpRequest.
  EXPECT_CALL(*global_mock_transport_, NewHttpRequest(HttpRequest::POST))
      .WillOnce(Return(
          new FakeExchangeAuthorizationCodeHttpRequest(
              global_mock_transport_, kTestEncodedRedirectUri)));

  client::OAuth2RequestOptions options;
  googleapis::util::Status status = global_flow_->PerformExchangeAuthorizationCode(
      kTestAuthorizationCode, options, &credential);

  EXPECT_TRUE(status.ok()) << status.ToString();

  string access_token;
  string refresh_token;
  credential.access_token().AppendTo(&access_token);
  credential.refresh_token().AppendTo(&refresh_token);

  EXPECT_EQ(kReturnedAccessToken, access_token);
  EXPECT_EQ(kReturnedRefreshToken, refresh_token);

  // Try mock call again, but this time we'll override the scope we asked for
  options.redirect_uri = "https://test_redirect";
  const string kEncodedUri = "https%3A%2F%2Ftest_redirect";

  EXPECT_CALL(*global_mock_transport_, NewHttpRequest(HttpRequest::POST))
      .WillOnce(Return(
          new FakeExchangeAuthorizationCodeHttpRequest(
              global_mock_transport_, kEncodedUri)));

  status = global_flow_->PerformExchangeAuthorizationCode(
      kTestAuthorizationCode, options, &credential);
  EXPECT_TRUE(status.ok()) << status.ToString();
}

TEST_F(OAuth2TestFixture, TestExchangeAuthorizationCodeRequestFailure) {
  OAuth2Credential credential;

  // Ownership passed back to caller of NewHttpRequest.
  EXPECT_CALL(*global_mock_transport_, NewHttpRequest(HttpRequest::POST))
      .WillOnce(Return(
          new FakeFailedHttpRequest(global_mock_transport_, 401)));

  client::OAuth2RequestOptions options;
  googleapis::util::Status status = global_flow_->PerformExchangeAuthorizationCode(
      kTestAuthorizationCode, options, &credential);

  EXPECT_FALSE(status.ok()) << status.ToString();
  EXPECT_EQ(util::error::PERMISSION_DENIED, status.error_code())
      << status.error_message();

  string refresh_token;
  credential.refresh_token().AppendTo(&refresh_token);

  EXPECT_TRUE(credential.access_token().empty());
  EXPECT_TRUE(credential.refresh_token().empty());
}

TEST_F(OAuth2TestFixture, TestRefreshToken) {
  OAuth2Credential credential;
  credential.mutable_refresh_token()->set(kReturnedRefreshToken);

  // Ownership passed back to caller of NewHttpRequest.
  EXPECT_CALL(*global_mock_transport_, NewHttpRequest(HttpRequest::POST))
      .WillOnce(Return(
          new FakeRefreshTokenHttpRequest(global_mock_transport_)));

  int64 expires_near_secs =
      client::DateTime().ToEpochTime() + kReturnedExpiresInSecs;

  client::OAuth2RequestOptions options;
  googleapis::util::Status status =
      global_flow_->PerformRefreshToken(options, &credential);
  EXPECT_TRUE(status.ok()) << status.ToString();

  string access_token;
  credential.access_token().AppendTo(&access_token);
  EXPECT_EQ(kReturnedAccessToken, access_token);
  EXPECT_NEAR(expires_near_secs, credential.expiration_timestamp_secs(), 1);
}

TEST_F(OAuth2TestFixture, TestRefreshTokenFailure) {
  OAuth2Credential credential;
  credential.mutable_refresh_token()->set(kReturnedRefreshToken);

  // Ownership passed back to caller of NewHttpRequest.
  EXPECT_CALL(*global_mock_transport_, NewHttpRequest(HttpRequest::POST))
      .WillOnce(Return(
          new FakeFailedHttpRequest(global_mock_transport_, 400)));

  googleapis::util::Status status = global_flow_->PerformRevokeToken(false, &credential);
  EXPECT_FALSE(status.ok()) << status.ToString();
  EXPECT_TRUE(credential.access_token().empty());
}

TEST_F(OAuth2TestFixture, TestRevokeAccessToken) {
  OAuth2Credential credential;
  credential.mutable_access_token()->set(kReturnedAccessToken);
  credential.mutable_refresh_token()->set(kReturnedRefreshToken);

  // Ownership passed back to caller of NewHttpRequest.
  EXPECT_CALL(*global_mock_transport_, NewHttpRequest(HttpRequest::POST))
      .WillOnce(Return(
          new FakeRevokeTokenHttpRequest(global_mock_transport_,
                                         kReturnedAccessToken)));

  googleapis::util::Status status = global_flow_->PerformRevokeToken(true, &credential);
  EXPECT_TRUE(status.ok()) << status.ToString();
  EXPECT_TRUE(credential.access_token().empty());
  EXPECT_TRUE(!credential.refresh_token().empty());
}

TEST_F(OAuth2TestFixture, TestRevokeRefreshToken) {
  OAuth2Credential credential;
  credential.mutable_access_token()->set(kReturnedAccessToken);
  credential.mutable_refresh_token()->set(kReturnedRefreshToken);

  // Ownership passed back to caller of NewHttpRequest.
  EXPECT_CALL(*global_mock_transport_, NewHttpRequest(HttpRequest::POST))
      .WillOnce(Return(
          new FakeRevokeTokenHttpRequest(global_mock_transport_,
                                         kReturnedRefreshToken)));

  googleapis::util::Status status = global_flow_->PerformRevokeToken(false, &credential);
  EXPECT_TRUE(status.ok()) << status.ToString();
  EXPECT_TRUE(!credential.access_token().empty());
  EXPECT_TRUE(credential.refresh_token().empty());
}

TEST_F(OAuth2TestFixture, TestRevokeAccessTokenFailure) {
  OAuth2Credential credential;
  credential.mutable_access_token()->set(kReturnedAccessToken);
  credential.mutable_refresh_token()->set(kReturnedRefreshToken);

  // Ownership passed back to caller of NewHttpRequest.
  EXPECT_CALL(*global_mock_transport_, NewHttpRequest(HttpRequest::POST))
      .WillOnce(Return(
          new FakeFailedHttpRequest(global_mock_transport_, 400)));

  googleapis::util::Status status = global_flow_->PerformRevokeToken(true, &credential);
  EXPECT_FALSE(status.ok()) << status.ToString();
  EXPECT_TRUE(!credential.access_token().empty());
  EXPECT_TRUE(!credential.refresh_token().empty());
}

TEST_F(OAuth2TestFixture, TestSerialization) {
  OAuth2Credential credential;
  std::unique_ptr<DataReader> reader(credential.MakeDataReader());
  string serialized = reader->RemainderToString();
  EXPECT_EQ("{}", serialized) << serialized;

  OAuth2Credential verify;
  verify.set_access_token("access");
  verify.set_refresh_token("refresh");
  verify.set_expiration_timestamp_secs(123);

  EXPECT_TRUE(reader->Reset());
  googleapis::util::Status status = verify.Load(reader.get());
  EXPECT_TRUE(status.ok()) << status.ToString();
  EXPECT_TRUE(verify.access_token().as_string().empty());
  EXPECT_TRUE(verify.refresh_token().as_string().empty());

  credential.set_access_token("access");
  credential.set_refresh_token("refresh");
  credential.set_expiration_timestamp_secs(123);

  reader.reset(credential.MakeDataReader());
  EXPECT_TRUE(verify.Load(reader.get()).ok());
  EXPECT_EQ("access", verify.access_token().as_string());
  EXPECT_EQ("refresh", verify.refresh_token().as_string());
  EXPECT_EQ(123, verify.expiration_timestamp_secs());
}

static string BuildJwtJson(string v) {
  string ret("{");
  ret.append("\"id_token\": \"");
  ret.append(v);
  ret.append("\"}");
  return ret;
}

TEST_F(OAuth2TestFixture, TestJWT) {
  OAuth2Credential credential;
  std::unique_ptr<DataReader> reader(credential.MakeDataReader());
  string serialized = reader->RemainderToString();
  EXPECT_EQ("{}", serialized) << serialized;

  string claims("{\"hello\": \"world\"}");
  string enc_claims;
  googleapis_util::WebSafeBase64Escape(
      reinterpret_cast<const unsigned char *>(claims.data()),
      claims.size(), &enc_claims, false);
  string good_token("part1.");
  good_token.append(enc_claims);
  good_token.append(".part3");

  string json(BuildJwtJson(good_token));
  auto status = credential.UpdateFromString(json);
  EXPECT_TRUE(status.ok()) << status.ToString() << ": " << json;

  json = BuildJwtJson("too.short");
  status = credential.UpdateFromString(json);
  EXPECT_FALSE(status.ok()) << status.ToString() << ": " << json;

  json = BuildJwtJson("one.tok.too.long");
  status = credential.UpdateFromString(json);
  EXPECT_FALSE(status.ok()) << status.ToString() << ": " << json;
}

// TODO(user): 20130315
// Test RefreshCredential from store
// Test RefreshCredential from refresh
// Test RefreshCredential from scratch
// Test RefreshCredential from revoked

}  // namespace googleapis
