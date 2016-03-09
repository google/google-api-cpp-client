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
#include <string>
using std::string;

#include "googleapis/client/transport/ca_paths.h"
#include "googleapis/client/util/program_path.h"

namespace googleapis {


namespace client {

std::string DetermineDefaultCaCertsPath() {
  const std::string program_path(GetCurrentProgramFilenamePath());
  auto dirname(client::StripBasename(program_path));
  dirname.append("roots.pem");  // dirname has ending slash.
  return dirname;
}

}  // namespace client

}  // namespace googleapis
