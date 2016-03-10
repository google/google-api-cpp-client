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


#include <time.h>

#include <cstdint>
#include <functional>
using std::binary_function;
using std::less;
#include <map>
#include <string>
using std::string;
#include <vector>

#include <glog/logging.h>
#include "googleapis/base/mutex.h"
#include "googleapis/client/data/data_reader.h"
#include "googleapis/client/data/data_writer.h"
#include "googleapis/util/executor.h"
#include "googleapis/client/transport/http_authorization.h"
#include "googleapis/client/transport/http_request.h"
#include "googleapis/client/transport/http_response.h"
#include "googleapis/client/transport/http_scribe.h"
#include "googleapis/client/transport/http_transport.h"
#include "googleapis/client/transport/http_types.h"
#include "googleapis/strings/case.h"
#include "googleapis/client/util/status.h"
#include "googleapis/client/util/uri_utils.h"
#include "googleapis/strings/strcat.h"

namespace googleapis {

namespace {

using client::HttpRequest;
using client::HttpRequestState;
using client::HttpScribe;

static Mutex initializer_mutex_(base::LINKER_INITIALIZED);

// We need to supply a comparator for the HttpHeaderMap.
// Given we need one, rather than simply sorting [case-insensitive]
// alphabetically, we'll order certain header fields before others
// and order the remaining [case-insensitive] alphabetically.
// Section 4.2 of http://www.w3.org/Protocols/rfc2616/rfc2616-sec4.html#sec4.2
// says that although order doesnt matter, "it is 'good practice' to send
// general-header fields first, followed by request-header or
// response- header fields, and ending with the entity-header fields.'
//
// This map just considers a subset of these fields which we already know
// about and sorts them first. The underlying transport may still use some
// other ordering when actually sending, but this will determine the
// "default order" that we'll present headers in when iterating over the map.
typedef std::map<string, int, StringCaseLess> HeaderSortOrderMap;

// This ordering is initialized in InitGlobalVariables when the comparator
// is first used.
std::unique_ptr<HeaderSortOrderMap> header_sort_order_;

static const bool state_is_done_[] = {
  false,  // UNSENT
  false,  // QUEUED
  false,  // PENDING
  true,   // COMPLETED
  true,   // COULD_NOT_SEND
  true,   // TIMED_OUT
  true,   // CANCELLED
  true,   // ABORTED
};

COMPILE_ASSERT(
    sizeof(state_is_done_) == HttpRequestState::_NUM_STATES_ * sizeof(bool),
    state_table_out_of_sync);

static void InitGlobalVariables() {
  MutexLock l(&initializer_mutex_);
  if (header_sort_order_.get() != NULL) {
    return;
  }

  header_sort_order_.reset(new HeaderSortOrderMap);
  int order = 1;

  header_sort_order_->insert(
      std::make_pair(HttpRequest::HttpHeader_HOST, order++));
  header_sort_order_->insert(
      std::make_pair(HttpRequest::HttpHeader_AUTHORIZATION, order++));
  header_sort_order_->insert(
      std::make_pair(HttpRequest::HttpHeader_CONTENT_LENGTH, order++));
  header_sort_order_->insert(
      std::make_pair(HttpRequest::HttpHeader_TRANSFER_ENCODING, order++));
  header_sort_order_->insert(
      std::make_pair(HttpRequest::HttpHeader_CONTENT_TYPE, order++));
  header_sort_order_->insert(
      std::make_pair(HttpRequest::HttpHeader_LOCATION, order++));
  header_sort_order_->insert(
      std::make_pair(HttpRequest::HttpHeader_USER_AGENT, order++));
}

inline bool MethodImpliesContent(const HttpRequest::HttpMethod& method) {
  if (method == HttpRequest::GET) return false;
  if (method == HttpRequest::DELETE) return false;
  if (method == HttpRequest::HEAD) return false;
  return true;
}

inline bool IsStateDone(HttpRequestState::StateCode code) {
  CHECK_LE(0, code);
  CHECK_GT(HttpRequestState::_NUM_STATES_, code);
  return state_is_done_[code];
}

}  // anonymous namespace

namespace client {

RequestHeaderLess::RequestHeaderLess() {
  InitGlobalVariables();
}

bool RequestHeaderLess::operator()(const string& a, const string& b) const {
  HeaderSortOrderMap::const_iterator found_a = header_sort_order_->find(a);
  HeaderSortOrderMap::const_iterator found_b = header_sort_order_->find(b);
  if (found_a != header_sort_order_->end()) {
    if (found_b != header_sort_order_->end()) {
      return found_a->second < found_b->second;
    }
    return true;
  } else if (found_b != header_sort_order_->end()) {
    return false;
  } else {
    return StringCaseCompare(a, b) < 0;
  }
}


static googleapis::util::Status DetermineStatus(
    googleapis::util::Status transport_status,
    int http_code,
    HttpRequestState::StateCode state_code) {
  switch (state_code) {
    case HttpRequestState::UNSENT:
    case HttpRequestState::QUEUED:
      return StatusOk();
    case HttpRequestState::PENDING:
      // If the request is pending return what we know about it so far.
      // This is to support error handling where client code may be looking
      // at response status while it is still "officially" pending because
      // it is still inside the request flow.
      if (http_code >= 200) {
        return StatusFromHttp(http_code);
      } else {
        // Includes HTTP 100 informational codes.
        return transport_status;
      }
    case HttpRequestState::COMPLETED:
      return StatusFromHttp(http_code);
    case HttpRequestState::COULD_NOT_SEND:
      return transport_status;
    case HttpRequestState::TIMED_OUT:
      return StatusDeadlineExceeded("Request timed out");
    case HttpRequestState::ABORTED:
      return StatusAborted("Aborted Request");
    case HttpRequestState::CANCELLED:
      return StatusCanceled("Cancelled Request");

    case HttpRequestState::_NUM_STATES_:
    default:
      return StatusInternalError("INTERNAL ERROR");  // not reached
  }
  // not reached
}

// TODO(user): Examine performance impact of changing these to char[]. If it
// is negligible, then change, otherwise add NOLINT.
const string HttpRequest::HttpHeader_AUTHORIZATION("Authorization");
const string HttpRequest::HttpHeader_CONTENT_LENGTH("Content-Length");
const string HttpRequest::HttpHeader_CONTENT_TYPE("Content-Type");
const string HttpRequest::HttpHeader_HOST("Host");
const string HttpRequest::HttpHeader_LOCATION("Location");
const string HttpRequest::HttpHeader_TRANSFER_ENCODING("Transfer-Encoding");
const string HttpRequest::HttpHeader_USER_AGENT("User-Agent");

const string HttpRequest::ContentType_HTML("text/html");
const string HttpRequest::ContentType_JSON("application/json");
const string HttpRequest::ContentType_TEXT("text/plain");
const string HttpRequest::ContentType_FORM_URL_ENCODED(
    "application/x-www-form-urlencoded");
const string HttpRequest::ContentType_MULTIPART_MIXED("multipart/mixed");
const string HttpRequest::ContentType_MULTIPART_RELATED("multipart/related");

const HttpRequest::HttpMethod HttpRequest::DELETE("DELETE");
const HttpRequest::HttpMethod HttpRequest::GET("GET");
const HttpRequest::HttpMethod HttpRequest::HEAD("HEAD");
const HttpRequest::HttpMethod HttpRequest::PATCH("PATCH");
const HttpRequest::HttpMethod HttpRequest::POST("POST");
const HttpRequest::HttpMethod HttpRequest::PUT("PUT");

HttpRequestOptions::HttpRequestOptions()
    : timeout_ms_(10 * 1000),
      max_retries_(1), max_redirects_(5), destroy_when_done_(false),
      priority_(0) {
}

HttpRequestState::HttpRequestState()
    : state_code_(UNSENT), http_code_(0), waiting_(0), destroy_when_done_(0),
      request_(NULL), callback_(NULL) {
}

HttpRequestState::~HttpRequestState() {
}

void HttpRequestState::DestroyWhenDone() {
  bool destroy = true;
  {
    MutexLock l(&mutex_);
    if (waiting_) {
      destroy_when_done_ = true;
      destroy = false;
    }
  }
  if (destroy) {
    delete this;
  }
}

void HttpRequestState::Reset() {
  MutexLock l(&mutex_);
  CHECK(!request_);
  CHECK(!callback_);
  state_code_ = UNSENT;
  http_code_ = 0;
  transport_status_ = StatusOk();
}

void HttpRequestState::set_notify_callback(
    HttpRequest* request, HttpRequestCallback* callback) {
  MutexLock l(&mutex_);
  request_ = request;
  callback_ = callback;
}

HttpRequestState::StateCode HttpRequestState::state_code() const {
  MutexLock l(&mutex_);
  return state_code_;
}

void CallRequestCallback(HttpRequestCallback* callback, HttpRequest* request) {
  callback->Run(request);
}

void HttpRequestState::TransitionAndNotifyIfDone(
    HttpRequestState::StateCode code) {
  VLOG(9) << "set_state_code=" << code << " on " << this;

  // now_done will indicate that we have just transitioned into the done state.
  // It will be true when the new code is done but the previous code wasnt.
  bool now_done = IsStateDone(code);
  HttpRequestCallback* callback = NULL;
  thread::Executor* callback_executor = NULL;
  HttpRequest* request;
  {
    MutexLock l(&mutex_);
    // TODO(user): 20130812
    // This is messy calling the callback from here. It is here as part
    // of a refactoring. To finish the refactoring move the entire state
    // transition logic into the request and have this state
    // just be a simple data object shared between request and response.
    request = request_;
    if (now_done) {
      now_done = !IsStateDone(state_code_);
      if (now_done && request_) {
        callback_executor =
            request->transport()->options().callback_executor();
        callback = callback_;
        callback_ = NULL;
        request_ = NULL;
      }
    }
    state_code_ = code;
  }

  // There is a race condition here where we havent yet called the callback.
  // The danger is the response could be destroyed here.
  // But since we only call it on the transition, then we should be ok.
  // As an extra guard we state a policy that responses should not be destroyed
  // until they are done. Because of the race condition we cannot enforce it.
  if (now_done) {
    VLOG(9) << "Signal " << this;
    condvar_.SignalAll();

    // The callback should be the last thing executed because the client is
    // likely to run DestroyWhenDone() in the callback which will destroy this
    // object.
    if (callback) {
      if (callback_executor) {
        Closure* closure = NewCallback(&CallRequestCallback, callback, request);
        // TODO(user): I am not sure if we should switch to Try, or if we
        // should send back some sort of error if the TryAdd fails. It is not
        // clear that we can ever work in situations where the callback_executor
        // could block on Add. I am leaning towards switching to Add, but for
        // now we will log and see.
        if (!callback_executor->TryAdd(closure)) {
          delete closure;
          VLOG(1) << "Signal: callback_executor filled up" << this;
        }
      } else {
        callback->Run(request);
      }
    }
  }
}

util::Status HttpRequestState::transport_status() const {
  MutexLock l(&mutex_);
  return transport_status_;
}

void HttpRequestState::set_transport_status(const googleapis::util::Status& status) {
  VLOG(9) << "set_tranport_status=" << status.error_code() << " on " << this;
  MutexLock l(&mutex_);
  transport_status_ = status;
}

util::Status HttpRequestState::AutoTransitionAndNotifyIfDone() {
  googleapis::util::Status status;
  HttpRequestState::StateCode code;
  {
    MutexLock l(&mutex_);
    if (!transport_status_.ok()) {
      switch (transport_status_.error_code()) {
        case util::error::DEADLINE_EXCEEDED:
          code = TIMED_OUT;
          break;
        case util::error::ABORTED:
          code = ABORTED;
          break;
        case util::error::CANCELLED:
          code = CANCELLED;
          break;
        default:
          code = COULD_NOT_SEND;
          break;
      }
    } else if (http_code_ == 0) {
      code = UNSENT;
    } else if (HttpStatusCode::IsInformational(http_code_)) {
      code = PENDING;  // provisional response
    } else if (http_code_ >= 300 && http_code_ < 400) {
      // TODO(user): 20130227
      // Need to address redirection.
      code = COMPLETED;
    } else {
      code = COMPLETED;  // including errors
    }

    if (code == UNSENT) {
      return StatusOk();
    }

    // Grab status now because we may destroy after transition.
    status = DetermineStatus(transport_status_, http_code_, code);
  }  // end locking transport_status_

  // Transition state as a result of having an HTTP statu.
  TransitionAndNotifyIfDone(code);
  return status;
}

bool HttpRequestState::done() const {
  MutexLock l(&mutex_);
  return IsStateDone(state_code_);
}

bool HttpRequestState::ok() const {
  MutexLock l(&mutex_);
  switch (state_code_) {
    case UNSENT:
    case QUEUED:
      return true;
    case PENDING:
      // Request is ok while pending if we are not aware of errors.
      // This is so we can check request.ok() while in error handlers
      // before the request completes.
      return transport_status_.ok()
          && (http_code_ < 300 || http_code_ == HttpStatusCode::NOT_MODIFIED);

    case COULD_NOT_SEND:
    case TIMED_OUT:
    case CANCELLED:
    case ABORTED:
    case _NUM_STATES_:  // just for completeness
      return false;
    case COMPLETED:
      // 100 information results are not considered valid completion states.
      return HttpStatusCode::IsOk(http_code_)
          || http_code_ == HttpStatusCode::NOT_MODIFIED;

    default:
      // not reached
      return false;
  }
}

util::Status HttpRequestState::status() const {
  MutexLock l(&mutex_);
  return DetermineStatus(transport_status_, http_code_, state_code_);
}

bool HttpRequestState::WaitUntilDone(int64 timeout_ms) {
  bool result;
  bool destroy = false;
  {
    MutexLock l(&mutex_);
    ++waiting_;
    result = UnsafeWaitUntilDone(timeout_ms);
    --waiting_;
    destroy = destroy_when_done_ && !waiting_;
  }
  if (destroy) {
    delete this;
  }
  return result;
}

bool HttpRequestState::UnsafeWaitUntilDone(int64 timeout_ms) {
  if (IsStateDone(state_code_)) {
    return true;
  }

  // Bound it to 32 bits worth.
  if (timeout_ms > kint32max) {
    timeout_ms = kint32max;
  }
  int32_t target_timeout_ms = static_cast<int32_t>(timeout_ms);

  // TODO(user): 20130325
  // Revisit this when there's more mature time support. I do not want to add
  // it yet just for this.
  const int32_t start_time = time(0);
  do {
    int32_t now = time(0);
    int32_t remaining_ms = target_timeout_ms - (now - start_time) * 1000;
    VLOG(9) << "WaitWithTimeout " << (remaining_ms) << "ms on " << this
            << "    code=" << state_code_;
    if (IsStateDone(state_code_)) {
      return true;
    }
    if (remaining_ms < 0) {
      break;
    }
    if (condvar_.WaitWithTimeout(&mutex_, remaining_ms)) {
      if (IsStateDone(state_code_)) {
        return true;
      } else {
        LOG(WARNING) << "Wait was signaled with code=" << state_code_;
      }
    }
  } while (true);

  return false;
}

/*
 * Helper class for encapsulating and managing execution workflow state
 * to support asynchronous requests. This is used for both synchronous
 * and asynchronous requests.
 *
 * The workflow is:
 *    Prepare
 *    [if synchronous, in the same call flow]
 *       repeat AttemptToExecute
 *       until done or give up
 *       Cleanup
 *    [if asynchronous, each step is queued to an executor]
 *       repeat QueueAsync (which later calls AttemptToExecute)
 *       until done or give up
 *       Cleanup
 */
class HttpRequest::HttpRequestProcessor {
 public:
  /*
   * @param[in] request Caller retains ownership unless the request is
   *                    configured to self destruct when done.
   */
  explicit HttpRequestProcessor(HttpRequest* request)
      : request_(request),
        state_(request->response()->mutable_request_state()),
        scribe_(request->transport()->scribe()),
        num_redirects_(0), num_retries_(0), retry_(true) {
  }

