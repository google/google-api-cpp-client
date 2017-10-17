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


#include <cstdio>
#include <utility>
#include <vector>

#include "googleapis/client/data/data_reader.h"
#include "googleapis/client/transport/http_request.h"
#include "googleapis/client/transport/http_request_batch.h"
#include "googleapis/client/transport/http_response.h"
#include "googleapis/client/transport/test/mock_http_transport.h"
#include "googleapis/client/util/status.h"
#include "googleapis/strings/stringpiece.h"
#include "googleapis/strings/strcat.h"
#include "googleapis/strings/util.h"
#include <gmock/gmock.h>
#include <gtest/gtest.h>

namespace googleapis {

using testing::_;
using testing::InvokeWithoutArgs;
using testing::DoAll;
using testing::Return;

using client::AuthorizationCredential;
using client::HttpRequestBatch;
using client::DataReader;
using client::HttpRequest;
using client::HttpRequestCallback;
using client::HttpResponse;
using client::HttpStatusCode;
using client::MockHttpRequest;
using client::MockHttpTransport;
using client::kCRLF;

const char kAuthorizationHeader[] = "TestAuthorizationHeader";
class FakeCredential : public AuthorizationCredential {
 public:
  explicit FakeCredential(const StringPiece& value)
      : value_(value.as_string()) {
  }
  virtual ~FakeCredential() {}
  const string& fake_value() const { return value_; }

  virtual const string type() const { return "FAKE"; }
  virtual googleapis::util::Status Refresh() {
    LOG(FATAL) << "Not expected";
    return client::StatusUnimplemented("Not expected");
  }
  virtual void RefreshAsync(Callback1<util::Status>* callback) {
    LOG(FATAL) << "Not expected";
    callback->Run(client::StatusUnimplemented("Not expected"));
  }
  virtual googleapis::util::Status Load(DataReader* reader) {
    LOG(FATAL) << "Not expected";
    return client::StatusUnimplemented("Not expected");
  }
  virtual DataReader* MakeDataReader() const {
    LOG(FATAL) << "Not expected";
    return NULL;
  }
  virtual googleapis::util::Status AuthorizeRequest(HttpRequest* request) {
    request->AddHeader(kAuthorizationHeader, value_);
    return client::StatusOk();
  }

 private:
  string value_;
  DISALLOW_COPY_AND_ASSIGN(FakeCredential);
};

struct BatchTestCase {
  BatchTestCase(HttpRequest::HttpMethod meth,
                int code,
                FakeCredential* cred = NULL,
                HttpRequestCallback* cb = NULL)
      : method(meth), http_code(code), credential(cred), callback(cb),
        create_directly_in_batch(true), respond_out_of_order(false) {
  }

  HttpRequest::HttpMethod method;
  int http_code;
  FakeCredential* credential;
  HttpRequestCallback* callback;
  bool create_directly_in_batch;
  bool respond_out_of_order;  // Defer my response until after the others.
};

static void DeleteStrings(string* a, string* b, string* c) {
  delete a;
  delete b;
  delete c;
}

class BatchTestFixture : public testing::Test {
 protected:
  static void PokeResponseBody(string* body, HttpRequest* request) {
    EXPECT_TRUE(request->response()->body_writer()->Write(*body).ok());
  }

