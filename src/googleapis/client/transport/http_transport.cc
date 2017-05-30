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


#include <map>

#include "googleapis/client/data/data_reader.h"
#include "googleapis/client/data/data_writer.h"
#include "googleapis/util/executor.h"
#include "googleapis/client/transport/ca_paths.h"
#include "googleapis/client/transport/http_transport.h"
#include "googleapis/client/transport/http_authorization.h"
#include "googleapis/client/transport/http_request.h"
#include "googleapis/client/transport/http_response.h"
#include "googleapis/client/transport/http_scribe.h"
#include "googleapis/client/transport/http_types.h"
#include "googleapis/client/transport/versioninfo.h"
#include "googleapis/client/util/status.h"
#include "googleapis/client/util/program_path.h"
#include <glog/logging.h>
#include "googleapis/strings/strcat.h"
#include "googleapis/strings/stringpiece.h"
#include "googleapis/strings/strip.h"
#include "googleapis/strings/numbers.h"
#include "googleapis/strings/util.h"

namespace googleapis {

namespace {

string BuildStandardUserAgentString(const string& application) {
  string user_agent;
  if (!application.empty()) {
    user_agent = StrCat(application, " ");
  }

  StrAppend(&user_agent,
            client::HttpTransportOptions::kGoogleApisUserAgent,
            "/", client::VersionInfo::GetVersionString(),
            " ", client::VersionInfo::GetPlatformString());

  return user_agent;
}

}  // anonymous namespace

namespace client {

// These global constants are declared in http_types.h
const string kCRLF("\r\n");  // NOLINT
const string kCRLFCRLF("\r\n\r\n");  // NOLINT


HttpTransportLayerConfig::HttpTransportLayerConfig() {
  ResetDefaultErrorHandler(new HttpTransportErrorHandler);
}

HttpTransportLayerConfig::~HttpTransportLayerConfig() {
}

void HttpTransportLayerConfig::ResetDefaultErrorHandler(
    HttpTransportErrorHandler* error_handler) {
  VLOG(1) << "Resetting default error handler";
  default_error_handler_.reset(error_handler);
  default_options_.set_error_handler(error_handler);
}

void HttpTransportLayerConfig::ResetDefaultExecutor(
    thread::Executor* executor) {
  VLOG(1) << "Resetting default executor";
  default_executor_.reset(executor);
  default_options_.set_executor(executor);
}

void HttpTransportLayerConfig::ResetDefaultTransportFactory(
    HttpTransportFactory* factory) {
  VLOG(1) << "Setting default transport factory = " << factory->default_id();
  default_transport_factory_.reset(factory);
}

void HttpTransportOptions::set_cacerts_path(const string& path) {
  VLOG(1) << "Initializing cacerts_path=" << path;
  cacerts_path_ = path;
  ssl_verification_disabled_ = path == kDisableSslVerification;
  if (ssl_verification_disabled_)
    LOG(WARNING) << "Disabled SSL verification";
}

void HttpTransportOptions::set_connect_timeout_ms(int64 connect_timeout_ms) {
  VLOG(1) << "Initializing connect_timeout_ms=" << connect_timeout_ms;
  if (connect_timeout_ms < 0) {
    connect_timeout_ms = 0;
  }
  connect_timeout_ms_ = connect_timeout_ms;
}

void HttpTransportOptions::set_nonstandard_user_agent(const string& agent) {
  VLOG(1) << "Setting user_agent = " << agent;
  user_agent_ = agent;
}

void HttpTransportOptions::SetApplicationName(const string& name) {
  user_agent_ = BuildStandardUserAgentString(name);
  VLOG(1) << "Setting ApplicationName = " << name;
}

HttpTransportErrorHandler::HttpTransportErrorHandler() {
}

HttpTransportErrorHandler::~HttpTransportErrorHandler() {
  auto begin = specialized_http_code_handlers_.begin();
  auto end = specialized_http_code_handlers_.end();
  while (begin != end) {
    auto temp = begin;
    ++begin;
    delete temp->second;
  }
}

void HttpTransportErrorHandler::ResetHttpCodeHandler(
    int code, HttpTransportErrorHandler::HttpCodeHandler* handler) {
  std::map<int, HttpCodeHandler*>::iterator found =
      specialized_http_code_handlers_.find(code);
  if (found != specialized_http_code_handlers_.end()) {
    delete found->second;
    if (!handler) {
      specialized_http_code_handlers_.erase(found);
    }
  }
  if (handler) {
    specialized_http_code_handlers_.insert(std::make_pair(code, handler));
  }
}

bool HttpTransportErrorHandler::HandleTransportError(
    int num_retries, HttpRequest* request) const {
  return false;
}

void HttpTransportErrorHandler::HandleTransportErrorAsync(
    int num_retries, HttpRequest* request, Callback1<bool>* callback) const {
  callback->Run(false);
}

bool HttpTransportErrorHandler::HandleHttpError(
    int num_retries_so_far, HttpRequest* request) const {
  int http_code = request->response()->http_code();
  std::map<int, HttpCodeHandler*>::const_iterator found =
      specialized_http_code_handlers_.find(http_code);
  if (found != specialized_http_code_handlers_.end()) {
    VLOG(2) << "Using overriden error handler for http_code=" << http_code;
    return found->second->Run(num_retries_so_far, request);
  }

  if (http_code == HttpStatusCode::UNAUTHORIZED) {
    if (!num_retries_so_far) {
      // Only try unauthorized once.
      AuthorizationCredential* credential = request->credential();
      if (credential) {
        googleapis::util::Status status = credential->Refresh();
        if (status.ok()) {
          VLOG(2) << "Refreshed credential";
          status = request->PrepareToReuse();
          if (!status.ok()) {
            LOG(ERROR) << "Failed to reuse HTTP request.";
            return false;
          }
          status = credential->AuthorizeRequest(request);
          if (status.ok()) {
            VLOG(1) << "Re-authorized credential";
            return true;
          } else {
            LOG(ERROR) << "Failed reauthorizing request: "
                       << status.error_message();
          }
        } else {
          LOG(ERROR)
              << "Failed refreshing credential: " << status.error_message();
        }
      } else {
        VLOG(2) << "No credential provided where one was expected.";
      }
    } else {
      // TODO(user): 20130616
      // Here a retry is a retry. So a 503 retry that results in a 401
      // would fail even though we never retried the 401 error.
      VLOG(2) << "Already retried with a http_code="
              << HttpStatusCode::UNAUTHORIZED;
    }
  } else {
    // This isnt to say that the caller wont be handling the error later.
    VLOG(2) << "No configured error handler for http_code=" << http_code;
  }
  return false;
}

void HttpTransportErrorHandler::HandleHttpErrorAsync(
    int num_retries_so_far,
    HttpRequest* request,
    Callback1<bool>* callback) const {
  int http_code = request->response()->http_code();
  std::map<int, HttpCodeHandler*>::const_iterator found =
      specialized_http_code_handlers_.find(http_code);
  if (found != specialized_http_code_handlers_.end()) {
    VLOG(2) << "Using overriden error handler for http_code=" << http_code;
    callback->Run(found->second->Run(num_retries_so_far, request));
    return;
  }

  if (http_code == HttpStatusCode::UNAUTHORIZED) {
    if (!num_retries_so_far) {
      // Only try unauthorized once.
      AuthorizationCredential* credential = request->credential();
      if (credential) {
        Callback1<util::Status>* cb =
            NewCallback(this, &HttpTransportErrorHandler::HandleRefreshAsync,
                        callback, request);
        credential->RefreshAsync(cb);
        return;
      } else {
        VLOG(2) << "No credential provided where one was expected.";
      }
    } else {
      // TODO(user): 20130616
      // Here a retry is a retry. So a 503 retry that results in a 401
      // would fail even though we never retried the 401 error.
      VLOG(2) << "Already retried with a http_code="
              << HttpStatusCode::UNAUTHORIZED;
    }
  } else {
    // This isnt to say that the caller wont be handling the error later.
    VLOG(2) << "No configured error handler for http_code=" << http_code;
  }
  callback->Run(false);
}

void HttpTransportErrorHandler::HandleRefreshAsync(
    Callback1<bool>* callback,
    HttpRequest* request,
    googleapis::util::Status status) const {
  if (status.ok()) {
    VLOG(2) << "Refreshed credential";
    status = request->credential()->AuthorizeRequest(request);
    if (status.ok()) {
      VLOG(1) << "Re-authorized credential";
      callback->Run(true);
      return;
    } else {
      LOG(ERROR) << "Failed reauthorizing request: "
                 << status.error_message();
    }
  } else {
    LOG(ERROR)
        << "Failed refreshing credential: " << status.error_message();
  }
  callback->Run(false);
}

bool HttpTransportErrorHandler::HandleRedirect(
    int num_redirects, HttpRequest* request) const {
  return ShouldRetryRedirect_(num_redirects, request);
}

void HttpTransportErrorHandler::HandleRedirectAsync(
    int num_redirects,
    HttpRequest* request,
    Callback1<bool>* callback) const {
  callback->Run(ShouldRetryRedirect_(num_redirects, request));
}

bool HttpTransportErrorHandler::ShouldRetryRedirect_(
    int num_redirects, HttpRequest* request) const {
  int http_code = request->response()->http_code();
  std::map<int, HttpCodeHandler*>::const_iterator found =
      specialized_http_code_handlers_.find(http_code);
  if (found != specialized_http_code_handlers_.end()) {
    VLOG(2) << "Using overriden redirect handler for http_code=" << http_code;
    return found->second->Run(num_redirects, request);
  }

  if (HttpStatusCode::IsRedirect(http_code)
      && http_code != HttpStatusCode::MULTIPLE_CHOICES) {
    googleapis::util::Status status = request->PrepareRedirect(num_redirects);
    if (status.ok()) {
      return true;
    }
    request->mutable_state()->set_transport_status(status);
  }
  return false;
}

HttpTransportOptions::HttpTransportOptions()
    : proxy_port_(0),
      ssl_verification_disabled_(false),
      connect_timeout_ms_(0L),
      executor_(nullptr),
      callback_executor_(nullptr),
      error_handler_(nullptr) {
  string app_name = DetermineDefaultApplicationName();
  user_agent_ = BuildStandardUserAgentString(app_name);

  // TODO(user): Resolve if we should do this or not. The application should
  // really decide where the certs are and do the call to
  // options.set_cacerts_path(DetermineDefaultCaCertsPath()).
  // But this is breaking, so we need to discuss the appropriate choice for
  // desktop clients like DriveFS with security. Do we ship a certs.pem from
  // drivefs or from boringssl? In any case, this is an application choice
  // not the SDK's choice.
  cacerts_path_ = client::DetermineDefaultCaCertsPath();
  VLOG(1) << "Setting default cacerts_path=" << cacerts_path_;
}

HttpTransportOptions::~HttpTransportOptions() {}

thread::Executor* HttpTransportOptions::executor() const {
  if (!executor_) {
    return thread::Executor::DefaultExecutor();
  }
  return executor_;
}

thread::Executor* HttpTransportOptions::callback_executor() const {
  if (!callback_executor_) {
    return thread::SingletonInlineExecutor();
  }
  return callback_executor_;
}

const char HttpTransportOptions::kGoogleApisUserAgent[] =
    "google-api-cpp-client";

const char HttpTransportOptions::kDisableSslVerification[] =
    "DisableSslVerification";

HttpTransport::HttpTransport(const HttpTransportOptions& options)
    : options_(options), scribe_(nullptr), in_shutdown_(false) {
  set_id("Unidentified");
}

HttpTransport::~HttpTransport() {
}

void HttpTransport::Shutdown() {
  in_shutdown_ = true;
}


HttpTransport* HttpTransportLayerConfig::NewDefaultTransport(
    googleapis::util::Status* status) const {
  HttpTransportFactory* factory = default_transport_factory_.get();
  if (!factory) {
    *status = StatusInternalError(
        "ResetDefaultTransportFactory has not been called.");
    return NULL;
  }

  return factory->NewWithOptions(default_options_);
}

/* static */
void HttpTransport::WriteRequestPreamble(
    const HttpRequest* request, DataWriter* writer) {
  // Write start-line
  if (!writer->Write(StrCat(request->http_method(), " ",
                            request->url(), " HTTP/1.1", kCRLF)).ok()) {
    return;  // error in status()
  }

  // Write headers
  const HttpHeaderMap& header_map = request->headers();
  for (HttpHeaderMap::const_iterator it = header_map.begin();
       it != header_map.end();
       ++it) {
    // Dont bother checking for errors since we're probably good at this point.
    // They'll be in the status, which is sticky, so wont get lost anyway.
    writer->Write(StrCat(it->first, ": ", it->second, kCRLF)).IgnoreError();
  }
  writer->Write(kCRLF).IgnoreError();
}

/* static */
void HttpTransport::WriteRequest(
    const HttpRequest* request, DataWriter* writer) {
  WriteRequestPreamble(request, writer);
  DataReader* content = request->content_reader();
  if (content) {
    // TODO(user): 20130820
    // Check for chunked transfer encoding and, if so, write chunks
    // by iterating Write's using some chunk size.
    writer->Write(content).IgnoreError();
  }
}

/* static */
void HttpTransport::ReadResponse(DataReader* reader, HttpResponse* response) {
  response->Clear();
  const StringPiece kHttpIdentifier("HTTP/1.1 ");
  HttpRequestState* state = response->mutable_request_state();
  string first_line;
  bool found = reader->ReadUntilPatternInclusive(
      kCRLF, &first_line);
  if (!found || !StringPiece(first_line).starts_with(kHttpIdentifier)) {
    state->set_transport_status(StatusUnknown("Expected leading 'HTTP/1.1'"));
    return;
  }
  int space = first_line.find(' ');
  int http_code = 0;
  if (space != StringPiece::npos) {
    safe_strto32(first_line.c_str() + space + 1, &http_code);
  }
  if (!http_code) {
    state->set_transport_status(
        StatusUnknown("Expected HTTP response code on first line"));
    return;
  }
  state->set_http_code(http_code);
  do {
    string header_line;
    if (!reader->ReadUntilPatternInclusive(kCRLF, &header_line)) {
      googleapis::util::Status error;
      if (reader->done()) {
        error = StatusUnknown("Expected headers to end with an empty CRLF");
      } else {
        error = StatusUnknown("Expected header to end with CRLF");
      }
      state->set_transport_status(error);
      return;
    }
    if (header_line == kCRLF) break;

    int colon = header_line.find(':');
    if (colon == string::npos) {
      googleapis::util::Status error = StatusUnknown(
          StrCat("Expected ':' in header #", response->headers().size()));
      state->set_transport_status(error);
      return;
    }
    StringPiece line_piece(header_line);
    StringPiece name = line_piece.substr(0, colon);
    StringPiece value = line_piece.substr(
        colon + 1, header_line.size() - colon - 1 - kCRLF.size());
    StripWhitespace(&name);
    StripWhitespace(&value);
    response->AddHeader(name.as_string(), value.as_string());
  } while (true);  // break above when header_line is empty

  // Remainder of reader is the response payload.
  response->body_writer()->Write(reader).IgnoreError();
}

HttpTransport*
HttpTransportLayerConfig::NewDefaultTransportOrDie() const {
  googleapis::util::Status status;
  HttpTransport* result = NewDefaultTransport(&status);
  if (!result) {
    LOG(FATAL) << "Could not create transport.";
  }

  return result;
}

HttpTransport* HttpTransportFactory::NewWithOptions(
    const HttpTransportOptions& options) {
  HttpTransport* transport = DoAlloc(options);
  if (scribe_.get()) {
    transport->set_scribe(scribe_.get());
  }
  return transport;
}

void HttpTransportFactory::reset_scribe(HttpScribe* scribe) {
  scribe_.reset(scribe);
}

HttpTransportFactory::HttpTransportFactory()
    : config_(NULL), default_id_("UNKNOWN") {
}

HttpTransportFactory::HttpTransportFactory(
    const HttpTransportLayerConfig* config)
    : config_(config), default_id_("UNKNOWN") {
}

HttpTransportFactory::~HttpTransportFactory() {
}

}  // namespace client

}  // namespace googleapis
