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

//
// Common type declarations for http_transport layer.
// These are really here to get around cross-include dependencies where
// the header guard would prevent getting at definitions.

#ifndef GOOGLEAPIS_TRANSPORT_HTTP_TYPES_H_
#define GOOGLEAPIS_TRANSPORT_HTTP_TYPES_H_

#include <map>
#include <string>
using std::string;

#include "googleapis/strings/case.h"
#include "googleapis/client/util/status.h"
#include "googleapis/base/callback.h"
#include "googleapis/base/macros.h"
#include "googleapis/base/mutex.h"
#include "googleapis/base/thread_annotations.h"
namespace googleapis {

namespace client {

class HttpRequest;
class HttpResponse;

/*
 * Comparator for using headers in standard template library collections.
 *
 * Puts certain headers before others and case-insensitive compares the rest.
 */
struct RequestHeaderLess {
  /*
   * Constructor is overloaded to perform some global initialization.
   */
  RequestHeaderLess();

  /*
   * Compares headers for sort-order.
   *
   * @param[in] a Name of first header.
   * @param[in] b Name of second header.
   * @return true if a should be sorted before b, false otherwise.
   */
  bool operator()(const string& a, const string& b) const;
};

/*
 * Collection of HTTP headers (without repeated headers).
 * @ingroup TransportLayerCore
 *
 * The map is keyed by case-insensitive header naem.
 * The values are the header values.
 */
typedef std::map<string, string, RequestHeaderLess> HttpHeaderMap;

/*
 * Collection of HTTP headers (allows repeated header values).
 * @ingroup TransportLayerCore
 *
 * The map is keyed by case-insensitive header naem.
 * The values are the header values.
 */
typedef std::multimap<string, string, StringCaseLess> HttpHeaderMultiMap;

/*
 * Denotes a callback function that takes an HttpRequest* parameter.
 * @ingroup TransportLayerCore
 *
 * Request callbacks are used for notification on asynchronous requests.
 * Typically the owner maintains ownership of the request.
 * If this is called by the ExecuteAsync flow then you can call
 * DestroyWhenDone before executing the request and the request will
 * be destroyed after the callback is called.
 */
typedef Callback1<HttpRequest*> HttpRequestCallback;

/*
 * Specifies per-request options that control its behavior.
 * @ingroup TransportLayerCore
 */
class HttpRequestOptions {
 public:
  /*
   * Default constructor.
   */
  HttpRequestOptions();

  /*
   * Clears the timeout value so requests can be indefnite.
   */
  void clear_timeout()              { timeout_ms_ = 0; }

  /*
   * Determine if request can timeout.
   * @return false if request will never timeout.
   */
  bool has_timeout() const          { return timeout_ms_ != 0; }

  /*
   * Specify timeout, in milliseconds.
   *
   * The precision of the timeout is left to the actual transport used.
   *
   * @param[in] ms  0 indicates request will never timeout.
   */
  void set_timeout_ms(int64 ms)     { timeout_ms_ = ms; }

  /*
   * Get specified timeout, in milliseconds.
   * @return 0 if there is no timeout.
   */
  int64 timeout_ms() const          { return timeout_ms_; }

  /*
   * Set maximum permissable retries.
   *
   * This is only applicable for situations in which the application
   * chooses to attempt to retry sending a request. These do not include
   * redirects.
   *
   * @param[in] n 0 will not attempt any retries.
   *
   * @see HttpRequest
   * @see max_redirects
   */
  void set_max_retries(int n)       { max_retries_ = n; }

  /*
   * Get specified max permissable retries.
   */
  int max_retries() const            { return max_retries_; }

  /*
   * Get specified maximum permissable redirects.
   */
  int max_redirects() const         { return max_redirects_; }

  /*
   * Set maximum permissable redirects.
   *
   * @param[in] n 0 will not follow any redirets.
   * Maximum number of redirects to follow when sending requests.
   */
  void set_max_redirects(int n)     { max_redirects_ = n; }

  /*
   * Determine if request will self-destruct when done.
   *
   * @return false Will not self destruct.
   */
  bool destroy_when_done() const            { return destroy_when_done_; }

  /*
   * Specify whether to self-destruct when done.
   * @param[in] destroy false (default) requires explicit destruction.
   */
  void set_destroy_when_done(bool destroy)  { destroy_when_done_ = destroy; }

