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

/*
 * @defgroup TransportLayerTesting Transport Layer - Testing Support
 *
 * The Transport Layer Testing Support module contains classes and components
 * to facilitate testing, debugging, and diagnosing the transport layer.
 * In many cases these components can be used in place of normal production
 * components to test applications and libraries that use the transport layer.
 *
 * Components in this module are not typically deployed into production
 * applications as a matter of policy. There are not generally technical
 * reasons to prevent their inclusion.
 */
#ifndef GOOGLEAPIS_TRANSPORT_MOCK_HTTP_TRANSPORT_H_
#define GOOGLEAPIS_TRANSPORT_MOCK_HTTP_TRANSPORT_H_

#include <string>
using std::string;
#include "googleapis/client/data/data_reader.h"
#include "googleapis/client/data/data_writer.h"
#include "googleapis/client/transport/http_authorization.h"
#include "googleapis/client/transport/http_transport.h"
#include "googleapis/client/transport/http_request.h"
#include <gmock/gmock.h>
namespace googleapis {

namespace client {

/*
 * Mock transport error handler for testing with GMock.
 * @ingroup TransportLayerTesting
 */
class MockHttpTransportErrorHandler : public HttpTransportErrorHandler {
 public:
  MOCK_CONST_METHOD2(HandleTransportError, bool(int, HttpRequest*));  // NOLINT
  MOCK_CONST_METHOD2(HandleRedirect, bool(int, HttpRequest*));  // NOLINT
  MOCK_CONST_METHOD2(HandleHttpError, bool(int, HttpRequest*));  // NOLINT
};

/*
 * Mock authorization credential for testing with GMock.
 * @ingroup TransportLayerTesting
 */
class MockAuthorizationCredential : public AuthorizationCredential {
 public:
  MOCK_CONST_METHOD0(type, const string());
  MOCK_METHOD1(AuthorizeRequest, googleapis::util::Status(HttpRequest* request));
  MOCK_METHOD0(Refresh, googleapis::util::Status());
  MOCK_METHOD1(RefreshAsync, void(Callback1<util::Status>* callback));
  MOCK_METHOD1(Load, googleapis::util::Status(DataReader* reader));
  MOCK_CONST_METHOD0(MakeDataReader, DataReader*());
};

/*
 * Mock http request for testing GMock.
 * @ingroup TransportLayerTesting
 */
class MockHttpRequest : public HttpRequest {
 public:
  typedef Callback1<HttpResponse*> DoExecuteCallback;

  MockHttpRequest(HttpRequest::HttpMethod method, HttpTransport* transport)
      : HttpRequest(method, transport) {}
  MOCK_METHOD1(DoExecute, void(HttpResponse* response));

  void poke_http_code(int code) {
    response()->set_http_code(code);
  }

  void poke_transport_status(util::Status status) {
    mutable_state()->set_transport_status(status);
  }

  void poke_response_header(const string& name, const string& value) {
    response()->AddHeader(name, value);
  }

  void poke_response_body(const string& body) {
    EXPECT_TRUE(response()->body_writer()->Write(body).ok());
  }

  /*
   * Testing convienence method for checking the content_reader() value.
   *
   * This method will rewind the reader if it had already been called so
   * is safe to grab multiple values.
   *
   * @return content_reader payload value as a string.
   */
  const string content_as_string() {
    DataReader* reader = content_reader();
    CHECK(reader);
    if (reader->offset()) {
      EXPECT_TRUE(reader->Reset());
    }
    return reader->RemainderToString();
  }

  /*
   * Testing convienence method for checking the response body_reader() value.
   *
   * This method will rewind the reader if it had already been called so
   * is safe to grab multiple values.
   *
   * @return response_body payload value as a string.
   */
  const string response_body_as_string() {
    CHECK(response() != NULL);
    DataReader* reader = response()->body_reader();
    CHECK(reader != NULL);
    if (!reader->offset()) {
      EXPECT_TRUE(reader->Reset());
    }
    return reader->RemainderToString();
  }

  /*
   * Testing convienence method added to check header values.
   *
   * Fails if the header was not present or value does not match.
   *
   * @param[in] name Header to check
   * @param[in] value Value to confirm
   */
  void CheckHeader(const string& name, const string& value) {
    const string* have = FindHeaderValue(name);
    EXPECT_TRUE(have != NULL) << "Did not find header=" << name;
    if (have && !value.empty()) {
      EXPECT_EQ(value, *have) << "header=" << name;
    }
  }
};

/*
 * Mock http request for testing GMock.
 * @ingroup TransportLayerTesting
 */
class MockHttpTransport : public HttpTransport {
 public:
  MockHttpTransport() : HttpTransport(HttpTransportOptions()) {
    set_id("MockHttpTransport");
  }
  explicit MockHttpTransport(
      const client::HttpTransportOptions& options)
      : HttpTransport(options) {
  }

  MOCK_METHOD1(
      NewHttpRequest, HttpRequest*(const HttpRequest::HttpMethod& method));
};

/*
 * Mock http transport factory for testing GMock.
 * @ingroup TransportLayerTesting
 */
class MockHttpTransportFactory : public HttpTransportFactory {
 public:
  MockHttpTransportFactory() {
    set_default_id("MockHttpTransport");
  }
  explicit MockHttpTransportFactory(const HttpTransportLayerConfig* config)
      : HttpTransportFactory(config) {
    set_default_id("MockHttpTransport");
  }
  MOCK_METHOD1(DoAlloc, HttpTransport*(const HttpTransportOptions& options));
};

}  // namespace client

}  // namespace googleapis
#endif  // GOOGLEAPIS_TRANSPORT_MOCK_HTTP_TRANSPORT_H_
