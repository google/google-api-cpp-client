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
// This tests the whole core HttpTransport model since the classes are all
// tied together.
//    HttpTransport
//    HttpAuthorization
//    HttpAuthorizationCredential
//    HttpRequest
//    HttpResponse

#include <string>
using std::string;
#include <vector>
#include "googleapis/client/data/data_reader.h"
#include "googleapis/client/data/data_writer.h"
#include "googleapis/client/transport/http_authorization.h"
#include "googleapis/client/transport/http_request.h"
#include "googleapis/client/transport/http_response.h"
#include "googleapis/client/transport/http_transport.h"
#include "googleapis/client/transport/http_types.h"
#include "googleapis/client/transport/test/mock_http_transport.h"
#include "googleapis/client/transport/versioninfo.h"
#include "googleapis/strings/case.h"
#include "googleapis/client/util/status.h"
#include "googleapis/base/callback.h"
#include <glog/logging.h>
#include "googleapis/strings/join.h"
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include "googleapis/util/mock_executor.h"

namespace googleapis {

using testing::_;
using testing::DoAll;
using testing::Invoke;
using testing::InvokeWithoutArgs;
using testing::Return;
using testing::SaveArg;

using client::AuthorizationCredential;
using client::DataReader;
using client::DataWriter;
using client::HttpHeaderMap;
using client::HttpRequest;
using client::HttpRequestCallback;
using client::HttpRequestOptions;
using client::HttpRequestState;
using client::HttpResponse;
using client::HttpTransport;
using client::HttpTransportLayerConfig;
using client::HttpTransportOptions;
using client::HttpTransportErrorHandler;
using client::MockAuthorizationCredential;
using client::MockHttpRequest;
using client::MockHttpTransport;
using client::MockHttpTransportErrorHandler;
using client::StatusFromHttp;
using client::StatusOk;
using client::StatusPermissionDenied;
using client::StatusUnknown;
using client::VersionInfo;
using client::NewUnmanagedInMemoryDataReader;
using client::NewManagedInMemoryDataReader;
using client::kCRLF;
using client::kCRLFCRLF;
using thread::MockExecutor;

class FakeDataWriter : public DataWriter {
 public:
  FakeDataWriter() {}
  ~FakeDataWriter() {}
  const string& got() { return s_; }

 protected:
  virtual googleapis::util::Status DoClear() {
    s_.clear();
    return StatusOk();
  }

  virtual googleapis::util::Status DoWrite(int64 bytes, const char* data) {
    s_.append(data, bytes);
    return StatusOk();
  }

  virtual DataReader* DoNewDataReader(Closure* closure) {
    return client::NewManagedInMemoryDataReader(s_, closure);
  }

