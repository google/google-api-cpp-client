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


#ifndef GOOGLEAPIS_TRANSPORT_HTTP_REQUEST_BATCH_H_
#define GOOGLEAPIS_TRANSPORT_HTTP_REQUEST_BATCH_H_

#include <memory>
#include <string>
using std::string;
#include <vector>

#include "googleapis/client/transport/http_request.h"
#include "googleapis/client/transport/http_types.h"
#include "googleapis/client/util/status.h"
#include "googleapis/base/macros.h"
namespace googleapis {

namespace client {
class HttpTransport;

/*
 * Denotes a batch of HTTP requests to be sent to an HTTP server all together.
 * @ingroup TransportLayerCore
 *
 * Batch requests are encoded together as a multipart/mixed message whose
 * parts are the individual messages being batched. The batch message is
 * POSTed to a webserver URL that is capable of handling such a message,
 * which is not standard. The Google Cloud Platform provides a URL intended
 * for batching requests to Google APIs. This class is intended to be used
 * for that purpose.
 *
 * HttpRequestBatch acts as an HttpRequest factory for the requests to be
 * batched. Ideally you know at the time of construction whether you are
 * going to batch an HttpRequest or not. If you are going to batch it then
 * you need to construct it from the BatchHttpRequest that it will live in.
 * If it isnt to be batched then you need to construct it from the
 * HttpTransport that you will use to send it. The HttpRequestBatch provides
 * a method in which you can add any existing HttpRequest, including one
 * that was created directly from a transport. However this method will
 * destroy the old request and create a new copy in its place, thus invaliding
 * the old pointer (and corrupting any other references to it).
 *
 * The HttpRequestBatch is constructed with a transport instance that it will
 * use for the actualy HTTP messaging of the batch.
 *
 * To customize the actual HTTP message sent, such as setting a default
 * credential, headers, or HttpRequestOptions, get the underlying http_request
 * object through the mutable_http_request() attribute. The HttpRequest can
 * also be used to inspect HTTP details within the response, such as overall
 * status. The individual batched HttpRequests can also be inspected.
 *
 * TODO(user): 20130823
 * Need to refactor HttpScribe to know about batched requests so that it
 * can censor the individual parts in the batch rather than them as an
 * opaque stream. As it stands header censorship wont work because it
 * doesnt know about the headers inside the batched body content.
 *
 * @see HttpRequest
 * @see HttpTransport
 */
class HttpRequestBatch {
 public:
  typedef std::vector<HttpRequest*> BatchedRequestList;

  /*
   * Constructs a new HttpRequest that is a collection of requests to batch.
   *
   * @param[in] transport The caller retains ownership of the transport.
   *                      This transport will be used to create the underlying
   *                      HTTP messaging that contains the batch messages.
   */
  explicit HttpRequestBatch(HttpTransport* transport);

  /*
   * Constructs a new HttpRequest that is a collection of requests to batch.
   *
   * @param[in] transport The caller retains ownership of the transport.
   *                      This transport will be used to create the underlying
   *                      HTTP messaging that contains the batch messages.
   * @param[in] batch_url The specific endpoint to send requests to.
   */
  
  HttpRequestBatch(HttpTransport* transport, const std::string& batch_url);
  /*
   * Standard destructor.
   */
  virtual ~HttpRequestBatch();

  HttpRequest* mutable_http_request()     { return http_request_.get(); }
  const HttpRequest& http_request() const { return *http_request_.get(); }

  /*
   * Returns the list of HttpRequest's added to the batch so far.
   *
   * @see MakeRequest
   * @see AddFromGenericRequestAndRetire
   */
  const BatchedRequestList& requests() const { return requests_; }

  /*
   * Returns the MIME multipart message boundary pattern used when
   * constructing the multipart/mixed request.
   */
  const string& boundary() const { return boundary_; }

  /*
   * Changes the MIME multipart message boundary pattern.
   *
   * @param boundary If the new boundary text is empty then
   *                 the default will be used. If the text conflicts with
   *                 content in one of the request bodies then the web server
   *                 will not be able to properly parse the request.
   */
  void set_boundary(const string& boundary);