  /*
   * Provides return result for HttpRequest::Execute().
   * @return The final status of the execution attempts.
   */
  googleapis::util::Status final_status() const { return final_status_; }


  /*
   * Run the synchronous workflow.
   */
  void ExecuteSync() {
    Prepare();
    while (retry_) {
      AttemptToExecute(false);
    }
    Cleanup();
  }

  /*
   * Run the asynchronous workflow then destroy the processor.
   */
  void ExecuteAsyncAndDestroy() {
    Prepare();
    QueueAsync();  // Will eventually destroy
  }

  /*
   * Queues an asynchronous attempt to execute the request.
   *
   * This will queue into the Executor for the original request.
   */
  void QueueAsync() {
    thread::Executor* executor = request_->transport()->options().executor();
    googleapis::util::Status status;
    if (!executor) {
      status = StatusInternalError("No default executor configured");
    } else {
      Closure* closure = NewCallback(
          this, &HttpRequestProcessor::AttemptToExecute, true);
      state_->TransitionAndNotifyIfDone(HttpRequestState::QUEUED);
      if (!executor->TryAdd(closure)) {
        delete closure;
        status = StatusInternalError("Executor queue is full");
      }
    }
    if (!status.ok()) {
      state_->set_transport_status(status);
      retry_ = false;
      Cleanup();
    }
  }

