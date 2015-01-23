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


#include "googleapis/client/data/data_reader.h"
#include "googleapis/client/transport/json_playback_transport.h"
#include "googleapis/client/transport/http_scribe.h"
#include "googleapis/client/transport/http_transport.h"
#include "googleapis/client/transport/test/http_transport_test_fixture.h"
#include "googleapis/client/util/uri_utils.h"
#include <gflags/gflags.h>
#include "googleapis/strings/strcat.h"
#include <gtest/gtest.h>

DECLARE_bool(fork_wax);

namespace googleapis {

using client::DataReader;
using client::HttpTransport;
using client::JoinPath;
using client::JsonPlaybackTransportFactory;

// This runs the standard test suite defined by HttpTransportTestFixture
// but using the HttpOverRpcTransport factory.
}  // namespace googleapis

using namespace googleapis;
int main(int argc, char** argv) {
  FLAGS_logtostderr = true;
  FLAGS_fork_wax = false;
testing::InitGoogleTest(&argc, argv);

  client::HttpTransportLayerConfig config;
  JsonPlaybackTransportFactory* factory = new JsonPlaybackTransportFactory(
      &config);
  factory->ResetCensor(new client::HttpScribeCensor);
  const string data_dir = "src/googleapis/client/transport/test";
  {
    // To create this file, run another concrete transport test using this
    // fixture and the flag --http_scribe_path=json_transport_playback.json
    // then put that output file into the data_dir.
    //
    // For example:
    // curl_http_transport_test --http_scribe_path=json_transport_playback.json

    string path = JoinPath(data_dir, "json_transport_playback.json");

    std::unique_ptr<DataReader> reader(
        client::NewUnmanagedFileDataReader(path));
    EXPECT_TRUE(factory->LoadTranscript(reader.get()).ok());
  }

  CHECK_EQ(client::JsonPlaybackTransport::kTransportIdentifier,
           factory->default_id());
  std::unique_ptr<client::HttpTransport> check_instance(
      factory->New());
  CHECK_EQ(client::JsonPlaybackTransport::kTransportIdentifier,
           check_instance->id());

  config.ResetDefaultTransportFactory(factory);
  HttpTransportTestFixture::SetTestConfiguration(&config);

  return RUN_ALL_TESTS();
}