  /*
   * Clears all the requests from the batch.
   */
  virtual void Clear();

  /*
   * Removes a single request from the batch and destroys it.
   *
   * @param[in, out] The request to remove will be deleted even if it was
   *                 not in the batch (i.e. even if an error is returned).
   * @return Ok if the request was in the batch and error otherwise.
   */
  googleapis::util::Status RemoveAndDestroyRequest(HttpRequest* request);

  /*
   * Creates a new, empty HttpRequest and adds it to the batch.
   *
   * The resulting request will be implciitly executed when the batch instance
   * is executed. To get at the response or result status, use the returned
   * HttpRequest::response() attribute as any other HttpRequest.
   *
   * @param[in] method The HTTP protocol method (e.g. GET) that will be used.
   *                   This is simlar to the standard HttpRequest constructor.
   * @param[in] callback If not NULL then this callback will be called when
   *            the added method finishes executing, similar to the callback
   *            given to HttpRequest::ExecuteAsync. If provided, the callback
   *            will be called exactly once and called when the method is done
   *            similar to HttpRequest::ExecuteAsync. The caller retains
   *            ownership but can use a standard self-destructing callback to
   *            transfer ownership to the callback instance itself.
   *
   * @return The HttpRequest returned will be executed when the this batch
   *         object is executued. It should not be explicitly Execute'd
   *         directly. The instance returned is empty so must be given a URL.
   *         Additional headers and request body payload can be added as well.
   *
   * @see AddFromGenericRequestAndRetire
   */
  HttpRequest* NewHttpRequest(
      const HttpRequest::HttpMethod method,
      HttpRequestCallback* callback = NULL);

  /*
   * Converts the original HttpRequest into a batched request in this batch.
   *
   * @param[in] original The original request will be destroyed but its parts
   *            will be swapped into the new instance returned.
   *
   * @param[in] callback If not NULL the callback will be invoked when the
   *                     request is done either because it has finished
   *                     executing or has failed in a way in which it will
   *                     never execute. The caller retains ownership.
   *                     If not NULL this callback will be called exactly once
   *                     so the caller can use a regular self-destructing
   *                     callback.
   *
   * @return A new HttpRequest that should be used in place of the
   *         destroyed original. The returned request will be added
   *         similar to MakeRequest.
   */
  HttpRequest* AddFromGenericRequestAndRetire(
      HttpRequest* original, HttpRequestCallback* callback = NULL);

  /*
   * Synchronously send the batch of requests to the designated URL and wait
   * for the response.
   *
   * @return The overall batch status. Individual requests can be checked
   *         independently.
   *
   * @see ExecuteAsync()
   * @see http_request()
   */
  googleapis::util::Status Execute();

  /*
   * Asynchronously send the batch of requests to the designated URL then
   * continue this thread while the server is processing the request.
   *
   * param[in] callback If non-NULL the callback will be run once the
   * request is done() for whatever reason. The caller retains ownership
   * however typically single-use callbacks are used so the callback will
   * self-destruct once run.
   *
   * Note that the callback may or may not have been executed at the time
   * that this call returns. If using a self-destructing instance, you
   * should only look at the state and response in the callback.
   *
   * @see Execute()
   */
  void ExecuteAsync(HttpRequestCallback* callback);

  /*
   * Can differ from the underling HTTP status if the response that came back
   * didnt correlate to the requests within as we expected it to.
   */
  googleapis::util::Status batch_processing_status() const {
    return batch_processing_status_;
  }

  /*
   * Internal helper used for building identifiers to batch requests.
   */
  static string PointerToHex(void *p);

 protected:
  void PrepareFinalHttpRequest();
  void ProcessHttpResponse(
      HttpRequestCallback* original_callback, HttpRequest* request);

  std::unique_ptr<HttpRequest> http_request_;
  string boundary_;
  googleapis::util::Status batch_processing_status_;
  BatchedRequestList requests_;

 private:
  DISALLOW_COPY_AND_ASSIGN(HttpRequestBatch);
};

}  // namespace client

}  // namespace googleapis
#endif  // GOOGLEAPIS_TRANSPORT_HTTP_REQUEST_BATCH_H_
