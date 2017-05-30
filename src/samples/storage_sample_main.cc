/*
 * \copyright Copyright 2017 Google Inc. All Rights Reserved.
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

#include <fstream>
#include <memory>
#include "gflags/gflags.h"
#include "googleapis/client/auth/oauth2_authorization.h"
#include "googleapis/client/auth/oauth2_service_authorization.h"
#include "googleapis/client/data/data_reader.h"
#if HAVE_OPENSSL
#include "googleapis/client/data/openssl_codec.h"
#endif
#include "googleapis/client/transport/curl_http_transport.h"
#include "googleapis/client/transport/http_authorization.h"
#include "googleapis/client/transport/http_transport.h"
#include "googleapis/client/transport/http_request_batch.h"
#include "googleapis/client/util/status.h"
#include "googleapis/strings/strcat.h"

#include "google/storage_api/storage_api.h"  // NOLINT

namespace googleapis {

static const char usage[] =
    "List bucket links.\n"
    "\n"
    "This is a sample application illustrating the use of the GoogleApis C++\n"
    "Client. The application makes calls into the Google Cloud Storage API.\n"
    "The application itself is not particularly useful, rather it just\n"
    "illustrates how to interact with a live service.\n"
    "\n"
    "Usage:\n"
    "\tstorage_sample <service_account.json> [<cacerts_path>]\n"
    "\n"
    "Output:\n"
    "\n"
    "A list of Google Cloud Storage bucket links.\n"
    "\n"
    "Example:\n"
    "\t$ bin/storage_sample your-project-id-1cf578086929.json\n"
    "\thttps://www.googleapis.com/storage/v1/b/1gallon\n"
    "\thttps://www.googleapis.com/storage/v1/b/2gallon\n"
    "\thttps://www.googleapis.com/storage/v1/b/5gallon\n"
    "\tDone!\n"
    "\n"
    "\n"
    "See README.md for more details.\n";

using google_storage_api::StorageService;

using client::HttpTransport;
using client::HttpTransportLayerConfig;
using client::OAuth2Credential;
using client::OAuth2ServiceAccountFlow;
#if HAVE_OPENSSL
using client::OpenSslCodecFactory;
#endif
using client::StatusInvalidArgument;

class StorageSample {
 public:
  googleapis::util::Status Startup(int argc, char* argv[]);
  void Run();

 private:

  OAuth2Credential credential_;
  std::unique_ptr<StorageService> storage_;
  std::unique_ptr<OAuth2ServiceAccountFlow> flow_;
  std::unique_ptr<HttpTransportLayerConfig> config_;
};

/* static */
util::Status StorageSample::Startup(int argc, char* argv[]) {
  if ((argc < 2) || (argc > 3)) {
    return StatusInvalidArgument(usage);
  }

  // Set up HttpTransportLayer.
  googleapis::util::Status status;
  config_.reset(new HttpTransportLayerConfig);
  client::HttpTransportFactory* factory =
      new client::CurlHttpTransportFactory(config_.get());
  config_->ResetDefaultTransportFactory(factory);
  if (argc > 2) {
    config_->mutable_default_transport_options()->set_cacerts_path(argv[2]);
  }

  // Set up OAuth 2.0 flow for a service account.
  flow_.reset(new client::OAuth2ServiceAccountFlow(
      config_->NewDefaultTransportOrDie()));
  // Load the the contents of the service_account.json into a string.
  string json(std::istreambuf_iterator<char>(std::ifstream(argv[1]).rdbuf()),
              std::istreambuf_iterator<char>());
  // Initialize the flow with the contents of service_account.json.
  flow_->InitFromJson(json);
  // Tell the flow exactly which scopes (priveleges) we need.
  flow_->set_default_scopes(StorageService::SCOPES::DEVSTORAGE_READ_ONLY);

  storage_.reset(new StorageService(config_->NewDefaultTransportOrDie()));
  return status;
}


void StorageSample::Run() {
  // Connect the credential passed to ListBuckets() with the AuthFlow
  // constructed in Startup().
  credential_.set_flow(flow_.get());
  // Construct the request.
  google_storage_api::BucketsResource_ListMethod request(
      storage_.get(), &credential_, flow_->project_id());
  Json::Value value;
  google_storage_api::Buckets buckets(&value);
  // Execute the request.
  auto status = request.ExecuteAndParseResponse(&buckets);
  if (!status.ok()) {
    std::cout << "Could not list buckets: " << status.error_message()
              << std::endl;
  }
  for (auto bucket: buckets.get_items()) {
    std::cout << bucket.get_self_link() << std::endl;
  }
}

}  // namespace googleapis

using namespace googleapis;
int main(int argc, char* argv[]) {
  google::InitGoogleLogging(argv[0]);
  google::ParseCommandLineFlags(&argc, &argv, true);

  StorageSample sample;
  googleapis::util::Status status = sample.Startup(argc, argv);
  if (!status.ok()) {
    std::cerr << "Could not initialize application." << std::endl;
    std::cerr << status.error_message() << std::endl;
    return -1;
  }

  sample.Run();
  std::cout << "Done!" << std::endl;

  return 0;
}
