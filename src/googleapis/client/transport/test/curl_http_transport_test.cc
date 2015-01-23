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


#include "googleapis/client/transport/curl_http_transport.h"
#include "googleapis/client/transport/http_transport.h"
#include "googleapis/client/transport/test/http_transport_test_fixture.h"

#include <gflags/gflags.h>
#include <gtest/gtest.h>

namespace googleapis {


// This runs the standard test suite defined by HttpTransportTestFixture
// but using the CurlHttpTransportFactory.
}  // namespace googleapis

using namespace googleapis;
int main(int argc, char** argv) {
  google::ParseCommandLineFlags(&argc, &argv, false);   // init gflags
  ::testing::InitGoogleTest(&argc, argv);               // init gtest
  client::HttpTransportLayerConfig config;
  client::HttpTransportFactory* factory =
      new client::CurlHttpTransportFactory(&config);

  CHECK_EQ(client::CurlHttpTransport::kTransportIdentifier,
           factory->default_id());
  std::unique_ptr<client::HttpTransport> check_instance(
      factory->New());
  CHECK_EQ(client::CurlHttpTransport::kTransportIdentifier,
           check_instance->id());

  config.ResetDefaultTransportFactory(factory);
  HttpTransportTestFixture::SetTestConfiguration(&config);

  return RUN_ALL_TESTS();
}
