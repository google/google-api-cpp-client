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


#include "googleapis/client/service/service_request_pager.h"

#include <iostream>
using std::cout;
using std::endl;
using std::ostream;
#include <string>
using std::string;

#include "googleapis/client/data/data_reader.h"
#include "googleapis/client/data/serializable_json.h"
#include "googleapis/client/service/client_service.h"
#include "googleapis/client/transport/test/mock_http_transport.h"
#include "googleapis/client/util/status.h"
#include "googleapis/base/callback.h"
#include <gmock/gmock.h>
#include <gtest/gtest.h>

namespace googleapis {

using testing::_;
using testing::DoAll;
using testing::Invoke;
using testing::Return;
using testing::Test;
using client::SerializableJson;
using client::BaseServiceRequestPager;
using client::ClientService;
using client::ClientServiceRequest;
using client::DataReader;
using client::HttpRequest;
using client::HttpRequestCallback;
using client::HttpResponse;
using client::MockHttpRequest;
using client::MockHttpTransport;
using client::NewManagedInMemoryDataReader;
using client::ServiceRequestPager;
using client::StatusFromHttp;
using client::StatusOk;

typedef Callback1<HttpResponse*> DoExecuteType;

const char kTestUri[] = "http://test/uri";
const char kServiceRoot[] = "http://service";
const char kServicePath[] = "service_path";

class MockData;
class FakeRequest;

class FakePager : public ServiceRequestPager<FakeRequest, MockData> {
 public:
  FakePager(FakeRequest* request, MockData* data)
      : ServiceRequestPager<FakeRequest, MockData>(request, data) {
  }

  const string& peek_next_page_token() const { return next_page_token(); }
};

class MockData : public SerializableJson {
 public:
  MockData() {}
  ~MockData() {}
  MOCK_CONST_METHOD0(get_next_page_token, string());

  MOCK_METHOD0(Clear, void());
  MOCK_METHOD1(LoadFromJsonReader, googleapis::util::Status(DataReader* reader));
  MOCK_CONST_METHOD0(MakeJsonReader, DataReader*());
};

class FakeRequest : public ClientServiceRequest {
 public:
  explicit FakeRequest(const ClientService* service)
       : ClientServiceRequest(service, NULL, HttpRequest::GET, kTestUri) {
  }

  googleapis::util::Status ExecuteAndParseResponse(MockData* data) {
    return ClientServiceRequest::ExecuteAndParseResponse(data);
  }

  bool has_page_token() const { return next_.get() != NULL; }
  const string get_page_token() const { return next_.get() ? *next_ : ""; }
  void set_page_token(const string& next) { next_.reset(new string(next)); }
  void clear_page_token() { next_.reset(NULL); }

 private:
  std::unique_ptr<string> next_;
};

class PagerTestFixture : public testing::Test {
 public:
  PagerTestFixture()
      : mock_transport_alias_(new MockHttpTransport),
        service_(kServiceRoot, kServicePath, mock_transport_alias_) {
  }

  ~PagerTestFixture() {
  }

  void SetResponseBodyCallback(HttpResponse* response, const string& data) {
    response->set_body_reader(NewManagedInMemoryDataReader(data));
  }

