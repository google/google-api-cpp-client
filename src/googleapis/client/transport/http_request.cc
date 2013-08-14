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
#include <string>
using std::string;
#include <glog/logging.h>
#include "googleapis/base/mutex.h"
#include "googleapis/base/once.h"
#include "googleapis/client/data/data_reader.h"
#include "googleapis/client/data/data_writer.h"
#include "googleapis/client/transport/http_authorization.h"
#include "googleapis/client/transport/http_request.h"
#include "googleapis/client/transport/http_response.h"
#include "googleapis/client/transport/http_scribe.h"
#include "googleapis/client/transport/http_transport.h"
#include "googleapis/client/transport/http_types.h"
#include "googleapis/client/util/status.h"
#include "googleapis/client/util/uri_utils.h"
#include "googleapis/strings/case.h"
#include "googleapis/strings/strcat.h"
#include "googleapis/util/executor.h"
#include "googleapis/util/status.h"

namespace googleapis {

namespace {

using client::HttpRequest;
using client::HttpRequestState;

GoogleOnceType once_init_ = GOOGLE_ONCE_INIT;

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
typedef map<StringPiece, int, CaseLess> HeaderSortOrderMap;

// This ordering is initialized in InitGlobalVariables when the comparator
// is first used.
scoped_ptr<HeaderSortOrderMap> header_sort_order_;

const bool state_is_done_[] = {
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

void InitGlobalVariables() {
  header_sort_order_.reset(new HeaderSortOrderMap);
  int order = 1;

  header_sort_order_->insert(
      make_pair(HttpRequest::HttpHeader_HOST, order++));
  header_sort_order_->insert(
      make_pair(HttpRequest::HttpHeader_AUTHORIZATION, order++));
  header_sort_order_->insert(
      make_pair(HttpRequest::HttpHeader_CONTENT_LENGTH, order++));
  header_sort_order_->insert(
      make_pair(HttpRequest::HttpHeader_TRANSFER_ENCODING, order++));
  header_sort_order_->insert(
      make_pair(HttpRequest::HttpHeader_CONTENT_TYPE, order++));
  header_sort_order_->insert(
      make_pair(HttpRequest::HttpHeader_LOCATION, order++));
  header_sort_order_->insert(
      make_pair(HttpRequest::HttpHeader_USER_AGENT, order++));
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
  GoogleOnceInit(&once_init_, &InitGlobalVariables);
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


static util::Status DetermineStatus(
    util::Status transport_status,
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

const StringPiece HttpRequest::HttpHeader_AUTHORIZATION("Authorization");
const StringPiece HttpRequest::HttpHeader_CONTENT_LENGTH("Content-Length");
const StringPiece HttpRequest::HttpHeader_CONTENT_TYPE("Content-Type");
const StringPiece HttpRequest::HttpHeader_HOST("Host");
const StringPiece HttpRequest::HttpHeader_LOCATION("Location");
const StringPiece HttpRequest::HttpHeader_TRANSFER_ENCODING(
    "Transfer-Encoding");
const StringPiece HttpRequest::HttpHeader_USER_AGENT("User-Agent");

const StringPiece HttpRequest::ContentType_HTML("text/html");
const StringPiece HttpRequest::ContentType_JSON("application/json");
const StringPiece HttpRequest::ContentType_TEXT("text/plain");
const StringPiece HttpRequest::ContentType_FORM_URL_ENCODED(
    "application/x-www-form-urlencoded");
const StringPiece HttpRequest::ContentType_MULTIPART_MIXED("multipart/mixed");
const StringPiece HttpRequest::ContentType_MULTIPART_RELATED(
    "multipart/related");

const HttpRequest::HttpMethod HttpRequest::DELETE("DELETE");
const HttpRequest::HttpMethod HttpRequest::GET("GET");
const HttpRequest::HttpMethod HttpRequest::HEAD("HEAD");
const HttpRequest::HttpMethod HttpRequest::PATCH("PATCH");
const HttpRequest::HttpMethod HttpRequest::POST("POST");
const HttpRequest::HttpMethod HttpRequest::PUT("PUT");

HttpRequestOptions::HttpRequestOptions()
    : timeout_ms_(10 * 1000),
      max_retries_(1), max_redirects_(5), destroy_when_done_(false) {
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

void HttpRequestState::TransitionAndNotifyIfDone(
    HttpRequestState::StateCode code) {
  VLOG(9) << "set_state_code=" << code << " on " << this;

  // now_done will indicate that we have just transitioned into the done state.
  // It will be true when the new code is done but the previous code wasnt.
  bool now_done = IsStateDone(code);
  HttpRequestCallback* callback = NULL;
  HttpRequest* request;
  {
    MutexLock l(&mutex_);
    // TODO(ewiseblatt): 20130812
    // This is messy calling the callback from here. It is here as part
    // of a refactoring. To finish the refactoring move the entire state
    // transition logic into the request and have this state
    // just be a simple data object shared between request and response.
    if (now_done) {
      now_done = !IsStateDone(state_code_);
      if (now_done && request_) {
        callback = callback_;
        callback_ = NULL;
      }
    }
    state_code_ = code;
    request = request_;
  }

  // There is a race condition here where we havent yet called the callback.
  // The danger is the response could be destroyed here.
  // But since we only call it on the transition, then we should be ok.
  // As an extra guard we state a policy that responses should not be destroyed
  // until they are done. Because of the race condition we cannot enforce it.
  if (now_done) {
    if (callback) {
      // Run the callback before we signal to those waiting to add some
      // determinism. That way we can ensure the callback was run before
      // anyone synchronizing on it continues (if that is how it is used).
      callback->Run(request);
    }

    VLOG(9) << "Signal " << this;
    condvar_.SignalAll();
  }
}

util::Status HttpRequestState::transport_status() const {
  MutexLock l(&mutex_);
  return transport_status_;
}

void HttpRequestState::set_transport_status(const util::Status& status) {
  VLOG(9) << "set_tranport_status=" << status.error_code() << " on " << this;
  MutexLock l(&mutex_);
  transport_status_ = status;
}

util::Status HttpRequestState::AutoTransitionAndNotifyIfDone() {
  util::Status status;
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
      // TODO(ewiseblatt): 20130227
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

  if (timeout_ms > kint32max) {
    timeout_ms = kint32max;
  }

  // TODO(ewiseblatt): 20130325
  // Revisit this when there's more mature time support. I dont want to add
  // it yet just for this.
  const int64 start_time = time(0);
  do {
    int64 now = time(0);
    int64 remaining_ms = timeout_ms - (now - start_time) * 1000;
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


HttpRequest::HttpRequest(
    HttpRequest::HttpMethod method, HttpTransport* transport)
    : http_method_(method),
      options_(transport->default_request_options()),
      transport_(transport),
      credential_(NULL),
      response_(new HttpResponse),
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

const string* HttpRequest::FindHeaderValue(const StringPiece& name) const {
  HttpHeaderMap::const_iterator found = header_map_.find(name.as_string());
  return (found == header_map_.end()) ? NULL : &found->second;
}

void HttpRequest::RemoveHeader(const StringPiece& name) {
  header_map_.erase(name.as_string());
}

void HttpRequest::AddHeader(
    const StringPiece& name, const StringPiece& value) {
  header_map_[name.as_string()] = value.as_string();
}

void HttpRequest::WillNotExecute(
    util::Status status, HttpRequestCallback* callback) {
  HttpRequestState* state = response()->mutable_request_state();
  CHECK_EQ(HttpRequestState::UNSENT, state->state_code());
  if (callback) {
    state->set_notify_callback(this, callback);
  }
  state->set_transport_status(status);
  state->AutoTransitionAndNotifyIfDone().IgnoreError();
}

util::Status HttpRequest::Execute() {
  HttpRequestState* state = response()->mutable_request_state();
  if (state->state_code() != HttpRequestState::QUEUED) {
    CHECK_EQ(HttpRequestState::UNSENT, state->state_code())
        << "Must call Clear() before reusing";
  }
  util::Status status;
  if (credential_) {
    status = credential_->AuthorizeRequest(this);
    if (!status.ok()) {
      LOG(ERROR) << "Failed authorizing request for url=" << url_;
      state->set_transport_status(status);
    }
  }

  busy_ = true;
  if (status.ok()) {
    HttpScribe* scribe = transport_->scribe();
    AddBuiltinHeaders();
    VLOG(9) << "Executing " << state;
    int num_redirects = 0;
    int num_retries = 0;
    bool retry = true;
    while (retry) {
      state->TransitionAndNotifyIfDone(HttpRequestState::PENDING);
      if (scribe) {
        scribe->AboutToSendRequest(this);
      }
      VLOG(1) << "DoExecute using transport:" << transport_->id();
      DoExecute(response_.get());

      // Form the response_body reader from the collected response
      DataWriter* response_writer = response_->body_writer();
      if (response_writer) {
        if (response_writer->ok()) {
          response_->set_body_reader(
              response_writer->NewUnmanagedDataReader());
        } else {
          response_->set_body_reader(
              NewUnmanagedInvalidDataReader(response_writer->status()));
        }
      }
      if (scribe) {
        if (response_->http_code()) {
          scribe->ReceivedResponseForRequest(this);
        } else {
          scribe->RequestFailedWithTransportError(
              this, state->transport_status());
        }
      }
      if (response_->ok()) break;

      // We're going to invoke the error handler then, maybe, try again.
      const HttpTransportErrorHandler* handler =
          transport_->options().error_handler();
      if (!handler) break;

      if (HttpStatusCode::IsRedirect(response_->http_code())) {
        retry = handler->HandleRedirect(num_redirects, this);
        if (retry) {
          VLOG(9) << "Redirecting to " << url_;
          ++num_redirects;
        }
      } else if (!response_->transport_status().ok()) {
        retry = handler->HandleTransportError(num_retries, this);
        if (retry) {
          ++num_retries;
        }
      } else {
        retry = handler->HandleHttpError(num_retries, this);
        ++num_retries;
      }
      if (retry) {
        VLOG(1) << "Attempting to retry after http_code="
                << response_->http_code();
      }
    };
    VLOG(9) << "Finished " << state;
  }

  status = state->AutoTransitionAndNotifyIfDone();
  busy_ = false;

  if (options_.destroy_when_done()) {
    VLOG(1) << "Auto-deleting " << this;
    delete this;  // Caller just needs the response object.
  }

  return status;
}

static void ExecuteRequestHelper(HttpRequest* request) {
  request->Execute().IgnoreError();
}

void HttpRequest::ExecuteAsync(HttpRequestCallback* callback) {
  HttpRequestState* state = response()->mutable_request_state();
  CHECK_EQ(HttpRequestState::UNSENT, state->state_code())
      << "Must Clear request to reuse it.";
  CHECK(!state->has_notify_callback());
  if (callback) {
    state->set_notify_callback(this, callback);
  }

  thread::Executor* executor = transport()->options().executor();
  util::Status status;
  if (!executor) {
    status = StatusInternalError("No default executor configured");
  } else {
    Closure* closure = NewCallback(&ExecuteRequestHelper, this);
    state->TransitionAndNotifyIfDone(HttpRequestState::QUEUED);
    if (!executor->TryAdd(closure)) {
      delete closure;
      status = StatusInternalError("Executor queue is full");
    }
  }

  if (!status.ok()) {
    state->set_transport_status(status);
    state->AutoTransitionAndNotifyIfDone().IgnoreError();
    if (options_.destroy_when_done()) {
      VLOG(1) << "Auto-deleting " << this;
      delete this;  // Caller just needs the response object.
    }
  }
}

util::Status HttpRequest::PrepareRedirect(int num_redirects) {
  if (num_redirects >= options_.max_redirects()) {
    return StatusOutOfRange(
        StrCat("Exceeded max_redirects=", options_.max_redirects()));
  }

  const HttpHeaderMultiMap& response_headers = response_->headers();
  HttpHeaderMultiMap::const_iterator location =
      response_headers.find(HttpRequest::HttpHeader_LOCATION.as_string());
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
  util::Status status = PrepareToReuse();
  if (status.ok()) {
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
  state->set_http_code(0);
  state->TransitionAndNotifyIfDone(HttpRequestState::UNSENT);
  response_->ClearHeaders();

  vector<StringPiece> remove_headers;
  string trace;
  for (HttpHeaderMap::const_iterator it = header_map_.begin();
       it != header_map_.end();
       ++it) {
    if (it->first.size() > 3
        && (CaseEqual(it->first, HttpHeader_AUTHORIZATION)
            || CaseEqual("if-none-match", it->first)
            || CaseEqual("if-modified-since", it->first))) {
      remove_headers.push_back(it->first);
      if (VLOG_IS_ON(1)) {
        StrAppend(&trace, " ", it->first);
      }
    }
  }
  VLOG(1) << "Stripping headers on redirect: " << trace;
  for (vector<StringPiece>::const_iterator it = remove_headers.begin();
       it != remove_headers.end();
       ++it) {
    RemoveHeader(*it);
  }

  return StatusOk();
}

void HttpRequest::AddBuiltinHeaders() {
  VLOG(1) << "Adding builtin headers";
  if (!FindHeaderValue(HttpHeader_USER_AGENT)) {
    AddHeader(HttpHeader_USER_AGENT, transport()->user_agent());
  }

  if (!FindHeaderValue(HttpHeader_HOST)) {
    ParsedUrl parsed_url(url_);
    AddHeader(HttpHeader_HOST, parsed_url.netloc());
  }

  if (content_reader_.get()) {
    int64 num_bytes = content_reader_->TotalLengthIfKnown();
    if (num_bytes >= 0) {
      if (!FindHeaderValue(HttpHeader_CONTENT_LENGTH)) {
        AddHeader(HttpHeader_CONTENT_LENGTH, SimpleItoa(num_bytes));
      }
    } else {
      if (!FindHeaderValue(HttpHeader_TRANSFER_ENCODING)) {
        AddHeader(HttpHeader_TRANSFER_ENCODING, "chunked");
      }
    }
  }
}

}  // namespace client

} // namespace googleapis