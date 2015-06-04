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


#include "googleapis/client/transport/http_scribe.h"

#include <memory>
#include <string>
using std::string;
#include "googleapis/client/data/data_reader.h"
#include "googleapis/client/transport/http_request.h"
#include "googleapis/client/transport/http_response.h"
#include "googleapis/client/transport/http_transport.h"
#include "googleapis/client/transport/http_types.h"
#include "googleapis/client/transport/test/mock_http_transport.h"
#include "googleapis/client/util/status.h"
#include "googleapis/base/callback.h"
#include "googleapis/base/macros.h"
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include "googleapis/strings/strcat.h"
#include "googleapis/strings/stringpiece.h"

namespace googleapis {

using client::HttpRequestBatch;
using client::HttpHeaderMap;
using client::HttpRequest;
using client::HttpResponse;
using client::HttpScribeCensor;
using client::HttpEntryScribe;
using client::HttpScribe;
using client::MockHttpRequest;
using client::MockHttpTransport;
using client::NewUnmanagedInMemoryDataReader;
using client::StatusInternalError;

using testing::_;
using testing::Invoke;
using testing::Return;

const string censored_base_url(
    "https://accounts.google.com/o/oauth2/auth");

static void SetHttpCode(int http_code, HttpResponse* response) {
  response->set_http_code(http_code);
}

static void SetTransportStatus(
    const googleapis::util::Status &status, HttpResponse* response) {
  response->mutable_request_state()->set_transport_status(status);
}

class MockEntry : public HttpEntryScribe::Entry {
 public:
  MockEntry(HttpEntryScribe* scribe, const HttpRequest* request)
      : HttpEntryScribe::Entry(scribe, request) {}
  MockEntry(HttpEntryScribe* scribe, const HttpRequestBatch* batch)
      : HttpEntryScribe::Entry(scribe, batch) {}
  virtual ~MockEntry() {}

  MOCK_METHOD0(FlushAndDestroy, void());
  MOCK_METHOD1(Sent, void(const HttpRequest* request));
  MOCK_METHOD1(SentBatch, void(const HttpRequestBatch* batch));
  MOCK_METHOD1(Received, void(const HttpRequest* request));
  MOCK_METHOD1(ReceivedBatch, void(const HttpRequestBatch* batch));
  MOCK_METHOD2(Failed,
               void(const HttpRequest* request, const googleapis::util::Status& status));
  MOCK_METHOD2(
      FailedBatch,
      void(const HttpRequestBatch* batch, const googleapis::util::Status& status));
};

class MockHttpEntryScribe : public HttpEntryScribe {
 public:
  explicit MockHttpEntryScribe(HttpScribeCensor* censor)
      : HttpEntryScribe(censor) {
  }
  virtual ~MockHttpEntryScribe() {}

  MOCK_METHOD1(NewEntry,
               MockHttpEntryScribe::Entry*(const HttpRequest* request));
  MOCK_METHOD1(NewBatchEntry,
               MockHttpEntryScribe::Entry*(const HttpRequestBatch* batch));
  MOCK_METHOD0(Checkpoint, void());
};

class HttpScribeTestFixture : public testing::Test {
 public:
  HttpScribeTestFixture() : request_(HttpRequest::GET, &transport_) { }
  virtual ~HttpScribeTestFixture() {}

 protected:
  MockHttpTransport transport_;
  MockHttpRequest request_;

