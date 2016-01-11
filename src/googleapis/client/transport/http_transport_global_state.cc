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


// This file implements the global state extension of the HttpTransport layer.
// This state is not part of the core API or used in the implementation of the
// core API. It is made available to facilitate the use of the API,
// particularly for simple applications when getting started.
//
// The global state reduces the amount of setup code required, and amount
// of code needed to remember or propagate configuration information.

#include <mutex>  // NOLINT

#include "googleapis/client/transport/http_transport.h"
#include "googleapis/client/transport/http_transport_global_state.h"

namespace googleapis {

namespace {
using client::HttpTransportLayerConfig;

static HttpTransportLayerConfig* configuration_ = NULL;

static void InitModule() {
  configuration_ = new HttpTransportLayerConfig;
}

std::once_flag g_once_flag;

}  // anonymous namespace

namespace client {

HttpTransportLayerConfig* GetGlobalHttpTransportLayerConfiguration() {
  std::call_once(g_once_flag, &InitModule);
  return configuration_;
}

}  // namespace client

}  // namespace googleapis
