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
#include <string>
using std::string;
#include <utility>
#include "googleapis/client/data/data_reader.h"
#include "googleapis/client/data/serializable_json.h"
#include "googleapis/client/service/client_service.h"
#include "googleapis/client/transport/http_response.h"
#include "googleapis/client/transport/http_transport.h"
#include "googleapis/client/transport/test/mock_http_transport.h"
#include "googleapis/client/util/status.h"
#include "googleapis/client/util/uri_template.h"
#include "googleapis/base/callback.h"
#include <glog/logging.h>
#include "googleapis/strings/strcat.h"
#include "googleapis/strings/stringpiece.h"
#include <gtest/gtest.h>

namespace googleapis {

using client::SerializableJson;
using client::AuthorizationCredential;
using client::ClientService;
using client::ClientServiceRequest;
using client::DataReader;
using client::HttpRequest;
using client::HttpResponse;
using client::HttpTransport;
using client::MockHttpTransport;
using client::MockHttpRequest;
using client::NewUnmanagedInMemoryDataReader;
using client::StatusInvalidArgument;
using client::StatusOk;
using client::StatusUnknown;
using client::UriTemplate;
using client::UriTemplateConfig;

using testing::_;
using testing::DoAll;
using testing::InvokeWithoutArgs;
using testing::Return;

const StringPiece kServiceRootUri("http://test.com/");
const StringPiece kServicePath("SERVICE/PATH/");

class TestServiceRequest : public ClientServiceRequest {
 public:
  TestServiceRequest(ClientService* service,
                     AuthorizationCredential* credential,
                     HttpRequest::HttpMethod method,
                     const string& uri_template)
      : ClientServiceRequest(service, credential, method, uri_template) {
  }

  virtual ~TestServiceRequest() {}

  string DetermineFinalUrl() {
    PrepareHttpRequest().IgnoreError();
    return mutable_http_request()->url();
  }

  virtual googleapis::util::Status AppendOptionalQueryParameters(string* target) {
    if (!get_use_media_download()) {
      target->append("&optional");
    }
    return ClientServiceRequest::AppendOptionalQueryParameters(target);
  }

  virtual googleapis::util::Status AppendVariable(
      const StringPiece& name, const UriTemplateConfig& config, string* out) {
    if (name == "var") {
      out->append("value");
      return StatusOk();
    }
    if (name == "list") {
      UriTemplate::AppendListFirst("red", config, out);
      UriTemplate::AppendListNext("green", config, out);
      UriTemplate::AppendListNext("blue", config, out);
      return StatusOk();
    }

    if (name == "map") {
      UriTemplate::AppendMapFirst("semi", ";", config, out);
      UriTemplate::AppendMapNext("dot", ".", config, out);
      UriTemplate::AppendMapNext("comma", ",", config, out);
      return StatusOk();
    }

    return StatusInvalidArgument(StrCat("Unknown name=", name));
  }

  googleapis::util::Status ExecuteAndParseResponse(SerializableJson* data) {
    // Expose protected method.
    return ClientServiceRequest::ExecuteAndParseResponse(data);
  }

  void set_use_media_download(bool use) {
    ClientServiceRequest::set_use_media_download(use);
  }
  bool get_use_media_download() const {
    return ClientServiceRequest::get_use_media_download();
  }
};

class FakeJsonData : public SerializableJson {
 public:
  FakeJsonData() {}
  ~FakeJsonData() {}
  virtual void Clear() {}
  virtual googleapis::util::Status LoadFromJsonReader(DataReader* reader) {
    return StatusOk();
  }
  virtual DataReader* MakeJsonReader() const { return NULL; }  // not used
};

class ClientServiceTestFixture : public testing::Test {
 public:
  ClientServiceTestFixture()
      : transport_(new MockHttpTransport),
        service_(kServiceRootUri, kServicePath, transport_) {
  }