 private:
  /*
   * One-time preparation of the request to execute.
   * We might retry to send the request, but wont do these steps again.
   */
  void Prepare() {
    BeginPrepare();

    if (request_->content_reader()) {
      AddContentLength(request_->content_reader()->TotalLengthIfKnown());
    }
  }


  void BeginPrepare() {
    AuthorizationCredential* credential = request_->credential_;
    if (credential) {
      googleapis::util::Status status = credential->AuthorizeRequest(request_);
      if (!status.ok()) {
        LOG(ERROR)
            << "Failed authorizing request for url=" << request_->url();
        state_->set_transport_status(status);
        return;
      }
    }
    retry_ = true;
    request_->busy_ = true;

    VLOG(1) << "Adding standard headers";
    if (!request_->FindHeaderValue(HttpRequest::HttpHeader_USER_AGENT)) {
      request_->AddHeader(HttpRequest::HttpHeader_USER_AGENT,
                          request_->transport()->user_agent());
    }
    if (!request_->FindHeaderValue(HttpRequest::HttpHeader_HOST)) {
      ParsedUrl parsed_url(request_->url());
      request_->AddHeader(HttpRequest::HttpHeader_HOST, parsed_url.netloc());
    }
  }

  void AddContentLength(int64 num_bytes) {
    if (num_bytes >= 0) {
      if (!request_->FindHeaderValue(HttpRequest::HttpHeader_CONTENT_LENGTH)) {
        request_->AddHeader(HttpRequest::HttpHeader_CONTENT_LENGTH,
                            SimpleItoa(num_bytes));
      }
    } else if (!request_->FindHeaderValue(
                   HttpRequest::HttpHeader_TRANSFER_ENCODING)) {
      request_->AddHeader(HttpRequest::HttpHeader_TRANSFER_ENCODING, "chunked");
    }
  }

