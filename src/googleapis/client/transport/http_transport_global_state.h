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


#ifndef GOOGLEAPIS_TRANSPORT_HTTP_TRANSPORT_GLOBAL_STATE_H_
#define GOOGLEAPIS_TRANSPORT_HTTP_TRANSPORT_GLOBAL_STATE_H_
namespace googleapis {

namespace client {

class HttpTransportLayerConfig;

/*
 * Returns global instance for the global configuration.
 *
 * The global configuration is not used internally so changing this has
 * no effect on the core runtime library. It is available to be used by
 * applications and libraries that wish to link with this module and use
 * it to share information. A more general solution that does not require
 * global state is to create your own <code>HttpTransportLayerConfig</code>.
 *
 * @return The library retains ownership of this instance however the
 *         caller can modify its attributes to configuration the runtime.
 */
HttpTransportLayerConfig* GetGlobalHttpTransportLayerConfiguration();

}  // namespace client

}  // namespace googleapis
#endif  // GOOGLEAPIS_TRANSPORT_HTTP_TRANSPORT_GLOBAL_STATE_H_