  HttpRequestBatch* MakeBatchRequest(
      const std::vector<BatchTestCase>& method_and_response,
      FakeCredential* credential,
      string** mock_response_ptr = NULL) {
    MockHttpRequest* mock_request =
        new MockHttpRequest(HttpRequest::POST, &transport_);

    EXPECT_CALL(transport_, NewHttpRequest(HttpRequest::POST))
        .WillOnce(Return(mock_request));
    HttpRequestBatch* batch = new HttpRequestBatch(&transport_);

    const string response_boundary = "_xxxxxx_";
    string* mock_response = new string;
    string* out_of_order_responses = new string;
    for (int i = 0; i < method_and_response.size(); ++i) {
      const BatchTestCase& test = method_and_response[i];
      const string url = StrCat("http://test/", i);
      HttpRequest* batched_request;
      if (test.create_directly_in_batch) {
        batched_request = batch->NewHttpRequest(test.method, test.callback);
      } else {
        batched_request = new MockHttpRequest(test.method, &transport_);
      }
      batched_request->set_url(StrCat("http://test/", i));
      for (int j = 0; j < 3; ++j) {
        batched_request->AddHeader(
            StrCat("TestHeader_", j), StrCat("Header Value ", j));
      }
      if (!test.create_directly_in_batch) {
        // Turn the independent mock request into a batch request then
        // destroy the original mock and giving us the batched replacement.
        batched_request =
            batch->AddFromGenericRequestAndRetire(
                batched_request, test.callback);
      }

      // The response for this request.
      string this_mock_response;
      StrAppend(&this_mock_response, "--", response_boundary, kCRLF,
                "Content-Type: application/http", kCRLF,
                "Content-ID: <response-",
                HttpRequestBatch::PointerToHex(batched_request), ">", kCRLF);
      StrAppend(&this_mock_response,
                kCRLF,
                "HTTP/1.1 ", test.http_code, " StatusSummary", kCRLF);
      StrAppend(&this_mock_response,
                "ResponseHeaderA: response A.", i, kCRLF,
                "ResponseHeaderB: response B.", i, kCRLF,
                kCRLF,
                "Response Body ", i);

      if (test.respond_out_of_order) {
        if (!out_of_order_responses->empty()) {
          StrAppend(out_of_order_responses, kCRLF);
        }
        StrAppend(out_of_order_responses, this_mock_response);
      } else {
        if (!mock_response->empty()) {
          StrAppend(mock_response, kCRLF);
        }
        StrAppend(mock_response, this_mock_response);
      }
    }
    if (!out_of_order_responses->empty()) {
      StrAppend(mock_response, kCRLF);
      StrAppend(mock_response, *out_of_order_responses);
    }
    StrAppend(mock_response, kCRLF, "--", response_boundary, "--", kCRLF);

    Closure* poke_http_code =
        NewCallback(mock_request, &MockHttpRequest::poke_http_code, 200);
    string* contenttype = new string(
        StrCat("multipart/mixed; boundary=", response_boundary));
    Closure* poke_response_header =
        NewCallback(mock_request, &MockHttpRequest::poke_response_header,
                    "Content-Type", *contenttype);
    Closure* poke_response_body =
        NewCallback(*PokeResponseBody, mock_response, mock_request);
    Closure* delete_strings =
        NewCallback(&DeleteStrings, mock_response, out_of_order_responses,
                    contenttype);

    EXPECT_CALL(*mock_request, DoExecute(_))
        .WillOnce(DoAll(
            InvokeWithoutArgs(poke_http_code, &Closure::Run),
            InvokeWithoutArgs(poke_response_header, &Closure::Run),
            InvokeWithoutArgs(poke_response_body, &Closure::Run),
            InvokeWithoutArgs(delete_strings, &Closure::Run)));

    if (mock_response_ptr) {
      *mock_response_ptr = mock_response;
    }
    return batch;
  }

  void CheckResponse(
      const std::vector<BatchTestCase>& tests,
      const std::vector<HttpRequest*>& parts) {
    for (int i = 0; i < tests.size(); ++i) {
      HttpResponse* response = parts[i]->response();
      EXPECT_EQ(tests[i].http_code, response->http_code());
      CheckResponseContent(response, i);
    }
  }

  void CheckResponseContent(HttpResponse* response, int position) {
    DataReader* reader = response->body_reader();
    EXPECT_TRUE(reader != NULL);
    if (reader == NULL) return;

    EXPECT_EQ(StrCat("Response Body ", position), reader->RemainderToString());
    const string* value;
    value = response->FindHeaderValue("ResponseHeaderA");
    ASSERT_TRUE(value != NULL);
    EXPECT_EQ(StrCat("response A.", position), *value);
    value = response->FindHeaderValue("ResponseHeaderB");
    ASSERT_TRUE(value != NULL);
    EXPECT_EQ(StrCat("response B.", position), *value);
  }