  /*
   * Specify priority of the request being made.
   * Default value is 0. As value increases priority decreases.
   */
  void set_priority(unsigned int priority)  { priority_ = priority; }

  /*
   * Get the priority value assigned to a request.
   */
  unsigned int priority() const   { return priority_; }

 private:
  int64 timeout_ms_;        //!< Default is subject to change.
  int max_retries_;         //!< Default is subject to change.
  int max_redirects_;       //!< Default is subject to change.
  bool destroy_when_done_;  //< Default is false.
  unsigned int priority_;            //< Default is 0.
};

/*
 * Denotes the current state of an HttpRequest's lifecycle.
 * @ingroup TransportLayerCore
 *
 * The state includes the StateCode in its state machine progress as
 * well as status and response data. Normally the state is created as
 * an attribute to an HttpRequest or HttpResponse -- you do not typically
 * instantiate these directly yourself.
 *
 * The state is shared between an HttpRequest and its HttpResponse such
 * that it is accessible by either. It will remain valid until both the
 * request and response have been destroyed.
 *
 * This class is thread-safe.
 */
class HttpRequestState {
 public:
  /*
   * Denotes a state in the HttpRequest lifecycle.
   *
   * <table>
   * <tr><th>State Code <th>Done<th>Ok
   *     <th>Description
   *     <th>Possible Transitions
   * <tr><td>UNSENT     <td>-   <td>Y
   *     <td>The request has not yet been sent.
   *     <TD>QUEUED, PENDING, COULD_NOT_SEND, CANCELLED
   * <tr><td>QUEUED     <td>-   <td>Y
   *     <td>The request has been queued to send (async)
   *         but has not yet been sent.
   *     <td>PENDING, COULD_NOT_SEND, CANCELLED,
   * <tr><td>PENDING    <td>-   <td>Y
   *     <td>The request has been sent (either in part or whole) but a
   *         response has not yet been received.
   *     <td>COMPLETED, COULD_NOT_SEND, TIMED_OUT, ABORTED
   * <tr><td>COMPLETED  <td>Y   <td>Y
   *     <td>A response was received from the server. The response might
   *         have been an HTTP error, but was a valid HTTP response.
   *     <td>-
   * <tr><td>COULD_NOT_SEND  <td>Y   <td>-
   *     <td>An error prevented the request from being sent or response
   *         from being received.
   *     <td>-
   * <tr><td>TIMED_OUT  <td>Y   <td>-
   *     <td>Request was sent but timed out before response arrived.
   *     <td>-
   * <tr><td>CANCELLED  <td>Y   <td>-
   *     <td>Indicates that the request was cancelled before it was sent.
   *     <td>-
   * <tr><td>ABORTED    <td>Y   <td>-
   *     <td>Used to signal callback it will never be called.
   *     <td>-
   * <tr><td>_NUM_STATES_ <td>-   <td>-
   *     <td>An internal marker used to count the nuber of states.
   *     <td>-
   * </table>
   */
  enum StateCode {
    UNSENT,
    QUEUED,
    PENDING,
    COMPLETED,
    COULD_NOT_SEND,
    TIMED_OUT,
    CANCELLED,
    ABORTED,
    _NUM_STATES_
  };

  /*
   * Standard constructor.
   */
  HttpRequestState();

  /*
   * Destroys this instance.
   */
  virtual ~HttpRequestState();

  /*
   * Destroys this instance when it is finished waiting.
   *
   * Will destory immediately if it isnt waiting.
   */
  void DestroyWhenDone();

  /*
   * Gets lifecycle state code.
   * @note Only HttpTransports should set the state attribute.
   */
  StateCode state_code() const LOCKS_EXCLUDED(mutex_);

  /*
   * Transition to a new lifecycle state.
   *
   * @param[in] code Specifies the new state_code().
   *
   * If this transitions into a done() state for the first time then it will
   * call the callback, if one has been bound, then signal any threads waiting
   * on this state. This method does not cause the instance to be destroyed
   * if it was configured to self-destruct; that is up to the caller.
   */
  void TransitionAndNotifyIfDone(StateCode code)  LOCKS_EXCLUDED(mutex_);