  /*
   * Attempt to execute (or retry to execute) the request.
   *
   * This will either be in support of a synchronous or asynchronous request.
   * If async then the function may return before the attempt finishes.
   * Otherwise if synchronous it will return when the attempt finishes.
   *
   * Regardless we might want to retry again after this returns.
   */
  void AttemptToExecute(bool async) {
    state_->TransitionAndNotifyIfDone(HttpRequestState::PENDING);
    if (scribe_) {
      scribe_->AboutToSendRequest(request_);
    }
    if (async) {
      Closure* closure =
          NewCallback(
              this, &HttpRequestProcessor::PostExecuteAsyncAndDestroy);
      VLOG(1) << "DoExecuteAsync using transport:"
              << request_->transport()->id();
      request_->DoExecuteAsync(request_->response(), closure);
    } else {
      VLOG(1) << "DoExecute using transport:" << request_->transport()->id();
      request_->DoExecute(request_->response());
      DoPostExecute();
    }
  }

  /*
   * Process the response resulting from the execution.
   *
   * This creates the DataReader in the response and notifies the scribe
   * if one is configured.
   */
  void ProcessResponse() {
    HttpResponse* response = request_->response();

    // Form the response_body reader from the collected response
    DataWriter* response_writer = response->body_writer();
    if (response_writer) {
      if (response_writer->ok()) {
        response->set_body_reader(
            response_writer->NewUnmanagedDataReader());
      } else {
        response->set_body_reader(
            NewUnmanagedInvalidDataReader(response_writer->status()));
      }
    }
    if (scribe_) {
      if (response->http_code()) {
        scribe_->ReceivedResponseForRequest(request_);
      } else {
        scribe_->RequestFailedWithTransportError(
            request_, response->transport_status());
      }
    }
  }