 private:
  string s_;
  DISALLOW_COPY_AND_ASSIGN(FakeDataWriter);
};

// This authenticator injects a header into the request.
const char kAuthorizationHeaderName[] = "MyAuthorizationHeader";

class HttpTransportFixture : public testing::Test {
 public:
};

class HttpTransportLayerFixture : public ::testing::Test {
};

TEST_F(HttpTransportLayerFixture, TestConstructor) {
  HttpTransportLayerConfig config;
  EXPECT_FALSE(NULL == config.default_transport_options().error_handler());
  EXPECT_FALSE(NULL == config.default_transport_options().executor());
  EXPECT_EQ(NULL, config.default_transport_factory());
}

TEST_F(HttpTransportLayerFixture, TestDefaultErrorHandlerSetter) {
  HttpTransportLayerConfig config;
  HttpTransportErrorHandler *error_handler = new HttpTransportErrorHandler;

  config.ResetDefaultErrorHandler(error_handler);
  EXPECT_EQ(error_handler, config.default_transport_options().error_handler());
}

TEST_F(HttpTransportLayerFixture, TestDefaultExecutorSetter) {
  HttpTransportLayerConfig config;
  thread::Executor *executor = new thread::MockExecutor;

  config.ResetDefaultExecutor(executor);
  EXPECT_EQ(executor, config.default_transport_options().executor());
}

TEST_F(HttpTransportFixture, TestRequest) {
  MockHttpTransport transport;
  MockHttpRequest get_request(HttpRequest::GET, &transport);
  EXPECT_TRUE(get_request.content_reader() == NULL);

  MockHttpRequest post_request(HttpRequest::POST, &transport);
  ASSERT_FALSE(post_request.content_reader() == NULL);
  EXPECT_EQ(0, post_request.content_reader()->TotalLengthIfKnown());
}

TEST_F(HttpTransportFixture, TestResponseAttributes) {
  googleapis::util::Status status;
  HttpResponse http_response;
  EXPECT_EQ(HttpRequestState::UNSENT, http_response.request_state_code());
  EXPECT_EQ(0, http_response.http_code());
  status = http_response.transport_status();
  EXPECT_TRUE(status.ok()) << "transport status: " << status.ToString();
  status = http_response.status();
  EXPECT_TRUE(status.ok()) << "application status: " << status.ToString();
  EXPECT_FALSE(http_response.done());
  EXPECT_TRUE(http_response.ok());

  http_response.set_http_code(200);
  EXPECT_TRUE(
      http_response.mutable_request_state()->AutoTransitionAndNotifyIfDone()
      .ok());
  EXPECT_EQ(200, http_response.http_code());
  EXPECT_EQ(HttpRequestState::COMPLETED, http_response.request_state_code());
  EXPECT_TRUE(http_response.done());
  EXPECT_TRUE(http_response.done());

  status = http_response.transport_status();
  EXPECT_TRUE(status.ok()) << "transport status: " << status.ToString();
  status = http_response.status();
  EXPECT_TRUE(status.ok()) << "application status: " << status.ToString();
  EXPECT_TRUE(http_response.done());
  EXPECT_TRUE(http_response.ok());

  http_response.set_http_code(400);
  EXPECT_FALSE(
      http_response.mutable_request_state()->AutoTransitionAndNotifyIfDone()
      .ok());
  EXPECT_EQ(HttpRequestState::COMPLETED, http_response.request_state_code());
  EXPECT_TRUE(http_response.done());
  EXPECT_FALSE(http_response.ok());
  status = http_response.transport_status();
  EXPECT_TRUE(status.ok()) << "transport status: " << status.ToString();
  EXPECT_FALSE(http_response.status().ok());

  http_response.set_http_code(201);
  EXPECT_TRUE(
      http_response.mutable_request_state()->AutoTransitionAndNotifyIfDone()
      .ok());
  EXPECT_TRUE(http_response.ok());
  status = http_response.transport_status();
  EXPECT_TRUE(status.ok()) << "transport status: " << status.ToString();
  status = http_response.status();
  EXPECT_TRUE(status.ok()) << "application status: " << status.ToString();

  http_response.set_http_code(123);
  EXPECT_TRUE(
      http_response.mutable_request_state()->AutoTransitionAndNotifyIfDone()
      .ok());
  EXPECT_EQ(HttpRequestState::PENDING, http_response.request_state_code());
  EXPECT_FALSE(http_response.done());
  EXPECT_TRUE(http_response.ok());
  status = http_response.transport_status();
  EXPECT_TRUE(status.ok()) << "transport status: " << status.ToString();
  status = http_response.status();
  EXPECT_TRUE(status.ok()) << "application status: " << status.ToString();

  // Treating provisional 100 responses as being ok/done.
  // This is probably wrong in the long term, but keeping things simple for now
  // and explicitly stating that here with this test.
  http_response.set_http_code(123);
  EXPECT_TRUE(
      http_response.mutable_request_state()->AutoTransitionAndNotifyIfDone()
      .ok());
  EXPECT_FALSE(http_response.done());
  EXPECT_TRUE(http_response.ok());
  EXPECT_TRUE(http_response.status().ok());
}

TEST_F(HttpTransportFixture, TestAddHeader) {
  MockHttpTransport transport;
  MockHttpRequest request(HttpRequest::GET, &transport);
  EXPECT_TRUE(request.headers().empty());
  request.AddHeader("A", "a");
  request.AddHeader("B", "b");

  HttpHeaderMap::const_iterator it = request.headers().begin();
  ASSERT_TRUE(it != request.headers().end());
  EXPECT_EQ("A", it->first);
  EXPECT_EQ("a", it->second);
  ++it;
  EXPECT_EQ("B", it->first);
  EXPECT_EQ("b", it->second);

  // NOTE(user): 20120920
  // Verify adding a redundant header overwrites the original value.
  // While this might not be correct in general, it's what we are assuming
  // for now so we can rewrite security headers if needed.
  EXPECT_EQ(2, request.headers().size());
  EXPECT_EQ("a", *request.FindHeaderValue("A"));
  request.AddHeader("A", "x");
  EXPECT_EQ(2, request.headers().size());
  EXPECT_EQ("x", *request.FindHeaderValue("A"));

  const string* got = request.FindHeaderValue("a");
  EXPECT_TRUE(got != NULL);
  if (got) {
    EXPECT_EQ("x", *got);
  }
}

TEST_F(HttpTransportFixture, TestAddBuiltinHeaders) {
  const StringPiece netloc = "test.host.com:123";
  MockHttpTransport transport;
  transport.mutable_options()->set_nonstandard_user_agent("TestUserAgent");
  MockHttpRequest mock_request(HttpRequest::GET, &transport);
  mock_request.set_url(StrCat("https://", netloc, "/path/to/url"));

  Closure* set_http_code =
      NewCallback(&mock_request, &MockHttpRequest::poke_http_code, 234);
  EXPECT_CALL(mock_request, DoExecute(_))
      .WillOnce(InvokeWithoutArgs(set_http_code, &Closure::Run));
  googleapis::util::Status got_status = mock_request.Execute();
  const string *value =
      mock_request.FindHeaderValue(HttpRequest::HttpHeader_HOST);
  EXPECT_TRUE(value != NULL);
  if (value) {
    EXPECT_EQ(netloc, *value);
  }
  value = mock_request.FindHeaderValue(HttpRequest::HttpHeader_USER_AGENT);
  EXPECT_TRUE(value != NULL);
  if (value) {
    EXPECT_EQ(transport.user_agent(), *value);
  }
  value = mock_request.FindHeaderValue(HttpRequest::HttpHeader_CONTENT_TYPE);
  EXPECT_TRUE(value == NULL);
  value = mock_request.FindHeaderValue(HttpRequest::HttpHeader_CONTENT_LENGTH);
  EXPECT_TRUE(value == NULL);
  value = mock_request.FindHeaderValue(
      HttpRequest::HttpHeader_TRANSFER_ENCODING);
  EXPECT_TRUE(value == NULL);

  string kContentType = "application/xyz";
  StringPiece kPostData = "Helo, World!";
  MockHttpRequest mock_post(HttpRequest::POST, &transport);
  set_http_code =
      NewCallback(&mock_request, &MockHttpRequest::poke_http_code, 234);
  EXPECT_CALL(mock_post, DoExecute(_))
      .WillOnce(InvokeWithoutArgs(set_http_code, &Closure::Run));

  mock_post.set_url(mock_request.url());
  mock_post.set_content_type(kContentType);
  mock_post.set_content_reader(NewUnmanagedInMemoryDataReader(kPostData));

  got_status = mock_post.Execute();
  value = mock_post.FindHeaderValue(HttpRequest::HttpHeader_CONTENT_LENGTH);
  EXPECT_TRUE(value != NULL);
  if (value != NULL) {
    EXPECT_EQ(SimpleItoa(kPostData.size()), *value);
  }
  value = mock_post.FindHeaderValue(
      HttpRequest::HttpHeader_TRANSFER_ENCODING);
  EXPECT_TRUE(value == NULL);
}

TEST_F(HttpTransportFixture, TestOverrideBuiltinHeaders) {
  MockHttpTransport transport;
  transport.mutable_options()->set_nonstandard_user_agent("TestUserAgent");
  MockHttpRequest mock_request(HttpRequest::GET, &transport);
  mock_request.set_url(StrCat("https://test/path"));

  string my_host("myhost:123");
  string my_agent("my user agent");
  mock_request.AddHeader("user-agent", my_agent);
  mock_request.AddHeader("host", my_host);
  mock_request.AddHeader("another", "whatever");

  Closure* set_http_code =
      NewCallback(&mock_request, &MockHttpRequest::poke_http_code, 200);
  EXPECT_CALL(mock_request, DoExecute(_))
      .WillOnce(InvokeWithoutArgs(set_http_code, &Closure::Run));
  EXPECT_TRUE(mock_request.Execute().ok());
  EXPECT_EQ(3, mock_request.headers().size());

  EXPECT_TRUE(StringCaseEqual("host", mock_request.headers().begin()->first));
  const string* value =
      mock_request.FindHeaderValue(HttpRequest::HttpHeader_HOST);
  EXPECT_TRUE(value != NULL);
  if (value) {
    EXPECT_EQ(my_host, *value);
  }

  value = mock_request.FindHeaderValue(HttpRequest::HttpHeader_USER_AGENT);
  EXPECT_TRUE(value != NULL);
  if (value) {
    EXPECT_EQ(my_agent, *value);
  }
}

TEST_F(HttpTransportFixture, TestOkFlow) {
  MockHttpTransportErrorHandler mock_handler;  // verify not called
  MockHttpTransport transport;
  transport.mutable_options()->set_error_handler(&mock_handler);
  MockHttpRequest mock_request(HttpRequest::GET, &transport);

  const string kExpect = "Hello, World!";
  Closure* write_payload =
      NewCallback(
          &mock_request, &MockHttpRequest::poke_response_body, kExpect);
  Closure* set_http_code =
      NewCallback(&mock_request, &MockHttpRequest::poke_http_code, 234);
  EXPECT_CALL(mock_request, DoExecute(_))
      .WillOnce(
          DoAll(InvokeWithoutArgs(set_http_code, &Closure::Run),
                InvokeWithoutArgs(write_payload, &Closure::Run)));
  googleapis::util::Status got_status = mock_request.Execute();
  EXPECT_TRUE(got_status.ok()) << got_status.ToString();

  HttpResponse* http_response = mock_request.response();
  EXPECT_EQ(kExpect, http_response->body_reader()->RemainderToString());

  EXPECT_EQ(234, http_response->http_code());
  EXPECT_TRUE(http_response->transport_status().ok());
  EXPECT_TRUE(http_response->status().ok());
  EXPECT_EQ(HttpRequestState::COMPLETED, http_response->request_state_code());
}

TEST_F(HttpTransportFixture, TestReplaceWriter) {
  MockHttpTransport transport;
  MockHttpRequest mock_request(HttpRequest::GET, &transport);

  FakeDataWriter* writer = new FakeDataWriter;
  mock_request.response()->set_body_writer(writer);  // passes ownership

  const string kTests[] = { "Hello, World!", "Goodbye" };
  for (int i = 0; i < ARRAYSIZE(kTests); ++i) {
    const string& kExpect = kTests[i];
    Closure* set_http_code =
        NewCallback(&mock_request, &MockHttpRequest::poke_http_code, 224);
    Closure* write_payload =
        NewCallback(
            &mock_request, &MockHttpRequest::poke_response_body, kExpect);
    EXPECT_CALL(mock_request, DoExecute(_))
        .WillOnce(
            DoAll(InvokeWithoutArgs(set_http_code, &Closure::Run),
                  InvokeWithoutArgs(write_payload, &Closure::Run)));
    googleapis::util::Status got_status = mock_request.Execute();
    EXPECT_TRUE(got_status.ok()) << got_status.ToString();
    EXPECT_EQ(kExpect,
              mock_request.response()->body_reader()->RemainderToString());
    EXPECT_EQ(kExpect, writer->got());

    LOG(INFO) << "Clearing request with writer to check we can reuse it";
    mock_request.Clear();
  }
}


TEST_F(HttpTransportFixture, TestTransportErrorFlow) {
  MockHttpTransportErrorHandler mock_handler;
  MockHttpTransport transport;
  transport.mutable_options()->set_error_handler(&mock_handler);

  for (int allow_retries = 0; allow_retries < 4; ++allow_retries) {
    MockHttpRequest mock_request(HttpRequest::GET, &transport);

    googleapis::util::Status failure_status = StatusUnknown("Transport Error");
    std::unique_ptr<Closure> set_transport_status(
        NewPermanentCallback(
            &mock_request, &MockHttpRequest::poke_transport_status,
            failure_status));
    EXPECT_CALL(mock_request, DoExecute(_))
        .Times(allow_retries + 1)
        .WillRepeatedly(
            InvokeWithoutArgs(set_transport_status.get(), &Closure::Run));

    for (int retry = 0; retry < allow_retries; ++retry) {
      // Ask to retry
      EXPECT_CALL(mock_handler,
                  HandleTransportError(retry, &mock_request))
          .WillOnce(Return(true));
    }

    // But not the last
    EXPECT_CALL(
        mock_handler,
        HandleTransportError(allow_retries, &mock_request))
        .WillOnce(Return(false));

    EXPECT_FALSE(mock_request.Execute().ok());
    HttpResponse* http_response = mock_request.response();

    EXPECT_EQ(failure_status.ToString(),
              http_response->transport_status().ToString());
    EXPECT_EQ(failure_status.ToString(), http_response->status().ToString());
  }
}

TEST_F(HttpTransportFixture, TestHttpErrorFlow) {
  MockHttpTransportErrorHandler mock_handler;
  MockHttpTransport transport;
  transport.mutable_options()->set_error_handler(&mock_handler);

  for (int allow_retries = 0; allow_retries < 4; ++allow_retries) {
    MockHttpRequest mock_request(HttpRequest::GET, &transport);
    int http_code = 400 + allow_retries;  // just try some different codes

    std::unique_ptr<Closure> set_http_code(
        NewPermanentCallback(
            &mock_request, &MockHttpRequest::poke_http_code, http_code));
    EXPECT_CALL(mock_request, DoExecute(_))
        .Times(allow_retries + 1)
        .WillRepeatedly(InvokeWithoutArgs(set_http_code.get(), &Closure::Run));

    for (int retry = 0; retry < allow_retries; ++retry) {
      // Ask to retry
      EXPECT_CALL(mock_handler, HandleHttpError(retry, &mock_request))
          .WillOnce(Return(true));
    }

    // But not the last
    EXPECT_CALL(mock_handler, HandleHttpError(allow_retries, &mock_request))
        .WillOnce(Return(false));

    EXPECT_FALSE(mock_request.Execute().ok());
    HttpResponse* http_response = mock_request.response();

    EXPECT_TRUE(http_response->transport_status().ok());
    EXPECT_FALSE(http_response->status().ok());
    EXPECT_EQ(HttpRequestState::COMPLETED,
              http_response->request_state_code());
    EXPECT_EQ(http_code, http_response->http_code());
  }
}

TEST_F(HttpTransportFixture, TestBuiltinTransportFailure) {
  MockHttpTransport transport;
  MockHttpRequest mock_request(HttpRequest::GET, &transport);
  HttpRequestState* state = mock_request.mutable_state();

  googleapis::util::Status failure_status = StatusUnknown("Transport Error");
  Closure* set_transport_status = NewCallback(
      state, &HttpRequestState::set_transport_status, failure_status);
  EXPECT_CALL(mock_request, DoExecute(_))
      .WillOnce(InvokeWithoutArgs(set_transport_status, &Closure::Run));
  EXPECT_FALSE(mock_request.Execute().ok());
  HttpResponse* http_response = mock_request.response();

  EXPECT_EQ(failure_status.ToString(),
            http_response->transport_status().ToString());
  EXPECT_EQ(failure_status.ToString(), http_response->status().ToString());
}

TEST_F(HttpTransportFixture, Test400ErrorFlow) {
  MockHttpTransport transport;
  HttpTransportErrorHandler error_handler;
  transport.mutable_options()->set_error_handler(&error_handler);

  MockHttpRequest mock_request(HttpRequest::GET, &transport);
  Closure* set_http_code(
      NewCallback(&mock_request, &MockHttpRequest::poke_http_code, 400));
  EXPECT_CALL(mock_request, DoExecute(_))
      .WillOnce(InvokeWithoutArgs(set_http_code, &Closure::Run));

  EXPECT_FALSE(mock_request.Execute().ok());
  HttpResponse* http_response = mock_request.response();

  EXPECT_TRUE(http_response->transport_status().ok());
  EXPECT_FALSE(http_response->status().ok());
  EXPECT_EQ(HttpRequestState::COMPLETED, http_response->request_state_code());
  EXPECT_EQ(400, http_response->http_code());
}

TEST_F(HttpTransportFixture, TestDefault401ErrorFlow) {
  MockHttpTransport transport;
  HttpTransportErrorHandler error_handler;
  transport.mutable_options()->set_error_handler(&error_handler);

  std::unique_ptr<MockHttpRequest> mock_request(
      new MockHttpRequest(HttpRequest::GET, &transport));
  std::unique_ptr<Closure> set_http_code(
      NewPermanentCallback(
          mock_request.get(), &MockHttpRequest::poke_http_code, 401));
  EXPECT_CALL(*mock_request.get(), DoExecute(_))
      .WillOnce(InvokeWithoutArgs(set_http_code.get(), &Closure::Run));

  // 401 without a credential
  EXPECT_FALSE(mock_request->Execute().ok());
  HttpResponse* http_response = mock_request->response();
  EXPECT_TRUE(http_response->transport_status().ok());
  EXPECT_FALSE(http_response->status().ok());
  EXPECT_EQ(401, http_response->http_code());

  // 401 with a credential will not retry if the credential could not refresh.
  MockAuthorizationCredential credential;
  mock_request.reset(new MockHttpRequest(HttpRequest::GET, &transport));
  mock_request->set_credential(&credential);
  set_http_code.reset(
      NewPermanentCallback(
          mock_request.get(), &MockHttpRequest::poke_http_code, 401));
  EXPECT_CALL(credential, AuthorizeRequest(mock_request.get()))
      .WillOnce(Return(StatusOk()));
  EXPECT_CALL(credential, Refresh())
      .WillOnce(Return(StatusUnknown("failed")));
  EXPECT_CALL(*mock_request.get(), DoExecute(_))
      .WillOnce(InvokeWithoutArgs(set_http_code.get(), &Closure::Run));

  EXPECT_FALSE(mock_request->Execute().ok());
  http_response = mock_request->response();
  EXPECT_TRUE(http_response->transport_status().ok());
  EXPECT_FALSE(http_response->status().ok());
  EXPECT_EQ(401, http_response->http_code());

  // 401 with a credential will not retry if the credential could not refresh.
  mock_request.reset(new MockHttpRequest(HttpRequest::GET, &transport));
  mock_request->set_credential(&credential);
  set_http_code.reset(
      NewPermanentCallback(
          mock_request.get(), &MockHttpRequest::poke_http_code, 401));
  EXPECT_CALL(credential, AuthorizeRequest(mock_request.get()))
      .Times(2)
      .WillRepeatedly(Return(StatusOk()));
  EXPECT_CALL(credential, Refresh())
      .WillOnce(Return(StatusOk()));
  EXPECT_CALL(*mock_request.get(), DoExecute(_))
      .Times(2)
      .WillRepeatedly(InvokeWithoutArgs(set_http_code.get(), &Closure::Run));

  EXPECT_FALSE(mock_request->Execute().ok());
  http_response = mock_request->response();
  EXPECT_TRUE(http_response->transport_status().ok());
  EXPECT_FALSE(http_response->status().ok());
  EXPECT_EQ(401, http_response->http_code());

  // 401 where credential can refresh and successful retry.
  mock_request.reset(new MockHttpRequest(HttpRequest::GET, &transport));
  mock_request->set_credential(&credential);
  set_http_code.reset(
      NewPermanentCallback(
          mock_request.get(), &MockHttpRequest::poke_http_code, 401));
  Closure* set_success_code(
      NewCallback(mock_request.get(), &MockHttpRequest::poke_http_code, 200));
  EXPECT_CALL(credential, Refresh())
      .WillOnce(Return(StatusOk()));
  EXPECT_CALL(credential, AuthorizeRequest(mock_request.get()))
      .Times(2)
      .WillRepeatedly(Return(StatusOk()));
  EXPECT_CALL(*mock_request.get(), DoExecute(_))
      .WillOnce(InvokeWithoutArgs(set_http_code.get(), &Closure::Run))
      .WillOnce(InvokeWithoutArgs(set_success_code, &Closure::Run));
  EXPECT_TRUE(mock_request->Execute().ok());
  http_response = mock_request->response();
  EXPECT_TRUE(http_response->transport_status().ok());
  EXPECT_TRUE(http_response->status().ok());
  EXPECT_EQ(200, http_response->http_code());
}

TEST_F(HttpTransportFixture, TestDefault503ErrorFlow) {
  MockHttpTransport transport;
  HttpTransportErrorHandler error_handler;
  transport.mutable_options()->set_error_handler(&error_handler);

  MockHttpRequest mock_request(HttpRequest::GET, &transport);
  Closure* set_http_code(
      NewCallback(&mock_request, &MockHttpRequest::poke_http_code, 503));
  EXPECT_CALL(mock_request, DoExecute(_))
      .WillOnce(InvokeWithoutArgs(set_http_code, &Closure::Run));

  // TODO(user): 503 errors are not retried yet.
  EXPECT_FALSE(mock_request.Execute().ok());
  HttpResponse* http_response = mock_request.response();

  EXPECT_TRUE(http_response->transport_status().ok());
  EXPECT_FALSE(http_response->status().ok());
  EXPECT_EQ(HttpRequestState::COMPLETED, http_response->request_state_code());
  EXPECT_EQ(503, http_response->http_code());
}

void TestRedirectFlowHelper(bool same_domain, bool same_scheme) {
  const string original_url = "http://test.org/original_path";
  bool add_back_auth_header = same_domain && same_scheme;
  const string redirect_url =
      StrCat(same_scheme ? "http:" : "https:",
             "//",
             same_domain ? "test.org" : "another.org",
             "/",
             add_back_auth_header ? "different_path" : "original_path");

  MockHttpTransport transport;
  MockHttpRequest mock_request(HttpRequest::POST, &transport);
  HttpTransportErrorHandler error_handler;
  transport.mutable_options()->set_error_handler(&error_handler);

  MockAuthorizationCredential mock_credential;
  const string kAuthorizationHeaderValue = "whatever";
  std::unique_ptr<Closure> add_auth_header(NewPermanentCallback(
      &mock_request, &HttpRequest::AddHeader,
      HttpRequest::HttpHeader_AUTHORIZATION, kAuthorizationHeaderValue));
  EXPECT_CALL(mock_credential, AuthorizeRequest(&mock_request))
      .Times(1 + (add_back_auth_header ? 1 : 0))
      .WillRepeatedly(DoAll(
          InvokeWithoutArgs(add_auth_header.get(), &Closure::Run),
          Return(StatusOk())));

  mock_request.set_credential(&mock_credential);
  Closure* add_location_header =
      NewCallback(&mock_request, &MockHttpRequest::poke_response_header,
                  HttpRequest::HttpHeader_LOCATION, redirect_url);
  Closure* set_redirect_http_code(
      NewCallback(&mock_request, &MockHttpRequest::poke_http_code, 301));
  Closure* set_final_http_code(
      NewCallback(&mock_request, &MockHttpRequest::poke_http_code, 222));
  EXPECT_CALL(mock_request, DoExecute(_))
      .WillOnce(
          DoAll(
              InvokeWithoutArgs(add_location_header, &Closure::Run),
              InvokeWithoutArgs(set_redirect_http_code, &Closure::Run)))
      .WillOnce(InvokeWithoutArgs(set_final_http_code, &Closure::Run));


  mock_request.set_url(original_url);
  EXPECT_TRUE(mock_request.Execute().ok());
  HttpResponse* http_response = mock_request.response();
  EXPECT_TRUE(http_response->transport_status().ok());
  EXPECT_EQ(redirect_url, mock_request.url());
  EXPECT_TRUE(http_response->status().ok());
  EXPECT_EQ(222, http_response->http_code());

  EXPECT_EQ(
      add_back_auth_header,
      mock_request.FindHeaderValue(HttpRequest::HttpHeader_AUTHORIZATION)
      != NULL)
      << "Did handle authorization header as expected";
  EXPECT_EQ(HttpRequest::POST, mock_request.http_method());
  EXPECT_EQ(HttpRequestState::COMPLETED, http_response->request_state_code());
}

TEST_F(HttpTransportFixture, Test301RedirectFlowDifferentDomain) {
  TestRedirectFlowHelper(false, true);
}

TEST_F(HttpTransportFixture, Test301RedirectFlowDifferentScheme) {
  TestRedirectFlowHelper(true, false);
}

TEST_F(HttpTransportFixture, Test301RedirectFlowWithinDomain) {
  TestRedirectFlowHelper(true, true);
}

TEST_F(HttpTransportFixture, TestRedirectFlow) {
  const string redirect_url = "the_redirected_path";
  MockHttpTransport transport;
  HttpTransportErrorHandler error_handler;
  transport.mutable_options()->set_error_handler(&error_handler);

  // redirect code, initial method, final method
  std::vector<std::pair<int, std::pair<HttpRequest::HttpMethod,
                                       HttpRequest::HttpMethod> > > tests;
  tests.push_back(
      std::make_pair(301, std::make_pair(HttpRequest::GET, HttpRequest::GET)));
  tests.push_back(std::make_pair(
      301, std::make_pair(HttpRequest::POST, HttpRequest::POST)));
  tests.push_back(std::make_pair(
      301, std::make_pair(HttpRequest::HEAD, HttpRequest::HEAD)));
  tests.push_back(std::make_pair(
      302, std::make_pair(HttpRequest::HEAD, HttpRequest::HEAD)));
  tests.push_back(
      std::make_pair(302, std::make_pair(HttpRequest::GET, HttpRequest::GET)));
  tests.push_back(std::make_pair(
      302, std::make_pair(HttpRequest::POST, HttpRequest::POST)));
  tests.push_back(
      std::make_pair(303, std::make_pair(HttpRequest::HEAD, HttpRequest::GET)));
  tests.push_back(
      std::make_pair(303, std::make_pair(HttpRequest::GET, HttpRequest::GET)));
  tests.push_back(
      std::make_pair(303, std::make_pair(HttpRequest::POST, HttpRequest::GET)));

  for (int i = 0; i < tests.size(); ++i) {
    MockHttpRequest mock_request(tests[i].second.first, &transport);
    Closure* add_location_header =
        NewCallback(&mock_request, &MockHttpRequest::poke_response_header,
                    HttpRequest::HttpHeader_LOCATION, redirect_url);
    Closure* set_redirect_http_code(
        NewCallback(
            &mock_request, &MockHttpRequest::poke_http_code, tests[i].first));
    Closure* set_final_http_code(
        NewCallback(&mock_request, &MockHttpRequest::poke_http_code, 222));
    EXPECT_CALL(mock_request, DoExecute(_))
        .WillOnce(
            DoAll(
                InvokeWithoutArgs(add_location_header, &Closure::Run),
                InvokeWithoutArgs(set_redirect_http_code, &Closure::Run)))
        .WillOnce(InvokeWithoutArgs(set_final_http_code, &Closure::Run));

    mock_request.set_url("http://host.com/original_path");
    EXPECT_TRUE(mock_request.Execute().ok());
    HttpResponse* http_response = mock_request.response();
    EXPECT_TRUE(http_response->transport_status().ok());
    EXPECT_TRUE(http_response->status().ok());
    EXPECT_EQ(222, http_response->http_code());
    EXPECT_EQ(HttpRequestState::COMPLETED,
              http_response->request_state_code());

    const string expect_url = StrCat("http://host.com/", redirect_url);
    EXPECT_EQ(expect_url, mock_request.url())
        << "redirect=" << tests[i].first
        << "  method=" << tests[i].second.first;
    EXPECT_EQ(tests[i].second.second, mock_request.http_method())
        << "redirect=" << tests[i].first
        << "  method=" << tests[i].second.first;
  }
}

TEST_F(HttpTransportFixture, Test304Redirect) {
  const char* original_url = "the_original_url";
  const string redirect_url = "the_redirected_url";
  MockHttpTransport transport;
  MockHttpRequest mock_request(HttpRequest::GET, &transport);

  Closure* add_location_header =
      NewCallback(&mock_request, &MockHttpRequest::poke_response_header,
                  HttpRequest::HttpHeader_LOCATION, redirect_url);
  Closure* set_redirect_http_code(
      NewCallback(&mock_request, &MockHttpRequest::poke_http_code, 304));
  EXPECT_CALL(mock_request, DoExecute(_))
      .WillOnce(
          DoAll(
              InvokeWithoutArgs(add_location_header, &Closure::Run),
              InvokeWithoutArgs(set_redirect_http_code, &Closure::Run)));

  mock_request.set_url(original_url);
  // Not ok because we're going to get back the 304 redirect.
  EXPECT_FALSE(mock_request.Execute().ok());
  HttpResponse* http_response = mock_request.response();
  EXPECT_TRUE(http_response->transport_status().ok());
  EXPECT_EQ(original_url, mock_request.url());
  EXPECT_FALSE(http_response->status().ok());
  EXPECT_EQ(304, http_response->http_code());

  EXPECT_EQ(HttpRequest::GET, mock_request.http_method());
  EXPECT_EQ(HttpRequestState::COMPLETED, http_response->request_state_code());
}

TEST_F(HttpTransportFixture, TestDoNotRedirect) {
  const char* original_url = "the_original_url";
  const string redirect_url = "the_redirected_url";
  MockHttpTransport transport;
  MockHttpRequest mock_request(HttpRequest::POST, &transport);
  HttpTransportErrorHandler error_handler;
  transport.mutable_options()->set_error_handler(&error_handler);

  Closure* add_location_header =
      NewCallback(&mock_request, &MockHttpRequest::poke_response_header,
                  HttpRequest::HttpHeader_LOCATION, redirect_url);
  Closure* set_redirect_http_code(
      NewCallback(&mock_request, &MockHttpRequest::poke_http_code, 303));
  EXPECT_CALL(mock_request, DoExecute(_))
      .WillOnce(
          DoAll(
              InvokeWithoutArgs(add_location_header, &Closure::Run),
              InvokeWithoutArgs(set_redirect_http_code, &Closure::Run)));

  mock_request.set_url(original_url);
  mock_request.mutable_options()->set_max_redirects(0);

  // Not ok because we're going to get back the 303 redirect.
  EXPECT_FALSE(mock_request.Execute().ok());

  // We're treating too many redirects as a transport error.
  // TODO(user): 20130330
  // Maybe this is a mistake. Perhaps we need an easy way to know that the
  // request could not be resolved on the server. Perhaps a different
  // completion code (and unhandled redirects would end in this state as well).
  HttpResponse* http_response = mock_request.response();
  EXPECT_FALSE(http_response->transport_status().ok());
  EXPECT_EQ(HttpRequestState::COULD_NOT_SEND,
            http_response->request_state_code());

  EXPECT_EQ(original_url, mock_request.url());
  EXPECT_FALSE(http_response->status().ok());
  EXPECT_EQ(303, http_response->http_code());
  EXPECT_EQ(HttpRequest::POST, mock_request.http_method());
}

TEST_F(HttpTransportFixture, TestAuthorizationFlow) {
  MockHttpTransport transport;
  MockHttpRequest bad_mock_request(HttpRequest::GET, &transport);

  const char kFailureMessage[] = "Test failed to authorize request";
  MockAuthorizationCredential mock_credential;
  googleapis::util::Status failed_status = StatusPermissionDenied(kFailureMessage);
  EXPECT_CALL(mock_credential, AuthorizeRequest(&bad_mock_request))
      .WillOnce(Return(failed_status));

  // Note that the mock_request is not expecting to get called. This
  // will fail before even calling into it (i.e. wont DoExecute).
  bad_mock_request.set_credential(&mock_credential);
  googleapis::util::Status got_status = bad_mock_request.Execute();
  EXPECT_FALSE(got_status.ok());
  HttpResponse* http_response = bad_mock_request.response();
  EXPECT_EQ(HttpRequestState::COULD_NOT_SEND,
            http_response->request_state_code());
  EXPECT_EQ(failed_status, got_status) << got_status.ToString();
  EXPECT_EQ(0, http_response->http_code());
  EXPECT_EQ(failed_status.ToString(),
            http_response->transport_status().ToString());
  EXPECT_EQ(failed_status.ToString(),
            http_response->status().ToString());

  MockHttpRequest mock_request(HttpRequest::GET, &transport);
  mock_request.set_credential(&mock_credential);
  const string kAuthorizationHeaderValue = "whatever";

  Closure* add_header = NewCallback(
      &mock_request, &HttpRequest::AddHeader,
      kAuthorizationHeaderName, kAuthorizationHeaderValue);

  EXPECT_CALL(mock_credential, AuthorizeRequest(&mock_request))
      .WillOnce(DoAll(
          InvokeWithoutArgs(add_header, &Closure::Run),
          Return(StatusOk())));

  Closure* check_header = NewCallback(
      &mock_request, &MockHttpRequest::CheckHeader,
      kAuthorizationHeaderName, kAuthorizationHeaderValue);
  Closure* set_http_code(
      NewCallback(&mock_request, &MockHttpRequest::poke_http_code, 200));
  EXPECT_CALL(mock_request, DoExecute(_))
      .WillOnce(
          DoAll(
              InvokeWithoutArgs(set_http_code, &Closure::Run),
              InvokeWithoutArgs(check_header, &Closure::Run)));

  EXPECT_TRUE(mock_request.Execute().ok());
  EXPECT_TRUE(
      mock_request.FindHeaderValue(kAuthorizationHeaderName) != NULL);
  EXPECT_EQ(200, mock_request.response()->http_code());
}

TEST_F(HttpTransportFixture, TestCannotReuse) {
  MockHttpTransport transport;
  MockHttpRequest mock_request(HttpRequest::GET, &transport);

  std::unique_ptr<Closure> set_http_code(
      NewPermanentCallback(
          &mock_request, &MockHttpRequest::poke_http_code, 200));
  EXPECT_CALL(mock_request, DoExecute(_))
      .Times(2)
      .WillRepeatedly(InvokeWithoutArgs(set_http_code.get(), &Closure::Run));
  EXPECT_TRUE(mock_request.Execute().ok());
  EXPECT_DEATH(mock_request.Execute().IgnoreError(), "");
  mock_request.Clear();
  EXPECT_TRUE(mock_request.Execute().ok());
}

TEST_F(HttpTransportFixture, TestUserAgent) {
  HttpTransportOptions options;
  EXPECT_EQ(StrCat("http_transport_test ",
                   HttpTransportOptions::kGoogleApisUserAgent,
                   "/", VersionInfo::GetVersionString(),
                   " ", VersionInfo::GetPlatformString()),
            options.user_agent());

  options.SetApplicationName("X");
  EXPECT_EQ(StrCat("X ", HttpTransportOptions::kGoogleApisUserAgent,
                   "/", VersionInfo::GetVersionString(),
                   " ", VersionInfo::GetPlatformString()),
            options.user_agent());

  options.SetApplicationName("");
  EXPECT_EQ(
      StrCat(HttpTransportOptions::kGoogleApisUserAgent,
             "/",
             VersionInfo::GetVersionString(),
             " ", VersionInfo::GetPlatformString()),
      options.user_agent());

  options.set_nonstandard_user_agent("Hello, World!");
  EXPECT_EQ("Hello, World!", options.user_agent());

  HttpTransportOptions copy = options;
  EXPECT_EQ(copy.user_agent(), options.user_agent());
}

TEST_F(HttpTransportFixture, TestAutoDestroyRequest) {
  MockHttpTransport transport;
  transport.mutable_default_request_options()->set_destroy_when_done(true);
  MockHttpRequest* mock_request(
      new MockHttpRequest(HttpRequest::GET, &transport));

  Closure* set_http_code(
      NewCallback(mock_request, &MockHttpRequest::poke_http_code, 234));
  EXPECT_CALL(*mock_request, DoExecute(_))
      .WillOnce(InvokeWithoutArgs(set_http_code, &Closure::Run));
  EXPECT_TRUE(mock_request->Execute().ok());
}

static void TestCallback(
    HttpRequestState::StateCode* code, HttpRequest** saw,
    HttpRequest* request) {
  *code = request->state().state_code();
  *saw = request;
}

TEST_F(HttpTransportFixture, TestOkFlowAsync) {
  MockHttpTransport transport;
  MockHttpRequest mock_request(HttpRequest::GET, &transport);

  Closure* posted_closure = NULL;
  MockExecutor mock_executor;
  EXPECT_CALL(mock_executor, TryAdd(_))
      .WillOnce(
          DoAll(
              SaveArg<0>(&posted_closure),
              Return(true)));
  transport.mutable_options()->set_executor(&mock_executor);

  HttpRequestState::StateCode saw_code = HttpRequestState::UNSENT;
  HttpRequest* saw = NULL;
  HttpRequestCallback* response_callback =
      NewCallback(&TestCallback, &saw_code, &saw);
  mock_request.ExecuteAsync(response_callback);
  EXPECT_EQ(HttpRequestState::QUEUED, mock_request.state().state_code());

  Closure* set_http_code(
      NewCallback(&mock_request, &MockHttpRequest::poke_http_code, 234));
  EXPECT_CALL(mock_request, DoExecute(_))
      .WillOnce(InvokeWithoutArgs(set_http_code, &Closure::Run));

  EXPECT_TRUE(posted_closure != NULL);
  if (posted_closure != NULL) {
    HttpResponse* http_response = mock_request.response();
    EXPECT_TRUE(http_response != NULL);
    EXPECT_TRUE(saw == NULL);
    EXPECT_EQ(HttpRequestState::QUEUED, mock_request.state().state_code());
    posted_closure->Run();
    EXPECT_TRUE(http_response->done());
    EXPECT_EQ(HttpRequestState::COMPLETED,
              mock_request.state().state_code());
    EXPECT_EQ(HttpRequestState::COMPLETED, saw_code);
    EXPECT_EQ(&mock_request, saw);
  }
}

TEST_F(HttpTransportFixture, TestWillNotExecute) {
  MockHttpTransport transport;
  MockHttpRequest mock_request(HttpRequest::GET, &transport);
  googleapis::util::Status failure_status = StatusUnknown("Transport Error");
  mock_request.WillNotExecute(failure_status);
  EXPECT_TRUE(mock_request.state().done());
  EXPECT_FALSE(mock_request.state().ok());
  EXPECT_EQ(failure_status, mock_request.state().status());
  EXPECT_TRUE(mock_request.response() != NULL);
  EXPECT_FALSE(mock_request.response()->ok());
}

TEST_F(HttpTransportFixture, TestWriteReqeust) {
  const char kUrl[] = "http://test.com/url_path?query@fragment";
  const char kHeader1[] = "Header1";
  const char kHeader2[] = "Header2";
  const char kValue1[] = "Value1";
  const char kValue2[] = "Value2";

  MockHttpTransport transport;
  MockHttpRequest mock_request(HttpRequest::POST, &transport);
  FakeDataWriter writer;

  HttpTransport::WriteRequest(&mock_request, &writer);

  // This is invalid, but so is the request without a url.
  // Just showing what happens.
  EXPECT_EQ(StrCat("POST  HTTP/1.1", kCRLFCRLF), writer.got());

  mock_request.set_url(kUrl);
  writer.Clear();
  HttpTransport::WriteRequest(&mock_request, &writer);
  EXPECT_EQ(StrCat("POST ", kUrl, " HTTP/1.1", kCRLFCRLF), writer.got());

  const string kExpectFirstLine = StrCat("POST ", kUrl, " HTTP/1.1", kCRLF);
  const string kExpectHeaders = StrCat(
      kHeader1, ": ", kValue1, kCRLF,
      kHeader2, ": ", kValue2, kCRLF,
      kCRLF);

  mock_request.set_url(kUrl);
  mock_request.AddHeader(kHeader1, kValue1);
  mock_request.AddHeader(kHeader2, kValue2);

  writer.Clear();
  HttpTransport::WriteRequest(&mock_request, &writer);
  EXPECT_EQ(StrCat(kExpectFirstLine, kExpectHeaders), writer.got());

  const char kBody[] = "This is a post body\nIt is two lines long.";
  mock_request.set_content_reader(NewUnmanagedInMemoryDataReader(kBody));

  writer.Clear();
  HttpTransport::WriteRequest(&mock_request, &writer);
  EXPECT_EQ(StrCat(kExpectFirstLine, kExpectHeaders, kBody), writer.got());
}

TEST_F(HttpTransportFixture, TestReadResponse) {
  MockHttpTransport transport;
  MockHttpRequest mock_request(HttpRequest::GET, &transport);
  HttpResponse* response = mock_request.response();

  const char kHeader1[] = "Header1";
  const char kHeader2[] = "Header2";
  const char kValue1[] = "Value1";
  const char kValue2[] = "Value2";

  const string kGotFirstLine = StrCat("HTTP/1.1 200 (OK)", kCRLF);
  const string kGotHeaders = StrCat(
      kHeader1, ": ", kValue1, kCRLF,
      kHeader2, ": ", kValue2, kCRLF,
      kCRLF);
  const string kGotBody = "Hello, World\nSecond line.";

  std::unique_ptr<DataReader> response_reader;

  response_reader.reset(client::NewManagedInMemoryDataReader(
      StrCat(kGotFirstLine, kGotHeaders, kGotBody)));
  HttpTransport::ReadResponse(response_reader.get(), response);
  EXPECT_TRUE(response->ok()) << response->transport_status().error_message();
  EXPECT_EQ(200, response->http_code());
  EXPECT_EQ(2, response->headers().size());
  const string* value = response->FindHeaderValue(kHeader1);
  ASSERT_TRUE(value != NULL);
  EXPECT_EQ(kValue1, *value);
  value = response->FindHeaderValue(kHeader2);
  ASSERT_TRUE(value != NULL);
  EXPECT_EQ(kValue2, *value);
  std::unique_ptr<DataReader> body_reader(
      response->body_writer()->NewUnmanagedDataReader());
  body_reader.reset(response->body_writer()->NewUnmanagedDataReader());
  EXPECT_EQ(kGotBody, body_reader->RemainderToString());

  response_reader.reset(NewManagedInMemoryDataReader(
      StrCat(kGotFirstLine, kGotHeaders)));
  HttpTransport::ReadResponse(response_reader.get(), response);
  body_reader.reset(response->body_writer()->NewUnmanagedDataReader());
  EXPECT_EQ(200, response->http_code());
  EXPECT_EQ(2, response->headers().size());
  EXPECT_EQ("", body_reader->RemainderToString());
}

}  // namespace googleapis