  /*
   * Transitions the state as applicable based on the transport_status() or
   * http_code().
   *
   * This function deteremins what the state_code should be based on the
   * transport_status() and http_code() values.
   *
   * An http_code indicating that the HTTP request is no longer outstanding
   * will transition into StateCode::COMPLETED (even if the http code is an
   * error). Only (specialized) HttpRequest classes should set the http_code
   * attribute.
   *
   * @return The overall request status after the transition. A failure status
   * indicates that the HttpRequest failed, not a failure to transition.
   */
  googleapis::util::Status AutoTransitionAndNotifyIfDone()  LOCKS_EXCLUDED(mutex_);

  /*
   * Sets the transport-level status for the request.
   *
   * The transport-level status can be used to determine whether the
   * communication between this client and the service was ok or not
   * independent of whether the service was able to actually perform the
   * request. HTTP errors are application-level failures, but transport-level
   * success because the complete HTTP messaging was able to take place.
   *
   * @param[in] status The transport-level status.
   */
  void set_transport_status(const googleapis::util::Status& status)
      LOCKS_EXCLUDED(mutex_);

  /*
   * Returns the transport-level status.
   *
   * @return failure status only if a transport error was encountered.
   * The status will be ok in an UNSENT state.
   */
  googleapis::util::Status transport_status() const LOCKS_EXCLUDED(mutex_);

  /*
   * Returns the overall status for this request.
   *
   * If the transport_status is a failure, then this status will reflect that.
   * If the transport_status is ok then this status will be determined by the
   * http_code. Often for HTTP level errors, HttpResponse::body_reader()
   * contains additional error information that wont be contained in the
   * status.
   */
  googleapis::util::Status status() const LOCKS_EXCLUDED(mutex_);

  /*
   * Returns the HTTP status code returned in the response.
   *
   * @pre request_state() == COMPLETED
   * @returns 0 if the request_state has not completed,
   * including transport errors.
   */
  int http_code() const              { return http_code_; }

  /*
   * Sets the HTTP status code returned by the HTTP server
   */
  void set_http_code(int http_code)  { http_code_ = http_code; }

  /*
   * Returns whether or not the request has completely finished executing.
   *
   * @return true if the state denotes done, false otherwise
   * @see StateCode
   */
  bool done() const LOCKS_EXCLUDED(mutex_);

  /*
   * Returns whether or not an error has been encountered.
   *
   * @return true if the state code denotes ok, false otherwise
   * @see StateCode
   */
  bool ok() const LOCKS_EXCLUDED(mutex_);

  /*
   * Blocks the callers thread until the state is done (the request completes)
   * or the timeout has elapsed.
   *
   * @param[in] timeout_ms (optional, forever if not specified)
   * @return true if the request is done, false if the timeout expired. If
   *         the request times out then true is returned (because the
   *         request is done).
   *
   * If the request had the destroy_when_done option set then it will no
   * longer be available, nor will this state instance. Even if false is
   * returned, indicating that the request is not done, you should not assume
   * that the request or state instance is still valid because it could have
   * just completed since this method returned.
   */
  bool WaitUntilDone(int64 timeout_ms = kint64max) LOCKS_EXCLUDED(mutex_);

  /*
   * Gets the respose objecta associated with the request.
   */
  HttpResponse* response() const LOCKS_EXCLUDED(mutex_);

 private:
  friend class HttpRequest;

  void Reset();

  bool has_notify_callback() LOCKS_EXCLUDED(mutex_) {
    MutexLock l(&mutex_);
    return callback_ != NULL;
  }

  // This is only here for use by HttpRequest::SwapToRequestThenDestroy
  HttpRequestCallback* callback() const LOCKS_EXCLUDED(mutex_) {
    MutexLock l(&mutex_);
    return callback_;
  }

  /*
   * Replaces callback to be called when request finishes executing.
   *
   * This method is only exposed for internal usage when composing objects
   * using HttpRequests. Application code should use the Async APIs on the
   * higher level objects, such as HttpRequest::ExecuteAsync.
   *
   * @param[in] request The request owning the state instance
   * @param[in] callabck The callback to invoke when done will eventually be
   *                     called exactly once. The caller retains ownership but
   *                     can use a single-use self-destructing callback.
   */
  void set_notify_callback(
      HttpRequest* request, HttpRequestCallback* callback)
      LOCKS_EXCLUDED(mutex_);