  /*
   * Call the error handler, if any.
   *
   * This assumes the request was an error.
   * It will modify the retry_ value depending on whether the caller should
   * attempt to process again or not.
   */
  void HandleError() {
    // We're going to invoke the error handler then, maybe, try again.
    const HttpTransportErrorHandler* handler =
        request_->transport()->options().error_handler();
    if (!handler) {
      retry_ = false;
      return;
    }

    HttpResponse* response = request_->response();
    if (HttpStatusCode::IsRedirect(response->http_code())) {
      retry_ = handler->HandleRedirect(num_redirects_, request_);
      if (retry_) {
        VLOG(9) << "Redirecting to " << request_->url();
        ++num_redirects_;
      }
    } else if (!response->transport_status().ok()) {
      retry_ = handler->HandleTransportError(num_retries_, request_);
      if (retry_) {
        ++num_retries_;
      }
    } else {
      retry_ = handler->HandleHttpError(num_retries_, request_);
      ++num_retries_;
    }
  }

  /*
   * Call the error handler asynchronously, if any.
   *
   * This assumes the request was an error.
   * It will modify the retry_ value depending on whether the caller should
   * attempt to process again or not.
   */
  void HandleErrorAsync(Closure* callback) {
    // We're going to invoke the error handler then, maybe, try again.
    const HttpTransportErrorHandler* handler =
        request_->transport()->options().error_handler();
    if (!handler) {
      retry_ = false;
      callback->Run();
      return;
    }

    HttpResponse* response = request_->response();
    if (HttpStatusCode::IsRedirect(response->http_code())) {
      Callback1<bool>* cb =
          NewCallback(this,
                      &HttpRequestProcessor::HandleRedirectResponseAsync,
                      callback);
      handler->HandleRedirectAsync(num_redirects_, request_, cb);
    } else if (!response->transport_status().ok()) {
      Callback1<bool>* cb =
          NewCallback(this,
                      &HttpRequestProcessor::HandleTransportErrorResponseAsync,
                      callback);
      handler->HandleTransportErrorAsync(num_retries_, request_, cb);
    } else {
      Callback1<bool>* cb =
          NewCallback(this,
                      &HttpRequestProcessor::HandleHttpErrorResponseAsync,
                      callback);
      handler->HandleHttpErrorAsync(num_retries_, request_, cb);
    }
  }