 protected:
  void SetupMockedRequest(bool will_invoke, const string& response_body) {
    // Setup the mock for the underlying http request that will execute.
    MockHttpRequest* mock_request =
        new MockHttpRequest(HttpRequest::GET, transport_);
    if (will_invoke) {
      Closure* set_http_code =
          NewCallback(mock_request, &MockHttpRequest::poke_http_code, 200);
      Closure* set_response_body =
          NewCallback(
              mock_request,
              &MockHttpRequest::poke_response_body, response_body);
      EXPECT_CALL(*mock_request, DoExecute(_))
          .WillOnce(
              DoAll(
                  InvokeWithoutArgs(set_http_code, &Closure::Run),
                  InvokeWithoutArgs(set_response_body, &Closure::Run)));
    }

    // Mock the transport to return the mocked http request
    // when the TestServiceRequest asks for it.
    EXPECT_CALL(*transport_, NewHttpRequest(HttpRequest::GET))
        .WillOnce(Return(mock_request));
  }

  MockHttpTransport* transport_;
  ClientService service_;
};

TEST_F(ClientServiceTestFixture, TestConstruct) {
  std::pair<StringPiece, StringPiece> tests[] = {
      std::make_pair("root", "path"), std::make_pair("root/", "path"),
      std::make_pair("root/", "/path"), std::make_pair("root", "/path"),
  };

  for (int i = 0; i < ARRAYSIZE(tests); ++i) {
    HttpTransport* transport = new MockHttpTransport;
    ClientService test_service(tests[i].first, tests[i].second, transport);
    EXPECT_EQ("root/path", test_service.service_url());
    EXPECT_EQ("root/", test_service.url_root())
      << "root=" << tests[i].first
      << " path=" << tests[i].second;
    EXPECT_EQ("path", test_service.url_path())
      << "root=" << tests[i].first
      << " path=" << tests[i].second;
  }
}

TEST_F(ClientServiceTestFixture, TestPrepare) {
  EXPECT_EQ(kServiceRootUri, service_.url_root());
  EXPECT_EQ(kServicePath, service_.url_path());

  string uri = "{var}/method{?list*}";

  SetupMockedRequest(false, "");
  TestServiceRequest request(&service_, NULL, HttpRequest::GET, uri);
  EXPECT_FALSE(request.get_use_media_download());

  EXPECT_EQ(
      StrCat(kServiceRootUri, kServicePath,
             "value/method?list=red&list=green&list=blue&optional"),
      request.DetermineFinalUrl());
}

TEST_F(ClientServiceTestFixture, TestConvertToHttpRequest) {
  EXPECT_EQ(kServiceRootUri, service_.url_root());
  EXPECT_EQ(kServicePath, service_.url_path());

  string uri = "{var}/method{?list*}";

  SetupMockedRequest(false, "");

  ClientServiceRequest* request =
      new TestServiceRequest(&service_, NULL, HttpRequest::GET, uri);
  std::unique_ptr<HttpRequest> http_request(
      request->ConvertToHttpRequestAndDestroy());
  EXPECT_TRUE(http_request->response()->transport_status().ok());

  EXPECT_EQ(
      StrCat(kServiceRootUri, kServicePath,
             "value/method?list=red&list=green&list=blue&optional"),
      http_request->url());
}

TEST_F(ClientServiceTestFixture, TestConvertToUnresolvedHttpRequest) {
  EXPECT_EQ(kServiceRootUri, service_.url_root());
  EXPECT_EQ(kServicePath, service_.url_path());

  // Use an unresolvable variable (unknown)
  string uri = "{unknown}/method{?list*}";

  SetupMockedRequest(false, "");
  ClientServiceRequest* request =
      new TestServiceRequest(&service_, NULL, HttpRequest::GET, uri);

  std::unique_ptr<HttpRequest> http_request(
      request->ConvertToHttpRequestAndDestroy());
  EXPECT_FALSE(http_request->response()->transport_status().ok());

  // We only partially resolved the url.
  EXPECT_EQ(
      StrCat(kServiceRootUri, kServicePath,
             "{unknown}/method?list=red&list=green&list=blue&optional"),
      http_request->url());
}

TEST_F(ClientServiceTestFixture, TestPrepareWithMediaDownload) {
  const string method_url = "/method";

  std::pair<string, string> tests[] = {
      std::make_pair("", StrCat(method_url, "?alt=media")),
      std::make_pair("?param=value",
                     StrCat(method_url, "?param=value&alt=media")),
      std::make_pair("?{var}", StrCat(method_url, "?value&alt=media")),
  };
  for (int i = 0; i < ARRAYSIZE(tests); ++i) {
    SetupMockedRequest(false, "");
    TestServiceRequest request(&service_, NULL, HttpRequest::GET,
                               StrCat(method_url, tests[i].first));
    request.set_use_media_download(true);
    EXPECT_EQ(StrCat(service_.service_url(), tests[i].second),
              request.DetermineFinalUrl());
  }
}

TEST_F(ClientServiceTestFixture, TestPrepareWithMediaDownloadAndAlt) {
  string method_url = "/method";

  SetupMockedRequest(false, "");
  TestServiceRequest request_with(
      &service_, NULL, HttpRequest::GET,
      StrCat(method_url, "?alt=media&foo=bar"));
  request_with.set_use_media_download(true);
  EXPECT_EQ(StrCat(service_.service_url(), method_url, "?alt=media&foo=bar"),
            request_with.DetermineFinalUrl());

  SetupMockedRequest(false, "");
  TestServiceRequest request_with_different(
      &service_, NULL, HttpRequest::GET,
      StrCat(method_url, "?alt=different&foo=bar"));
  request_with_different.set_use_media_download(true);
  EXPECT_EQ(
      StrCat(service_.service_url(), method_url, "?alt=different&foo=bar"),
      request_with_different.DetermineFinalUrl());
}

TEST_F(ClientServiceTestFixture, TestDeleteWhenDone) {
  string method_url = "/method";
  SetupMockedRequest(true, "{}");
  TestServiceRequest* request(
      new TestServiceRequest(&service_, NULL, HttpRequest::GET, method_url));

  EXPECT_TRUE(request->Execute().ok());
  request->DestroyWhenDone();  // test after it is already done
}

TEST_F(ClientServiceTestFixture, TestParseAndDeleteWhenDone) {
  string method_url = "/method";
  SetupMockedRequest(true, "{}");
  TestServiceRequest* request(
      new TestServiceRequest(&service_, NULL, HttpRequest::GET, method_url));
  request->DestroyWhenDone();

  FakeJsonData data;
  EXPECT_TRUE(request->ExecuteAndParseResponse(&data).ok());
}

TEST_F(ClientServiceTestFixture, TestParseResponse) {
  string method_url = "/method";
  SetupMockedRequest(true, "{}");
  TestServiceRequest* request(
      new TestServiceRequest(&service_, NULL, HttpRequest::GET, method_url));
  request->DestroyWhenDone();

  FakeJsonData data;
  EXPECT_TRUE(request->ExecuteAndParseResponse(&data).ok());
}

static void IncCalled(int* call_count, HttpRequest* request) {
  ++*call_count;
}

TEST_F(ClientServiceTestFixture, TestAsyncPrepareFailure) {
  string method_url = "/{invalid";
  SetupMockedRequest(false, "");
  TestServiceRequest* request(
      new TestServiceRequest(&service_, NULL, HttpRequest::GET, method_url));

  int call_count = 0;
  client::HttpRequestCallback* callback =
      NewCallback(&IncCalled, &call_count);
  request->ExecuteAsync(callback);
  EXPECT_EQ(1, call_count);
  delete request;
}

}  // namespace googleapis