 private:
  mutable Mutex mutex_;
  mutable CondVar condvar_;  //!< Used for signaling to WaitUntilDone
  googleapis::util::Status transport_status_  GUARDED_BY(mutex_);
  StateCode state_code_ GUARDED_BY(mutex_);
  int http_code_;
  int waiting_ GUARDED_BY(mutex_);
  bool destroy_when_done_ GUARDED_BY(mutex_);

  // Paired with callback_.
  // Null when callback_ is NULL.
  HttpRequest* request_ GUARDED_BY(mutex_);

  // Can be NULL. Single-use is permissible. Not owned. Only used
  // for async invocation.
  HttpRequestCallback* callback_ GUARDED_BY(mutex_);

  bool UnsafeWaitUntilDone(int64 timeout_ms) EXCLUSIVE_LOCKS_REQUIRED(mutex_);

  DISALLOW_COPY_AND_ASSIGN(HttpRequestState);
};

/*
 * This is a helper class for interpreting standard HTTP status codes.
 * @ingroup TransportLayerCore
 *
 * It is not meant to be instantiated.
 */
class HttpStatusCode {
 public:
  /*
   * Symbolic names for some common HTTP Stauts Codes of interest.
   *
   * The list here is not a complete enumeration of http_code values. It
   * only enumerates the standard codes that are of particular itnerest within
   * this library or might be commonly checked by consumers.
   *
   * See <a href='http://www.w3.org/Protocols/rfc2616/rfc2616-sec10.html'>
   * Section 10 of RFC 2616</a> for a complete list of standard status codes.
   */
  enum HttpStatus {
    OK = 200,
    NO_CONTENT = 204,
    MULTIPLE_CHOICES = 300,
    MOVED_PERMANENTLY = 301,
    FOUND = 302,
    SEE_OTHER = 303,
    NOT_MODIFIED = 304,
    TEMPORARY_REDIRECT = 307,
    BAD_REQUEST = 400,
    UNAUTHORIZED = 401,
    FORBIDDEN = 403,
    NOT_FOUND = 404,
    NOT_ALLOWED = 405,
    NOT_ACCEPTABLE = 406,
    REQUEST_TIMEOUT = 408,
    CONFLICT = 409,
    SERVER_ERROR = 500,
    SERVICE_UNAVAILABLE = 503,
  };

  /*
   * Returns true if the given HTTP status code indicates a server error
   * response.
   *
   * @param[in] http_code Denotes an HTTP status code.
   * @return true if the code is a 5xx series response code (500..599)
   */
  static bool IsServerError(int http_code) {
    return http_code >= 500 && http_code < 600;
  }

  /*
   * Returns true if the given HTTP status code indicates a client error
   * response.
   *
   * @param[in] http_code Denotes an HTTP status code.
   * @return true if the code is a 4xx series response code (400..499)
   */
  static bool IsClientError(int http_code) {
    return http_code >= 400 && http_code < 500;
  }

  /*
   * Returns true if the given HTTP status code indicates an HTTP Redirect.
   *
   * @param[in] http_code Denotes an HTTP status code
   * @return true if the code indicates a standard type of redirect
   *              (300..303, 305..307).
   */
  static bool IsRedirect(int http_code) {
    // HTTP 1.1 only defines 300-307
    // http://www.w3.org/Protocols/rfc2616/rfc2616-sec10.html
    return http_code >= 300 && http_code <= 307 && http_code != 304;
  }

  /*
   * Returns true if the given HTTP status code indicates a successful request.
   *
   * @param[in] http_code Denotes an HTTP status code.
   * @return true if the code is a 2xx series response code (200..299)
   */
  static bool IsOk(int http_code) {
    return http_code >= 200 && http_code < 300;
  }

  /*
   * Returns true if the given HTTP status code indicates an informational
   * response.
   *
   * @param[in] http_code Denotes an HTTP status code.
   * @return true if the code is a 1xx series response code (100..199)
   */
  static bool IsInformational(int http_code) {
    return http_code >= 100 && http_code < 200;
  }

 private:
  HttpStatusCode();    //!< Not instantiatable.
  ~HttpStatusCode();   //!< Not instantiatable.
};

/*
 * Denotes an end of line within an HTTP message.
 *
 * This is a \r\n sequence.
 */
extern const string kCRLF;

/*
 * Denotes an end of line followed by a blank line within an HTTP message.
 */
extern const string kCRLFCRLF;

}  // namespace client

}  // namespace googleapis
#endif  // GOOGLEAPIS_TRANSPORT_HTTP_TYPES_H_