  void HandleRedirectResponseAsync(
      Closure* callback, bool retry) {
    retry_ = retry;
    if (retry_) {
      VLOG(9) << "Redirecting to " << request_->url();
      ++num_redirects_;
    }
    callback->Run();
  }

  void HandleTransportErrorResponseAsync(
      Closure* callback, bool retry) {
    retry_ = retry;
    if (retry_) {
      ++num_retries_;
    }
    callback->Run();
  }

  void HandleHttpErrorResponseAsync(
      Closure* callback, bool retry) {
    retry_ = retry;
    ++num_retries_;
    callback->Run();
  }

  /*
   * Helper function for AttemptToExecute for after HTTP messaging.
   *
   * This function will determine if we need to retry or not, but will
   * not actually attempt a retry.
   */
  void DoPostExecute() {
    // Functionality provided by the base HttpRequest class for
    // setting HttpResponse state
    ProcessResponse();

    // Determine if we need to retry or not.
    if (request_->response()->ok()) {
      retry_ = false;
    } else {
      // May modify retry_ as a side effect.
      HandleError();
    }

    if (retry_) {
      VLOG(1) << "Attempting to retry after http_code="
              << request_->response()->http_code();
    }

    VLOG(9) << "Finished " << state_;
  }

  /*
   * Helper function for AttemptToExecute for after HTTP messaging.
   *
   * This function will determine if we need to retry or not
   * asynchronously, but will not actually attempt a retry.
   */
  void DoPostExecuteAsync(Closure* callback) {
    // Functionality provided by the base HttpRequest class for
    // setting HttpResponse state
    ProcessResponse();

    // Determine if we need to retry or not.
    if (request_->response()->ok()) {
      retry_ = false;
      callback->Run();
    } else {
      // May modify retry_ as a side effect.
      Closure* cb =
          NewCallback(this,
                      &HttpRequestProcessor::DoPostHandleErrorAsync,
                      callback);
      HandleErrorAsync(cb);
    }
  }

  void DoPostHandleErrorAsync(Closure* callback) {
    if (retry_) {
      VLOG(1) << "Attempting to retry after http_code="
              << request_->response()->http_code();
    }

    VLOG(9) << "Finished " << state_;

    callback->Run();
  }

  /*
   * The PostExecute for asynchronous execution.
   *
   * If we need to retry then this will queue the retry request.
   * Otherwise it will cleanup and destroy the instance.
   * The synchronous case does these additional steps in the
   * original ExecuteSync method.
   */
  void PostExecuteAsyncAndDestroy() {
    Closure* cb =
        NewCallback(this,
                    &HttpRequestProcessor::PostExecuteHandleRetry);
    DoPostExecuteAsync(cb);
  }

  void PostExecuteHandleRetry() {
    // If we arent done then requeue the request
    if (retry_) {
      QueueAsync();
    } else {
      Cleanup();
      delete this;
    }
  }