 private:
  DISALLOW_COPY_AND_ASSIGN(HttpScribeTestFixture);
};

TEST_F(HttpScribeTestFixture, CensorUrl) {
  std::unique_ptr<HttpScribeCensor> censor(new HttpScribeCensor);
  string bad_url = StrCat(censored_base_url,
                          "?client_id=ID3&client_secret=SECRET"
                          "&data=DATA&refresh_token=REFRESH");
  const string kExpectUrl =
      StrCat(censored_base_url,
             "?client_id=ID3&client_secret=CENSORED"
             "&data=DATA&refresh_token=CENSORED");
  bool censored = false;

  request_.set_url(bad_url);
  EXPECT_EQ(kExpectUrl, censor->GetCensoredUrl(request_, &censored));
  EXPECT_TRUE(censored);

  string other_url =
      "http://www.google.com/x?client_secret=123&refresh_token=123";
  string censored_other_url =
      "http://www.google.com/x?client_secret=CENSORED&refresh_token=CENSORED";
  request_.set_url(other_url);
  censored = false;
  EXPECT_EQ(censored_other_url, censor->GetCensoredUrl(request_, &censored));
  EXPECT_TRUE(censored);

  string good_url = "http://www.google.com/path?a=123";
  request_.set_url(good_url);
  EXPECT_EQ(good_url, censor->GetCensoredUrl(request_, &censored));
  EXPECT_FALSE(censored);
}

TEST_F(HttpScribeTestFixture, CensorResponseWholeBody) {
  std::unique_ptr<HttpScribeCensor> censor(new HttpScribeCensor);
  const StringPiece kResponseBody("RESPONSE BODY");
  HttpResponse* response = request_.response();
  request_.set_url(StrCat(censored_base_url, "?arg=foo"));

  response->set_body_reader(NewUnmanagedInMemoryDataReader(kResponseBody));

  int64 original_size;
  bool censored;
  EXPECT_EQ("CENSORED",
            censor->GetCensoredResponseBody(
                request_, kint64max, &original_size, &censored));
  EXPECT_TRUE(censored);
  EXPECT_EQ(kResponseBody.size(), original_size);

  request_.set_url("http://www.google.com");
  EXPECT_EQ(kResponseBody,
            censor->GetCensoredResponseBody(
                request_, kint64max, &original_size, &censored));
  EXPECT_FALSE(censored);
  EXPECT_EQ(kResponseBody.size(), original_size);

  // Test boundary conditions on eliding.
  // Just the right size.
  EXPECT_EQ(kResponseBody,
            censor->GetCensoredResponseBody(
                request_, kResponseBody.size(), &original_size, &censored));
  EXPECT_EQ(kResponseBody.size(), original_size);

  // Just short by 1 so add ellipses.
  EXPECT_EQ(StrCat(kResponseBody.substr(0, kResponseBody.size() - 4), "..."),
            censor->GetCensoredResponseBody(
                request_, kResponseBody.size() - 1,
                &original_size, &censored));
  EXPECT_EQ(kResponseBody.size(), original_size);

  // Not even big enough for ellipses.
  EXPECT_EQ(".",
            censor->GetCensoredResponseBody(
                request_, 1, &original_size, &censored));
  EXPECT_EQ(kResponseBody.size(), original_size);
}

TEST_F(HttpScribeTestFixture, CensorPartialResponseBody) {
  const StringPiece kResponseBody(
      "{\"A\":\"ok\", \"refresh_token\" :  \"X\"}");
  const StringPiece kCensoredBody(
      "{\"A\":\"ok\", \"refresh_token\" :  \"CENSORED\"}");
  std::unique_ptr<HttpScribeCensor> censor(new HttpScribeCensor);
  request_.set_url("https://www.google.com");
  HttpResponse* response = request_.response();
  response->set_body_reader(NewUnmanagedInMemoryDataReader(kResponseBody));
  response->AddHeader(HttpRequest::HttpHeader_CONTENT_TYPE, "text/plain");

  bool censored = true;
  int64 original_size = 0;
  string got = censor->GetCensoredResponseBody(
      request_, kint64max, &original_size, &censored);
  EXPECT_FALSE(censored);
  EXPECT_EQ(kResponseBody.size(), original_size);
  EXPECT_EQ(kResponseBody, got);

  response->ClearHeaders();
  response->AddHeader(HttpRequest::HttpHeader_CONTENT_TYPE,
                      HttpRequest::ContentType_JSON);

  got = censor->GetCensoredResponseBody(
      request_, kint64max, &original_size, &censored);
  EXPECT_TRUE(censored);
  EXPECT_EQ(kResponseBody.size(), original_size);
  EXPECT_EQ(kCensoredBody, got);
}

TEST_F(HttpScribeTestFixture, CensorRequestHeader) {
  std::unique_ptr<HttpScribeCensor> censor(new HttpScribeCensor);
  request_.set_url("https://www.google.com");

  string value("value");
  bool censored = true;
  EXPECT_EQ(
      value,
      censor->GetCensoredRequestHeaderValue(
          request_, HttpRequest::HttpHeader_USER_AGENT, value, &censored));
  EXPECT_FALSE(censored);

  EXPECT_EQ(
      "CENSORED",
      censor->GetCensoredRequestHeaderValue(
          request_, HttpRequest::HttpHeader_AUTHORIZATION, value, &censored));
  EXPECT_TRUE(censored);
}

TEST_F(HttpScribeTestFixture, ScribeRequestResponse) {
  int http_code[] = { 200, 400 };

  // Same flow whether we have an OK or ERROR http response.
  for (int i = 0; i < ARRAYSIZE(http_code); ++i) {
    MockHttpEntryScribe scribe(new HttpScribeCensor);
    MockHttpTransport transport;
    transport.set_scribe(&scribe);
    MockHttpRequest request(HttpRequest::GET, &transport);
    MockEntry entry(&scribe, &request);

    typedef Callback1<HttpResponse*> DoExecuteCallbackType;
    DoExecuteCallbackType* set_http_code =
        NewCallback(&SetHttpCode, http_code[i]);

    EXPECT_CALL(scribe, NewEntry(&request)).WillOnce(Return(&entry));
    EXPECT_CALL(entry, Sent(&request)).Times(1);
    EXPECT_CALL(entry, Received(&request)).Times(1);

    // FlushAndDestroy is responsible for deleting entry, but we created on
    // the stack so no need.
    EXPECT_CALL(entry, FlushAndDestroy()).Times(1);
    EXPECT_CALL(request, DoExecute(_))
        .WillOnce(Invoke(set_http_code, &DoExecuteCallbackType::Run));
    EXPECT_EQ(request.Execute().ok(), http_code[i] == 200);
  }
}

TEST_F(HttpScribeTestFixture, ScribeRequestFailure) {
  MockHttpEntryScribe scribe(new HttpScribeCensor);
  MockHttpTransport transport;
  transport.set_scribe(&scribe);
  MockHttpRequest request(HttpRequest::GET, &transport);
  MockEntry entry(&scribe, &request);

  googleapis::util::Status error = client::StatusInternalError("Failed");
  typedef Callback1<HttpResponse*> DoExecuteCallbackType;
  DoExecuteCallbackType* set_transport_status =
      NewCallback(&SetTransportStatus, error);

  EXPECT_CALL(scribe, NewEntry(&request)).WillOnce(Return(&entry));
  EXPECT_CALL(entry, Sent(&request)).Times(1);
  EXPECT_CALL(entry, Failed(&request, error)).Times(1);

  // FlushAndDestroy is responsible for deleting entry, but we created on
  // the stack so no need.
  EXPECT_CALL(entry, FlushAndDestroy()).Times(1);
  EXPECT_CALL(request, DoExecute(_))
      .WillOnce(Invoke(set_transport_status, &DoExecuteCallbackType::Run));
  EXPECT_FALSE(request.Execute().ok());
}

TEST_F(HttpScribeTestFixture, HttpRequestCensoring) {
  const string kUrl = "THE URL";
  const string kRequestHeader = "A-REQUEST-HEADER";
  const string kRequestHeaderValue = "REQUEST HEADER VALUE";
  const string kResponseHeader = "A-RESPONSE-HEADER";
  const string kResponseHeaderValue = "RESPONSE HEADER VALUE";
  const string kRequestContent = "REQUEST CONTENT";
  const string kResponseBody = "RESPONSE BODY";

  MockHttpTransport transport;
  MockHttpRequest request(HttpRequest::POST, &transport);
  EXPECT_EQ(request.scribe_restrictions(), HttpScribe::ALLOW_EVERYTHING);

  request.set_url(kUrl);
  request.AddHeader(kRequestHeader, kRequestHeaderValue);
  request.set_content_reader(NewUnmanagedInMemoryDataReader(kRequestContent));
  HttpResponse* response = request.response();
  response->AddHeader(kResponseHeader, kResponseHeaderValue);
  response->set_body_reader(NewUnmanagedInMemoryDataReader(kResponseBody));

  int tests[] = {
    HttpScribe::FLAG_NO_URL,
    HttpScribe::FLAG_NO_REQUEST_HEADERS,
    HttpScribe::FLAG_NO_RESPONSE_HEADERS,
    HttpScribe::FLAG_NO_REQUEST_PAYLOAD,
    HttpScribe::FLAG_NO_RESPONSE_PAYLOAD,
    HttpScribe::MASK_NO_HEADERS,
    HttpScribe::MASK_NO_PAYLOADS,
    HttpScribe::MASK_NOTHING,
  };

  // Verify masks have expected bits set
  EXPECT_EQ(HttpScribe::MASK_NO_HEADERS,
            HttpScribe::FLAG_NO_REQUEST_HEADERS
            | HttpScribe::FLAG_NO_RESPONSE_HEADERS);

  EXPECT_EQ(HttpScribe::MASK_NO_PAYLOADS,
            HttpScribe::FLAG_NO_REQUEST_PAYLOAD
            | HttpScribe::FLAG_NO_RESPONSE_PAYLOAD);

  EXPECT_EQ(HttpScribe::MASK_NOTHING,
            HttpScribe::FLAG_NO_URL
            | HttpScribe::FLAG_NO_REQUEST_HEADERS
            | HttpScribe::FLAG_NO_REQUEST_PAYLOAD
            | HttpScribe::FLAG_NO_RESPONSE_HEADERS
            | HttpScribe::FLAG_NO_RESPONSE_PAYLOAD);

  // Check different hard-wired censoring into the requests.
  HttpScribeCensor censor;
  for (int i = 0; i < ARRAYSIZE(tests); ++i) {
    string got;
    int64 original_size;
    bool censored;
    request.set_scribe_restrictions(tests[i]);

    got = censor.GetCensoredUrl(request, &censored);
    EXPECT_EQ((tests[i] & HttpScribe::FLAG_NO_URL) != 0, censored);
    EXPECT_EQ(!censored, got == kUrl);

    got = censor.GetCensoredRequestHeaderValue(
        request, kRequestHeader, kRequestHeaderValue, &censored);
    EXPECT_EQ((tests[i] & HttpScribe::FLAG_NO_REQUEST_HEADERS) != 0, censored);
    EXPECT_EQ(!censored, got == kRequestHeaderValue);

    got = censor.GetCensoredResponseHeaderValue(
        request, kResponseHeader, kResponseHeaderValue, &censored);
    EXPECT_EQ((tests[i] & HttpScribe::FLAG_NO_RESPONSE_HEADERS) != 0, censored);
    EXPECT_EQ(!censored, got == kResponseHeaderValue);

    got = censor.GetCensoredRequestContent(
        request, kint64max, &original_size, &censored);
    EXPECT_EQ((tests[i] & HttpScribe::FLAG_NO_REQUEST_PAYLOAD) != 0, censored);
    EXPECT_EQ(kRequestContent.size(), original_size);
    EXPECT_EQ(!censored, got == kRequestContent);

    got = censor.GetCensoredResponseBody(
        request, kint64max, &original_size, &censored);
    EXPECT_EQ((tests[i] & HttpScribe::FLAG_NO_RESPONSE_PAYLOAD) != 0, censored);
    EXPECT_EQ(kResponseBody.size(), original_size);
    EXPECT_EQ(!censored, got == kResponseBody);
  }
}

}  // namespace googleapis
