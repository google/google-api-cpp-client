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

/*
 * @defgroup TransportLayerTransport Transport Layer - Concrete Transports
 *
 * The HTTP transport layer does not include any specific HTTP implementations.
 * This module contains concrete transport implementations that can be used
 * in practice. These transports use specialized classes and the injection
 * mechanisms provided by the core transport layer in order to seamlessly
 * integrate concrete implementations.
 *
 * Additional transports can be found in among the Transport Layer Testing
 * components.
 */
#ifndef GOOGLEAPIS_TRANSPORT_CURL_HTTP_TRANSPORT_H_
#define GOOGLEAPIS_TRANSPORT_CURL_HTTP_TRANSPORT_H_

#include <vector>

#include "googleapis/client/transport/http_request.h"
#include "googleapis/client/transport/http_transport.h"
#include "googleapis/base/macros.h"
#include "googleapis/base/mutex.h"
namespace googleapis {

namespace client {

class CurlProcessor;

/*
 * A concrete HttpTransport that is implemented using the open source CURL
 * library.
 * @ingroup TransportLayerTransport
 *
 * A curl transport is capable of having multiple requests running and talking
 * to multiple servers. For future optimization purposes, it is suggested that
 * you use a single CurlHttpTransport instances for all requests sent to the
 * same service, but use different instances to talk to different services.
 * This is not required but might have better performance as the implementation
 * is tuned and optimized.
 *
 * It is recommended to not use this class directly, especially
 * in library code. Use the generic HttpTransport and HttpTransportFactory
 * unless you specifically want to use curl for some reason.
 *
 * The Google APIs for C++ Client Library is designed to accomodate
 * external transport implementations and eliminating a dependency on
 * curl entirely. If you use this class directly then you will be interfering
 * with that property.
 *
 * @see HttpTransport::set_default_transport_factory
 */
class CurlHttpTransport : public HttpTransport {
 public:
  /*
   * Overrides default options while constructing.
   *
   * @param[in] options The options to use when configuring the transport are
   *            copied into the instance.
   */
  explicit CurlHttpTransport(const HttpTransportOptions& options);

  /*
   * Standard destructor.
   */
  virtual ~CurlHttpTransport();

  /*
   * Creates new HttpRequest that will be executed using the transport.
   *
   * @param[in] method The HTTP request method to use when constructing
   *            the request.
   *
   * @return Passes ownerhip of the request back to the caller. The request
   *         needs this transport instance so the caller must guarantee that
   *         this transport is not destroyed before it finishes using the
   *         request.
   */
  virtual HttpRequest* NewHttpRequest(const HttpRequest::HttpMethod& method);

  /*
   * The default id() attribute value identifying curl transport instances.
   */
  static const char kTransportIdentifier[];

 private:
  // For efficiency we maintain a free-list of curl processors for use by
  // requests made with this transport.
  //
  // Acquires a processor based upon transport configuration. The caller has
  // exclusive use of the processor until it calls ReleaseProcessor.  Calls to
  // AcquireProcessor and ReleaseProcessor must be paired.
  CurlProcessor* AcquireProcessor();
  void ReleaseProcessor(CurlProcessor* processor);

  Mutex mutex_;
  std::vector<CurlProcessor*> processors_;

  friend class CurlHttpRequest;
  DISALLOW_COPY_AND_ASSIGN(CurlHttpTransport);
};

/*
 * Factory for creating CurlHttpTransport instances.
 * @ingroup TransportLayerTransport
 *
 * It is recommended not to use this class directly except at the
 * point you are injecting curl as the Http Transport implementation.
 * This should be at application-level configuration.
 * <pre>
 *   HttpTransport::set_default_transport_factory(new CurlHttpTransportFactory)
 * </pre>
 */
class CurlHttpTransportFactory : public HttpTransportFactory {
 public:
  /*
   * Default constructor.
   */
  CurlHttpTransportFactory();

  /*
   * Standard constructor.
   */
  explicit CurlHttpTransportFactory(const HttpTransportLayerConfig* config);

  /*
   * Standard destructor.
   */
  virtual ~CurlHttpTransportFactory();

  /*
   * Creates a new instance of a CurlHttpTransport with overriden options.
   *
   * @param[in] options The options to use when configuring the transport are
   *            copied into the instance.
   *
   * @return ownership of the new transport is passed back to the caller.
   */
  static HttpTransport* NewCurlHttpTransport(
      const HttpTransportOptions& options);

 protected:
  /*
   * Creates a new transport with overrided options.
   *
   * @param[in] options The options will override the options in the factory
   *            that are used to configure new transports created.
   *            The options are copied into the new instance.
   * @return ownership of the new transport is passed back to the caller.
   */
  virtual HttpTransport* DoAlloc(const HttpTransportOptions& options);

 private:
  DISALLOW_COPY_AND_ASSIGN(CurlHttpTransportFactory);
};

}  // namespace client

}  // namespace googleapis
#endif  // GOOGLEAPIS_TRANSPORT_CURL_HTTP_TRANSPORT_H_
