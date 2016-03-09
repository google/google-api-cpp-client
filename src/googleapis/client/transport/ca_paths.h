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
#ifndef GOOGLEAPIS_TRANSPORT_CA_PATHS_H_
#define GOOGLEAPIS_TRANSPORT_CA_PATHS_H_

#include <string>
namespace googleapis {
using std::string;

namespace client {

/*
 * Returns the default location of the SSL certificate validation data.
 *
 * The default location is assumed to be the same directory as the
 * executable for the running process in a file called 'roots.pem'.
 */
std::string DetermineDefaultCaCertsPath();

}  // namespace client

}  // namespace googleapis
#endif  // GOOGLEAPIS_TRANSPORT_CA_PATHS_H_
