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
// These tests generally do not test the actual HTML produced,
// just certain properties of it as to whether it contains key
// tags and/or pieces of information.

#include "googleapis/client/transport/html_scribe.h"

#include "googleapis/client/transport/http_request_batch.h"
#include "googleapis/client/data/data_reader.h"
#include "googleapis/client/data/data_writer.h"
#include "googleapis/client/transport/http_request.h"
#include "googleapis/client/transport/http_response.h"
#include "googleapis/client/transport/test/mock_http_transport.h"
#include "googleapis/client/util/status.h"
#include "googleapis/base/callback.h"
#include "googleapis/strings/strcat.h"
#include "googleapis/strings/stringpiece.h"
#include <gmock/gmock.h>
#include <gtest/gtest.h>

namespace googleapis {

using testing::Return;
using client::HttpRequestBatch;
using client::DataWriter;
using client::HtmlScribe;
using client::HttpRequest;
using client::HttpResponse;
using client::MockHttpRequest;
using client::MockHttpTransport;
using client::NewUnmanagedInMemoryDataReader;

const char kUrl[] = "http://test.org/url?param=1";
const char kResponse[] = "Message Response Body";
const char kRequest[] = "Message Request Content";
const char kCustomRequestHeader[] = "CustomRequestHeader";
const char kCustomRequestValue[] = "Custom Value";
const char kCustomResponseHeader[] = "CustomRequestHeader";
const char kCustomResponseValue[] = "Custom Value";
const char kAuthorization[] = "Token1234abcdef";


class HtmlScribeTestFixture : public testing::Test {
 public:
  HtmlScribeTestFixture() {}
  virtual ~HtmlScribeTestFixture() {}

  virtual void SetUp() {
    output_.clear();
    scribe_.reset(
        new HtmlScribe(new client::HttpScribeCensor(),
                       "Test HTML",
                       client::NewStringDataWriter(&output_)));
  }

 protected:
  string output_;
  std::unique_ptr<HtmlScribe> scribe_;

 private:
  DISALLOW_COPY_AND_ASSIGN(HtmlScribeTestFixture);
};

TEST_F(HtmlScribeTestFixture, TestInitialization) {
  // We wrote a title and header
  EXPECT_NE(string::npos, output_.find("<title>Test HTML</title>"));
  EXPECT_NE(string::npos, output_.find("</head>"));

  // We started a body
  EXPECT_NE(string::npos, output_.find("<body>"));

  // Body was left open to concat into.
  EXPECT_EQ(string::npos, output_.find("</body>"));
  EXPECT_EQ(string::npos, output_.find("</html>"));
}

void InitRequest(HttpRequest* request) {
  request->set_url(kUrl);
  request->AddHeader(kCustomRequestHeader, kCustomRequestValue);
  request->AddHeader(HttpRequest::HttpHeader_AUTHORIZATION, kAuthorization);
  request->response()->set_http_code(200);
  request->response()->AddHeader(kCustomResponseHeader, kCustomResponseValue);
  request->response()->set_body_reader(
      NewUnmanagedInMemoryDataReader(kResponse));
}

TEST_F(HtmlScribeTestFixture, TestGet) {
  int64 starting_offset = output_.size();
  MockHttpTransport transport;
  MockHttpRequest request(HttpRequest::GET, &transport);
  InitRequest(&request);

  scribe_->AboutToSendRequest(&request);
  EXPECT_EQ(starting_offset, output_.size());

  scribe_->ReceivedResponseForRequest(&request);

  EXPECT_NE(starting_offset, output_.size());

  // Show custom headers.
  EXPECT_NE(string::npos, output_.find(kCustomRequestHeader));
  EXPECT_NE(string::npos, output_.find(kCustomRequestValue));

  // Dont show sensitive headers.
  EXPECT_NE(string::npos, output_.find(
      string(HttpRequest::HttpHeader_AUTHORIZATION)));
  EXPECT_EQ(string::npos, output_.find(kAuthorization));

  // Show response stuff.
  EXPECT_NE(string::npos, output_.find(kCustomResponseHeader));
  EXPECT_NE(string::npos, output_.find(kCustomResponseValue));
  EXPECT_NE(string::npos, output_.find(kResponse));

  // Leave body and document open.
  EXPECT_EQ(string::npos, output_.find("</body>"));
  EXPECT_EQ(string::npos, output_.find("</html>"));

  starting_offset = output_.size();
  delete scribe_.release();

  // Scribe will automatically close the document when destroyed.
  EXPECT_LT(starting_offset, output_.size());
  EXPECT_NE(string::npos, output_.find("</body>"));
  EXPECT_NE(string::npos, output_.find("</html>"));
}


TEST_F(HtmlScribeTestFixture, TestBatch) {
  MockHttpTransport transport;

  EXPECT_CALL(transport, NewHttpRequest(HttpRequest::POST))
      .WillOnce(Return(new MockHttpRequest(HttpRequest::POST, &transport)));
  int64 starting_offset = output_.size();
  HttpRequestBatch batch(&transport);
  HttpRequest* request = batch.NewHttpRequest(HttpRequest::DELETE);
  InitRequest(request);
  request->response()->set_http_code(432);

  const string kBatchRequest = "Batch Request Content";
  const string kBatchResponse = "Batch Response Body";
  const string kBatchAuth = "BatchAuthToken";
  HttpRequest* http_request = batch.mutable_http_request();
  http_request->response()->set_http_code(200);
  http_request->set_content_reader(
      NewUnmanagedInMemoryDataReader(kBatchRequest));
  http_request->response()->set_body_reader(
      NewUnmanagedInMemoryDataReader(kBatchResponse));
  http_request->AddHeader(HttpRequest::HttpHeader_AUTHORIZATION, kBatchAuth);

  scribe_->AboutToSendRequestBatch(&batch);
  scribe_->ReceivedResponseForRequestBatch(&batch);

  // We dont have the censored header, but we do have the regular one.
  EXPECT_EQ(string::npos, output_.find(kAuthorization, starting_offset));
  EXPECT_NE(string::npos, output_.find(kCustomRequestValue, starting_offset));

  // We dont have the censored header in the batch envelope either.
  EXPECT_EQ(string::npos, output_.find(kBatchAuth, starting_offset));

  // Both urls are there.
  EXPECT_NE(string::npos, output_.find(kUrl, starting_offset));
  EXPECT_NE(string::npos,
            output_.find("https://www.googleapis.com/batch", starting_offset));

  // We have the body content of the batched response.
  EXPECT_NE(string::npos, output_.find(kResponse, starting_offset));

  // But we do not have the body or response content of the batch message
  EXPECT_EQ(string::npos, output_.find(kBatchRequest, starting_offset));
  EXPECT_EQ(string::npos, output_.find(kBatchRequest, starting_offset));
}

}  // namespace googleapis