  MockHttpTransport transport_;
};


TEST_F(BatchTestFixture, TestAllOk) {
  std::vector<BatchTestCase> tests;
  tests.push_back(BatchTestCase(HttpRequest::GET, 200));
  tests.push_back(BatchTestCase(HttpRequest::GET, 200));
  tests.push_back(BatchTestCase(HttpRequest::GET, 200));

  std::unique_ptr<HttpRequestBatch> batch(MakeBatchRequest(tests, NULL));
  EXPECT_EQ("https://www.googleapis.com/batch", batch->http_request().url());
  CHECK_EQ(batch->requests().size(), tests.size());

  googleapis::util::Status status = batch->Execute();
  EXPECT_TRUE(status.ok()) << status.error_message();
  CheckResponse(tests, batch->requests());
}

TEST_F(BatchTestFixture, TestPartialFailure) {
  std::vector<BatchTestCase> tests;
  tests.push_back(BatchTestCase(HttpRequest::GET, 200));
  tests.push_back(BatchTestCase(HttpRequest::GET, 400));
  tests.push_back(BatchTestCase(HttpRequest::GET, 200));
  tests.push_back(BatchTestCase(HttpRequest::GET, 500));

  std::unique_ptr<HttpRequestBatch> batch(MakeBatchRequest(tests, NULL));

  googleapis::util::Status status = batch->Execute();
  EXPECT_TRUE(status.ok()) << status.error_message();

  CheckResponse(tests, batch->requests());
}

TEST_F(BatchTestFixture, TestWithCredentials) {
  // Use the repeated and different credentials for different messages.
  FakeCredential outer_credential("OuterCredential");
  FakeCredential override_credential_a("CredentialA");
  FakeCredential override_credential_b("CredentialB");
  std::vector<BatchTestCase> tests;
  tests.push_back(
      BatchTestCase(HttpRequest::GET, 200, &override_credential_a));
  tests.push_back(BatchTestCase(HttpRequest::GET, 200,
                                &override_credential_a));
  tests.push_back(BatchTestCase(HttpRequest::GET, 200));
  tests.push_back(BatchTestCase(HttpRequest::GET, 200,
                                &override_credential_b));

  std::unique_ptr<HttpRequestBatch> batch(
      MakeBatchRequest(tests, &outer_credential));
  CHECK_EQ(batch->requests().size(), tests.size());

  googleapis::util::Status status = batch->Execute();
  EXPECT_TRUE(status.ok()) << status.error_message();
  CheckResponse(tests, batch->requests());
}

static void DoCallback(
    int* count,
    util::error::Code expect_code,
    HttpRequest* request) {
  EXPECT_EQ(0, *count) << "id=" << count;
  EXPECT_EQ(expect_code,
            request->response()->transport_status().error_code());
  ++*count;
}

TEST_F(BatchTestFixture, TestWithCallback) {
  std::vector<BatchTestCase> tests;
  int call_count = 0;
  HttpRequestCallback* test_callback(
      NewCallback(&DoCallback, &call_count, util::error::OK));
  tests.push_back(BatchTestCase(HttpRequest::GET, 200, NULL, test_callback));

  std::unique_ptr<HttpRequestBatch> batch(MakeBatchRequest(tests, NULL));
  CHECK_EQ(tests.size(), batch->requests().size());

  EXPECT_EQ(0, call_count);
  googleapis::util::Status status = batch->Execute();
  EXPECT_TRUE(status.ok()) << status.error_message();
  EXPECT_EQ(1, call_count);
  CheckResponse(tests, batch->requests());
}

TEST_F(BatchTestFixture, TestDeleteWithCallback) {
  int call_count = 0;
  std::vector<BatchTestCase> tests;
  tests.push_back(BatchTestCase(HttpRequest::GET, 200));
  tests.push_back(BatchTestCase(HttpRequest::GET, 200));
  std::unique_ptr<HttpRequestBatch> batch(MakeBatchRequest(tests, NULL));

  HttpRequestCallback* test_callback(
      NewCallback(&DoCallback, &call_count, util::error::ABORTED));
  HttpRequest* to_delete = batch->AddFromGenericRequestAndRetire(
      new MockHttpRequest(HttpRequest::GET, &transport_), test_callback);

  CHECK_EQ(tests.size() + 1, batch->requests().size());
  EXPECT_EQ(0, call_count);
  EXPECT_TRUE(batch->RemoveAndDestroyRequest(to_delete).ok());
  EXPECT_EQ(1, call_count);
  CHECK_EQ(tests.size(), batch->requests().size());

  googleapis::util::Status status = batch->Execute();
  EXPECT_TRUE(status.ok()) << status.error_message();
  CheckResponse(tests, batch->requests());
}

TEST_F(BatchTestFixture, TestBatchAfterCreation) {
  std::vector<BatchTestCase> tests;
  tests.push_back(BatchTestCase(HttpRequest::GET, 200));
  tests.push_back(BatchTestCase(HttpRequest::GET, 200));
  tests.push_back(BatchTestCase(HttpRequest::GET, 200));
  for (int i = 0; i < tests.size(); ++i) {
    tests[i].create_directly_in_batch = false;
  }

  std::unique_ptr<HttpRequestBatch> batch(MakeBatchRequest(tests, NULL));
  CHECK_EQ(batch->requests().size(), tests.size());

  googleapis::util::Status status = batch->Execute();
  EXPECT_TRUE(status.ok()) << status.error_message();
  CheckResponse(tests, batch->requests());
}

TEST_F(BatchTestFixture, TestMissingAndUnexpectedResponse) {
  std::vector<BatchTestCase> tests;
  tests.push_back(BatchTestCase(HttpRequest::GET, 200));
  tests.push_back(BatchTestCase(HttpRequest::GET, 400));
  tests.push_back(BatchTestCase(HttpRequest::GET, 200));
  tests.push_back(BatchTestCase(HttpRequest::GET, 500));

  string* mock_response;
  std::unique_ptr<HttpRequestBatch> batch(
      MakeBatchRequest(tests, NULL, &mock_response));

  string third_result_id = HttpRequestBatch::PointerToHex(batch->requests()[2]);
  string hacked = StringReplace(
      *mock_response, third_result_id, "INVALID", true);
  *mock_response = hacked;

  googleapis::util::Status status = batch->Execute();
  EXPECT_FALSE(status.ok());
  EXPECT_EQ(batch->batch_processing_status(), status);
  EXPECT_TRUE(batch->http_request().response()->ok());

  for (int i = 0; i < tests.size(); ++i) {
    HttpRequest* request = batch->requests()[i];
    EXPECT_EQ(i != 2, request->response()->transport_status().ok())
        << "i=" << i;
    if (i == 2) {
      EXPECT_EQ(0, request->response()->http_code());
    } else {
      EXPECT_EQ(tests[i].http_code, request->response()->http_code());
    }
  }
}

// Make sure that responses get correlated to the proper request.
TEST_F(BatchTestFixture, TestOutOfOrderResponse) {
  std::vector<BatchTestCase> tests;
  tests.push_back(BatchTestCase(HttpRequest::GET, 200));
  tests.push_back(BatchTestCase(HttpRequest::GET, 200));
  tests.push_back(BatchTestCase(HttpRequest::GET, 200));
  tests.push_back(BatchTestCase(HttpRequest::GET, 200));

  tests.at(1).respond_out_of_order = true;

  std::unique_ptr<HttpRequestBatch> batch(MakeBatchRequest(tests, NULL));
  EXPECT_EQ("https://www.googleapis.com/batch", batch->http_request().url());
  CHECK_EQ(batch->requests().size(), tests.size());

  googleapis::util::Status status = batch->Execute();
  EXPECT_TRUE(status.ok()) << status.error_message();
  CheckResponse(tests, batch->requests());
}

TEST_F(BatchTestFixture, TestPerApiEndpointCtor) {
  MockHttpRequest* mock_request =
      new MockHttpRequest(HttpRequest::POST, &transport_);
  EXPECT_CALL(transport_, NewHttpRequest(HttpRequest::POST))
      .WillOnce(Return(mock_request));
  std::unique_ptr<HttpRequestBatch> batch(
      new HttpRequestBatch(&transport_, "https://google.com/myapi/batch"));
  EXPECT_EQ("https://google.com/myapi/batch", batch->http_request().url());
}

}  // namespace googleapis
