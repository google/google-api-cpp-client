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
// TODO(user): 20130117
// Make this test independent of the wax json service.
// Instead add some other simple PUBLIC service or mock
// that can be open sourced.

#ifndef GOOGLEAPIS_HTTP_TRANSPORT_TEST_FIXTURE_H_
#define GOOGLEAPIS_HTTP_TRANSPORT_TEST_FIXTURE_H_

#include "googleapis/client/transport/http_transport.h"
#include <gtest/gtest.h>

namespace google_wax_api {
class WaxService;
}  // namespace google_wax_api
namespace googleapis {

namespace client {
class HttpTransportFactory;
}  // namespace client

/*
 * Test fixture for verifying basic HttpTransport implementations.
 * @ingroup TransportLayerTesting
 *
 * This class is compiled into a library that runs tests against the
 * core HttpTransport interface. To test a specific implementation
 * you should set up a HttpTransportLayerConfiguration that uses that
 * implementation then HttpTransportTestFixture::SetTestConfiguration
 * to inject the configuration before running the tests.
 */
class HttpTransportTestFixture : public testing::Test {
 public:
  static google_wax_api::WaxService& GetGlobalWaxService();
  static const string& GetGlobalSessionId();
  static void ResetGlobalSessionId();
  static void SetUpTestCase();
  static void TearDownTestCase();

  static void SetTestConfiguration(
      const client::HttpTransportLayerConfig* config);

  HttpTransportTestFixture();
  virtual ~HttpTransportTestFixture();

 private:
  DISALLOW_COPY_AND_ASSIGN(HttpTransportTestFixture);
};

}  // namespace googleapis
#endif  // GOOGLEAPIS_HTTP_TRANSPORT_TEST_FIXTURE_H_
