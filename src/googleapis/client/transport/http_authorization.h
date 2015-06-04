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


#ifndef GOOGLEAPIS_TRANSPORT_HTTP_AUTHORIZATION_H_
#define GOOGLEAPIS_TRANSPORT_HTTP_AUTHORIZATION_H_

#include <string>
using std::string;

#include "googleapis/client/util/status.h"
#include "googleapis/base/callback.h"
#include "googleapis/base/macros.h"
namespace googleapis {

namespace client {

class DataReader;
class HttpRequest;

/*
 * The abstraction used to pass credentials also contains knowledge about how
 * to use the credentials to authorize requests.
 * @ingroup TransportLayerCore
 *
 * In practice this is probably an OAuth2Credential, but this class provides
 * an abstract interface sufficient to keep OAuth 2.0 dependencies out of the
 * HTTP transport layer and core libraries that dont care about the mechanism
 * details.
 */
class AuthorizationCredential {
 public:
  /*
   * Standard destructor.
   */
  virtual ~AuthorizationCredential();

  /*
   * Returns the type of credential for tracing/debug purposes.
   */
  virtual const string type() const = 0;

  /*
   * Refreshes credential.
   *
   * @return ok or reason for failure.
   */
  virtual googleapis::util::Status Refresh() = 0;

  /*
   * Refreshes credential asynchronously.
   *
   * @param[in] callback Called on refresh termination status.
   */
  virtual void RefreshAsync(Callback1<util::Status>* callback) = 0;

  /*
   * Initialize the credential from a stream.
   *
   * @param[in] serialized_credential A serialized credential stream to
   *            load from.
   *
   * @see MakeDataReader
   */
  virtual googleapis::util::Status Load(DataReader* serialized_credential) = 0;

  /*
   * Creates a DataReader stream serializing the credential.
   *
   * @return serialized stream suitable for Load.
   */
  virtual DataReader* MakeDataReader() const = 0;

  /*
   * Uses the credential to authorize a request.
   *
   * @param[in,out] request The request to authorized will be modified as
   *                needed depending on the specific class (e.g. adding an
   *                authoriation header).
   * @return failure if the request cannot be authorized. A success does
   *         not guarantee that the server will accept the authorization
   *         but a failure guarantees that it will not.
   */
  virtual googleapis::util::Status AuthorizeRequest(HttpRequest* request) = 0;
};

}  // namespace client

}  // namespace googleapis
#endif  // GOOGLEAPIS_TRANSPORT_HTTP_AUTHORIZATION_H_
