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


#ifndef GOOGLEAPIS_TRANSPORT_HTTP_RESPONSE_H_
#define GOOGLEAPIS_TRANSPORT_HTTP_RESPONSE_H_

#include <memory>
#include <string>
using std::string;
#include "googleapis/client/transport/http_types.h"
#include "googleapis/client/util/status.h"
#include "googleapis/base/integral_types.h"
#include "googleapis/base/macros.h"
#include "googleapis/base/mutex.h"
#include "googleapis/base/thread_annotations.h"
namespace googleapis {

namespace client {
class DataReader;
class DataWriter;

/*
 * Captures the response from HttpRequest invocations.
 * @ingroup TransportLayerCore
 *
 * HttpResponse has thread-safe state except the message body is not
 * thread-safe. It is assumed that you will have only one body reader
 * since DataReader is not thread-safe either and can only be reliably read
 * one time.
 *
 * Responses are typically created and owned by HttpRequest objects rather
 * than directly by consumer code.
 *
 * @see HttpRequest
 * @see HttpRequestState
 * @see body_reader
 * @see set_body_writer
 */
class HttpResponse {
 public:
  /*
   * Standard constructor.
   */
  HttpResponse();

  /*
   * Standard destructor.
   */
  virtual ~HttpResponse();

  /*
   * Clear the state and headers from the response
   */
  virtual void Clear();

  /*
   * Returns the current request state.
   */
  const HttpRequestState& request_state() const {
    return *request_state_.get();
  }

  /*
   * Returns the state code indicating where in the processing lifecycle the
   * request currently is.
   */
  HttpRequestState::StateCode request_state_code() const {
    return request_state_->state_code();
  }

  /*
   * Get modifiable request state.
   *
   * this is not normally used when using requests but may be needed if you
   * are using responses in some other non-standard way.
   */
  HttpRequestState* mutable_request_state() { return request_state_.get(); }

  /*
   * Sets the reader for the message body in the HTTP response.
   * Normally this is called by the HttpRequest::DoExecute() method.
   *
   * @param[in] reader  Takes owenership.
   */
  void set_body_reader(DataReader* reader);

  /*
   * Sets the writer for the message body in the HTTP response.
   *
   * This must be set before you call HttpRequest::Execute(). The response
   * will be constructed with a writer such as NewStringDataWriter(). However,
   * if you are expecting a large response and wish to stream it directly to
   * a file (or some other type of writer) then you this is how you make that
   * happen.
   *
   * @param[in] writer Takes ownership.
   */
  void set_body_writer(DataWriter* writer);

  /*
   * Returns the current body writer
   */
  DataWriter* body_writer() { return body_writer_.get(); }

  /*
   * Returns the reader for the HTTP message body.
   *
   * This method is not thread safe until the request is done.
   *
   * Reading the response from the reader will update the position. The
   * reader is not required to support Reset() so you may only have one
   * chance to look at the response.
   */
  DataReader* body_reader() const { return body_reader_.get(); }

  /*
   * Reads the entire response HTTP message body as a string.
   *
   * If the body_reader was already accessed, including calling this method
   * before, then this method might not work if the reader was not resetable.
   * It will attempt to return the whole body as a string even if the
   * body_reader() already read some portion of it.
   *
   * @param[out] body The entire message body text.
   * @return status indicating whether the entire body could be read or not.
   *
   * This method will block until the body_reader() is done reading.
   */
  googleapis::util::Status GetBodyString(string* body);

  /*
   * Returns the transport status.
   *
   * @see HttpRequestState::transport_status()
   */
  googleapis::util::Status transport_status() const {
    return request_state_->transport_status();
  }

  /*
   * Returns the overall request status
   *
   * @see HttpRequestState::transport_status()
   */
  googleapis::util::Status status() const { return request_state_->status(); }

  /*
   * Sets the HTTP status code for the response.
   * @param[in] code  0 indicates no code is available.
   */
  void set_http_code(int code)  { request_state_->set_http_code(code); }

  /*
   * Returns the HTTP status code returned with the HTTP response.
   *
   * @return 0 if no response has been received yet.
   */
  int http_code() const  { return request_state_->http_code(); }

  /*
   * Returns true if the request is done.
   *
   * @see HttpRequestState::done()
   */
  bool done() const { return request_state_->done(); }

  /*
   * Returns true if the request is ok.
   *
   * @see HttpRequestState::ok()
   */
  bool ok() const   { return request_state_->ok(); }

  /*
   * Returns the HTTP response headers
   *
   * @return individual headers might have multiple values.
   */
  const HttpHeaderMultiMap& headers() const  { return headers_; }

  /*
   * Adds a response header seen in the HTTP response message.
   *
   * @param[in] name The header name is not necessarily unique.
   * @param[in] value The value for the header.
   */
  void AddHeader(const string& name, const string& value) {
    headers_.insert(std::make_pair(name, value));
  }

  /*
   * Removes all the response headers from this instance.
   */
  void ClearHeaders() { headers_.clear(); }

  /*
   * Get the value of the named header.
   *
   * @return NULL if the named header is not present, otherwise returns a
   *         pointer to the header value.
   *
   * A non-NULL result will only be valid until a header is added or removed
   * (or the object is destroyed).
   */
  const string* FindHeaderValue(const string& name) const;

  /*
   * Blocks the callers thread until this response is done() or
   * the specified timeout expires.
   *
   * Note that if the underlying request was DestroyWhenDone then this
   * response instance may no longer exist when this method returns. Also
   * note that if the request was asynchronous, and the method returns true,
   * then the callback (if any) has already finished running as well.
   *
   * @param[in] timeout_ms  The timeout in millis.
   * @return true if the call's timeout expired,
   *         false if the response is done().
   */
  bool WaitUntilDone(int64 timeout_ms = kint64max) {
    return request_state_->WaitUntilDone(timeout_ms);
  }

 private:
  std::unique_ptr<HttpRequestState> request_state_;
  std::unique_ptr<DataReader> body_reader_;
  std::unique_ptr<DataWriter> body_writer_;
  HttpHeaderMultiMap headers_;

  DISALLOW_COPY_AND_ASSIGN(HttpResponse);
};

}  // namespace client

}  // namespace googleapis
#endif  // GOOGLEAPIS_TRANSPORT_HTTP_RESPONSE_H_