 protected:
  MockHttpTransport* mock_transport_alias_;  // pointer to transport in service
  ClientService service_;
};

TEST_F(PagerTestFixture, Construct) {
  EXPECT_CALL(*mock_transport_alias_, NewHttpRequest(HttpRequest::GET))
      .WillOnce(
          Return(new MockHttpRequest(HttpRequest::GET, mock_transport_alias_)));

  MockData data;
  FakeRequest request(&service_);
  FakePager pager(&request, &data);

  EXPECT_FALSE(pager.is_done());
  EXPECT_EQ(&data, pager.data());
  EXPECT_EQ(&request, pager.request());
}

static void SetBodyReaderAndHttpCode(
    const char* content, int http_code, HttpResponse* response) {
  response->set_body_reader(NewManagedInMemoryDataReader(content));
  response->set_http_code(http_code);
}

TEST_F(PagerTestFixture, OnePageResults) {
  MockHttpRequest* mock_request =
      new MockHttpRequest(HttpRequest::GET, mock_transport_alias_);

  EXPECT_CALL(*mock_transport_alias_, NewHttpRequest(HttpRequest::GET))
      .WillOnce(Return(mock_request));

  MockData data;
  FakeRequest request(&service_);
  FakePager pager(&request, &data);

  EXPECT_CALL(data, Clear()).WillOnce(Return());
  EXPECT_CALL(data, LoadFromJsonReader(_)).WillOnce(Return(StatusOk()));
  EXPECT_CALL(data, get_next_page_token()).WillOnce(Return(""));

  std::unique_ptr<DoExecuteType> callback(
      NewPermanentCallback(&SetBodyReaderAndHttpCode, "ignored", 200));
  EXPECT_CALL(*mock_request, DoExecute(_))
      .WillOnce(Invoke(callback.get(), &DoExecuteType::Run));

  EXPECT_TRUE(pager.NextPage());
  EXPECT_FALSE(request.has_page_token());
  EXPECT_EQ(&data, pager.data());
  EXPECT_EQ(&request, pager.request());
  EXPECT_TRUE(pager.is_done());
  // Last response is still valid
  EXPECT_EQ(request.http_response(), pager.http_response());
  EXPECT_EQ(200, pager.http_response()->http_code());

  // Attempting to continue just returns without invoking any methods.
  EXPECT_FALSE(pager.NextPage());
  EXPECT_FALSE(request.has_page_token());

  pager.Reset();
  EXPECT_FALSE(pager.is_done());
}

TEST_F(PagerTestFixture, MultiPageResults) {
  MockHttpRequest* mock_request =
      new MockHttpRequest(HttpRequest::GET, mock_transport_alias_);

  EXPECT_CALL(*mock_transport_alias_, NewHttpRequest(HttpRequest::GET))
      .WillOnce(Return(mock_request));

  MockData data;
  FakeRequest request(&service_);
  FakePager pager(&request, &data);

  EXPECT_CALL(data, Clear()).WillRepeatedly(Return());
  EXPECT_CALL(data, LoadFromJsonReader(_))
      .WillRepeatedly(Return(StatusOk()));

  std::unique_ptr<DoExecuteType> callback(
      NewPermanentCallback(&SetBodyReaderAndHttpCode, "ignored", 200));

  EXPECT_CALL(*mock_request, DoExecute(_))
      .WillRepeatedly(Invoke(callback.get(), &DoExecuteType::Run));

  EXPECT_CALL(data, get_next_page_token()).WillOnce(Return("MORE"));
  EXPECT_EQ("", pager.peek_next_page_token());
  EXPECT_TRUE(pager.NextPage());
  EXPECT_EQ("MORE", pager.peek_next_page_token());
  EXPECT_FALSE(request.has_page_token());
  EXPECT_EQ(&data, pager.data());
  EXPECT_EQ(&request, pager.request());
  EXPECT_FALSE(pager.is_done());

  // Attempting to continue will grab the next page
  EXPECT_CALL(data, get_next_page_token()).WillOnce(Return(""));
  EXPECT_TRUE(pager.NextPage());
  EXPECT_TRUE(pager.is_done());
  EXPECT_TRUE(request.has_page_token());
  EXPECT_EQ("MORE", request.get_page_token());
  EXPECT_EQ("", pager.peek_next_page_token());

  // Resetting undoes done and the lookahead page_token in the pager
  // (which is already empty here) but it does not affect the request.
  pager.Reset();
  EXPECT_FALSE(pager.is_done());
  EXPECT_EQ("MORE", request.get_page_token());

  // Since we reset, we can execute again at which point it will
  // update the request.
  EXPECT_CALL(data, get_next_page_token()).WillOnce(Return("AGAIN"));
  EXPECT_TRUE(pager.NextPage());
  EXPECT_EQ("", request.get_page_token());
  EXPECT_EQ("AGAIN", pager.peek_next_page_token());
  EXPECT_FALSE(pager.is_done());

  // We'll reset again in the middle to show that the pager lookahead token
  // did in fact get cleared (to start again as demonstrated above).
  pager.Reset();
  EXPECT_FALSE(pager.is_done());
  EXPECT_EQ("", pager.peek_next_page_token());
}

}  // namespace googleapis
