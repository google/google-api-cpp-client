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


#include "googleapis/client/transport/http_transport.h"
#include "googleapis/client/transport/http_authorization.h"
#include "googleapis/client/transport/http_request.h"
#include "googleapis/client/transport/http_response.h"
#include "googleapis/client/transport/http_scribe.h"
#include "googleapis/client/transport/versioninfo.h"
#include "googleapis/client/util/status.h"
#include "googleapis/base/once.h"
#include <glog/logging.h>
#include "googleapis/base/scoped_ptr.h"
#include "googleapis/util/file.h"

#include "googleapis/strings/strcat.h"
#include "googleapis/strings/stringpiece.h"
#include "googleapis/util/stl_util.h"
#include "googleapis/util/executor.h"

namespace googleapis {


using googleapis::File;


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

void HttpTransportOptions::set_cacerts_path(const StringPiece& path) {
  VLOG(1) << "Initializing cacerts_path=" << path;
  cacerts_path_ = path.as_string();
  ssl_verification_disabled_ = path == kDisableSslVerification;
  if (ssl_verification_disabled_)
    LOG(WARNING) << "Disabled SSL verification";
}

void HttpTransportOptions::set_nonstandard_user_agent(const string& agent) {
  VLOG(1) << "Setting user_agent = " << agent;
  user_agent_ = agent;
}

void HttpTransportOptions::SetApplicationName(const StringPiece& name) {
  user_agent_ = BuildStandardUserAgentString(name.as_string());
  VLOG(1) << "Setting ApplicationName = " << name;
}

/* static */
string HttpTransportOptions::DetermineDefaultCaCertsPath() {
const string program_path = File::GetCurrentProgramFilenamePath();
  StringPiece basename = File::Basename(program_path);
  StringPiece dirname = File::StripBasename(program_path);
  return StrCat(dirname, "roots.pem");  // dirname has ending slash.
}

/* static */
string HttpTransportOptions::DetermineDefaultApplicationName() {
  const string program_path = File::GetCurrentProgramFilenamePath();
  StringPiece basename = File::Basename(program_path);
  int dot = basename.rfind('.');
  if (dot != StringPiece::npos) {
    basename = basename.substr(0, dot);
  }
  return basename.as_string();
}

HttpTransportErrorHandler::HttpTransportErrorHandler() {
}

HttpTransportErrorHandler::~HttpTransportErrorHandler() {
  STLDeleteContainerPairSecondPointers(
      specialized_http_code_handlers_.begin(),
      specialized_http_code_handlers_.end());
}

void HttpTransportErrorHandler::ResetHttpCodeHandler(
    int code, HttpTransportErrorHandler::HttpCodeHandler* handler) {
  map<int, HttpCodeHandler*>::iterator found =
      specialized_http_code_handlers_.find(code);
  if (found != specialized_http_code_handlers_.end()) {
    delete found->second;
    if (!handler) {
      specialized_http_code_handlers_.erase(found);
    }
  }
  if (handler) {
    specialized_http_code_handlers_.insert(make_pair(code, handler));
  }
}

bool HttpTransportErrorHandler::HandleTransportError(
    int num_retries, HttpRequest* request) const {
  return false;
}

bool HttpTransportErrorHandler::HandleHttpError(
    int num_retries_so_far, HttpRequest* request) const {
  int http_code = request->response()->http_code();
  map<int, HttpCodeHandler*>::const_iterator found =
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
        util::Status status = credential->Refresh();
        if (status.ok()) {
          VLOG(2) << "Refreshed credential";
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
      // TODO(ewiseblatt): 20130616
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

bool HttpTransportErrorHandler::HandleRedirect(
    int num_redirects, HttpRequest* request) const {
  int http_code = request->response()->http_code();
  map<int, HttpCodeHandler*>::const_iterator found =
      specialized_http_code_handlers_.find(http_code);
  if (found != specialized_http_code_handlers_.end()) {
    VLOG(2) << "Using overriden redirect handler for http_code=" << http_code;
    return found->second->Run(num_redirects, request);
  }

  if (HttpStatusCode::IsRedirect(http_code)
      && http_code != HttpStatusCode::MULTIPLE_CHOICES) {
    util::Status status = request->PrepareRedirect(num_redirects);
    if (status.ok()) {
      return true;
    }
    request->mutable_state()->set_transport_status(status);
  }
  return false;
}

HttpTransportOptions::HttpTransportOptions()
    : executor_(NULL), error_handler_(NULL) {
  string app_name = DetermineDefaultApplicationName();
  user_agent_ = BuildStandardUserAgentString(app_name);

  cacerts_path_ = DetermineDefaultCaCertsPath();
ssl_verification_disabled_ = false;
  VLOG(1) << "Setting default cacerts_path=" << cacerts_path_;
}

HttpTransportOptions::~HttpTransportOptions() {}

thread::Executor* HttpTransportOptions::executor() const {
  if (!executor_) {
    return thread::Executor::DefaultExecutor();
  }
  return executor_;
}

const StringPiece HttpTransportOptions::kGoogleApisUserAgent =
    "google-api-cpp-client";

const StringPiece HttpTransportOptions::kDisableSslVerification =
    "DisableSslVerification";

HttpTransport::HttpTransport(const HttpTransportOptions& options)
    : options_(options), scribe_(NULL) {
  set_id("Unidentified");
}

HttpTransport::~HttpTransport() {
}

HttpTransport* HttpTransportLayerConfig::NewDefaultTransport(
    util::Status* status) const {
  HttpTransportFactory* factory = default_transport_factory_.get();
  if (!factory) {
    *status = StatusInternalError(
        "ResetDefaultTransportFactory has not been called.");
    return NULL;
  }

  return factory->NewWithOptions(default_options_);
}

HttpTransport*
HttpTransportLayerConfig::NewDefaultTransportOrDie() const {
  util::Status status;
  HttpTransport* result = NewDefaultTransport(&status);
  if (!result) {
    LOG(FATAL) << status.ToString();
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

} // namespace googleapis