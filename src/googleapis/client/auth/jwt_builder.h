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
#ifndef GOOGLEAPIS_AUTH_JWT_BUILDER_H_
#define GOOGLEAPIS_AUTH_JWT_BUILDER_H_

#include <openssl/evp.h>
#include <string>
using std::string;

#include "googleapis/strings/stringpiece.h"
#include "googleapis/util/status.h"

namespace googleapis {

namespace client {

/*
 * A helper class used to create the JWT we pass to the OAuth2.0 server.
 *
 * This class could be broken out into its own module if it is needed elsewhere
 * or to test more explicitly.
 */
class JwtBuilder {
 public:
  static googleapis::util::Status LoadPrivateKeyFromPkcs12Path(
      const string& path, string* result_key);

  static void AppendAsBase64(const char* data, size_t size, string* to);
  static void AppendAsBase64(const string& from, string* to);

  static EVP_PKEY* LoadPkeyFromData(const StringPiece data);
  static EVP_PKEY* LoadPkeyFromP12Path(const char* pkcs12_key_path);

  static googleapis::util::Status MakeJwtUsingEvp(
      const string& claims, EVP_PKEY* pkey, string* jwt);
};

}  // namespace client

}  // namespace googleapis
#endif  // GOOGLEAPIS_AUTH_JWT_BUILDER_H_