  /*
   * Do one-time execution once we have finished executing the request
   * for the final attempt, whether successful or not.
   *
   * If the request was configured to self-destruct then it will be
   * destroyed here.
   *
   * Note that clients are supposed to either manually delete requests or by
   * calling DestroyWhenDone(), but not both. DestroyWhenDone() must be called
   * at most once.
   */
  void Cleanup() {
    bool destroy_when_done = request_->options_.destroy_when_done();
    request_->busy_ = false;

    final_status_ = state_->AutoTransitionAndNotifyIfDone();

    if (destroy_when_done) {
      delete request_;  // Caller just needs the response object.
    }
  }

  googleapis::util::Status final_status_;  // Ultimate execution status.
  HttpRequest* request_;       // Request to execute owned by caller.
  HttpRequestState* state_;    // Alias to request state.
  HttpScribe* scribe_;         // Alias to request's transport scribe.
  int num_redirects_;          // Number of redirects we followed so far.
  int num_retries_;            // Number of retries so far.
  bool retry_;                 // Whether we should retry again or not.

  DISALLOW_COPY_AND_ASSIGN(HttpRequestProcessor);
};


HttpRequest::HttpRequest(
    HttpRequest::HttpMethod method, HttpTransport* transport)
    : http_method_(method),
      options_(transport->default_request_options()),
      transport_(transport),
      credential_(NULL),
      response_(new HttpResponse),
      // By default the request will present itself to the censorship policy
      // on the scribe. The default censor will still strip sensitive stuff.
      // If for some reason we didnt trust the censor then we can use this
      // attribute to hide parts of the request. This is used for batch
      // requests since the HttpCensor interface does not know about batching.
      scribe_restrictions_(HttpScribe::ALLOW_EVERYTHING),
      busy_(false) {
  CHECK_NOTNULL(transport);

  if (MethodImpliesContent(method)) {
    // Initialize with empty data.
    content_reader_.reset(NewUnmanagedInMemoryDataReader(""));
  }
}

HttpRequest::~HttpRequest() {
}

void HttpRequest::DestroyWhenDone() {
  if (response_->done() && !busy_) {
    delete this;
  } else {
    options_.set_destroy_when_done(true);
  }
}


void HttpRequest::set_content_reader(DataReader* reader) {
  content_reader_.reset(reader);
}

void HttpRequest::set_content_writer(DataWriter* writer) {
  // It is the Response which consumes the network response. Just let it do the
  // forwarding to the writer.
  response_->set_body_writer(writer);
}

void HttpRequest::Clear() {
  // If there was a response waiting on this, then the following will
  // notify it with an abort.
  HttpRequestState* state = response()->mutable_request_state();
  state->set_transport_status(StatusAborted("Cleared request"));
  state->AutoTransitionAndNotifyIfDone().IgnoreError();
  CHECK(!state->has_notify_callback());

  response_->Clear();
  state->Reset();

  credential_ = NULL;
  url_.clear();
  content_reader_.reset(NULL);
  header_map_.clear();
}

const string* HttpRequest::FindHeaderValue(const string& name) const {
  HttpHeaderMap::const_iterator found = header_map_.find(name);
  return (found == header_map_.end()) ? NULL : &found->second;
}

void HttpRequest::RemoveHeader(const string& name) {
  header_map_.erase(name);
}

void HttpRequest::AddHeader(const string& name, const string& value) {
  header_map_[name] = value;
}

void HttpRequest::WillNotExecute(util::Status status) {
  HttpRequestState* state = response()->mutable_request_state();
  CHECK_EQ(HttpRequestState::UNSENT, state->state_code());
  state->set_transport_status(status);
  state->AutoTransitionAndNotifyIfDone().IgnoreError();
}

util::Status HttpRequest::Execute() {
  HttpRequestState* state = response()->mutable_request_state();
  if (state->state_code() != HttpRequestState::QUEUED) {
    CHECK_EQ(HttpRequestState::UNSENT, state->state_code())
        << "Must call Clear() before reusing";
  }

  HttpRequestProcessor executor(this);
  executor.ExecuteSync();
  return executor.final_status();
}

void HttpRequest::set_callback(HttpRequestCallback* callback) {
  CHECK(!mutable_state()->has_notify_callback());
  mutable_state()->set_notify_callback(this, callback);
}

void HttpRequest::DoExecuteAsync(HttpResponse* response, Closure* callback) {
  DoExecute(response);
  if (callback) {
    callback->Run();
  }
}

void HttpRequest::ExecuteAsync(HttpRequestCallback* callback) {
  HttpRequestState* state = response()->mutable_request_state();
  CHECK_EQ(HttpRequestState::UNSENT, state->state_code())
      << "Must Clear request to reuse it.";
  if (callback) {
    set_callback(callback);
  }

  HttpRequestProcessor* executor(new HttpRequestProcessor(this));
  executor->ExecuteAsyncAndDestroy();
}

util::Status HttpRequest::PrepareRedirect(int num_redirects) {
  if (num_redirects >= options_.max_redirects()) {
    return StatusOutOfRange(
        StrCat("Exceeded max_redirects=", options_.max_redirects()));
  }

  const HttpHeaderMultiMap& response_headers = response_->headers();
  HttpHeaderMultiMap::const_iterator location =
      response_headers.find(HttpRequest::HttpHeader_LOCATION);
  if (location == response_headers.end()) {
    return StatusUnknown(
        StrCat("Received HTTP ", response_->http_code(),
               " redirect but not Location Header"));
  }
  const string url = location->second;
  const string resolved_url = ResolveUrl(url_, url);

  VLOG(1) << "Redirecting to " << resolved_url;
  if (response_->http_code() == HttpStatusCode::SEE_OTHER) {
    // 10.3.4 in http://www.w3.org/Protocols/rfc2616/rfc2616-sec10.html
    http_method_ = HttpRequest::GET;
    if (content_reader_.get()) {
      RemoveHeader(HttpHeader_CONTENT_TYPE);
      RemoveHeader(HttpHeader_CONTENT_LENGTH);
      set_content_reader(NULL);
    }
  }

  googleapis::util::Status status = PrepareToReuse();
  if (status.ok()) {
    // Reauthorize request if network location hasnt changed.
    if (credential_) {
      ParsedUrl original_url(this->url());
      ParsedUrl new_url(resolved_url);
      if (new_url.netloc() == original_url.netloc()
          && new_url.scheme() == original_url.scheme()) {
        status = credential_->AuthorizeRequest(this);
      }
    }
    set_url(resolved_url);  // url references a header so set before clearing.
  }
  return status;
}

util::Status HttpRequest::PrepareToReuse() {
  HttpRequestState* state = response()->mutable_request_state();
  response_->body_writer()->Clear();
  if (!response_->body_writer()->ok()) {
    LOG(ERROR) << "Could not clear response writer to redirect.";
    return response_->body_writer()->status();
  }
  state->set_transport_status(StatusOk());
  state->set_http_code(0);
  state->TransitionAndNotifyIfDone(HttpRequestState::UNSENT);
  response_->ClearHeaders();

  std::vector<string> remove_headers;
  string trace;
  for (HttpHeaderMap::const_iterator it = header_map_.begin();
       it != header_map_.end();
       ++it) {
    if (it->first.size() > 3
        && (StringCaseEqual(it->first, HttpHeader_AUTHORIZATION)
            || StringCaseEqual("if-none-match", it->first)
            || StringCaseEqual("if-modified-since", it->first))) {
      remove_headers.push_back(it->first);
      if (VLOG_IS_ON(1)) {
        StrAppend(&trace, " ", it->first);
      }
    }
  }
  VLOG(1) << "Stripping headers on redirect: " << trace;
  for (std::vector<string>::const_iterator it = remove_headers.begin();
       it != remove_headers.end();
       ++it) {
    RemoveHeader(*it);
  }

  return StatusOk();
}

// This function is used by batch requests to convert a request specialized
// for a transport into one specialized by an HttpBatchRequest. It is private
// only permitting the HttpBatchRequest to use it. The method works on any
// request but it is dangerous to generalize because requests instances can
// be coupled with transport specializations that act as their factories.
//
// We are allowing this for batch requests so that higher level components
// that are composed using http requests dont need to know that the request is
// going to be batched when it is constructed and can adjust their attribute
// later on.
void HttpRequest::SwapToRequestThenDestroy(HttpRequest* target) {
  CHECK(!busy_);
  target->options_ = options_;
  target->credential_ = credential_;
  target->content_reader_.swap(content_reader_);
  target->header_map_.swap(header_map_);
  target->response_.swap(response_);
  target->url_.swap(url_);
  target->set_callback(target->response_->request_state().callback());

  delete this;
}

}  // namespace client

}  // namespace googleapis
