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
// LOG level 1 for status
// LOG level 8 for checking headers we're touching
// LOG level 9 for actual header tracing

#include <iostream>
using std::cout;
using std::endl;
using std::ostream;  // NOLINT
#include <string>
using std::string;
#include <list>
#include <vector>

#include "googleapis/client/data/data_reader.h"
#include "googleapis/client/data/data_writer.h"
#include "googleapis/client/transport/curl_http_transport.h"
#include "googleapis/client/transport/http_request.h"
#include "googleapis/client/transport/http_response.h"
#include "googleapis/client/transport/http_transport.h"
#include "googleapis/client/transport/http_types.h"
#include "googleapis/client/util/status.h"
#include <glog/logging.h>
#include "curl/curl.h"
#include "googleapis/strings/join.h"
#include "googleapis/strings/strip.h"

namespace googleapis {

using client::CurlHttpTransport;
using client::DataReader;
using client::DataWriter;
using client::HttpHeaderMap;
using client::HttpHeaderMultiMap;
using client::HttpRequest;
using client::HttpRequestState;
using client::HttpResponse;
using client::HttpStatusCode;
using client::HttpTransport;
using client::HttpTransportOptions;
using client::NewManagedInMemoryDataReader;
using client::StatusDeadlineExceeded;
using client::StatusInternalError;
using client::StatusInvalidArgument;
using client::StatusOk;
using client::StatusOutOfRange;
using client::StatusUnknown;


namespace client {

namespace {

inline bool is_space_or_cntrl(char c) {
  return c <= ' ';
}

// Helper function when reading response headers that strips off trailing \r\n
// since StripWhitespace does not.
void StripWhitespaceAndCntrl(StringPiece* str) {
  int len = str->size();
  const char* data = str->data();
  while (len > 0 && is_space_or_cntrl(data[len - 1])) {
    --len;
  }

  int offset = 0;
  for (; offset < len && is_space_or_cntrl(data[offset]); ++offset) {
    // empty;
  }
  str->set(data + offset, len - offset);
}

}  // anonymous namespace

class CurlHttpRequest : public HttpRequest {
 public:
  CurlHttpRequest(const HttpMethod& method, CurlHttpTransport* transport);
  ~CurlHttpRequest();

 protected:
  virtual void DoExecute(HttpResponse* response);
};

util::Status StatusFromCurlCode(CURLcode code, const StringPiece& msg) {
  if (code == CURLE_OK) return StatusOk();

  util::error::Code status_code;
  StringPiece type;
  string detail = msg.empty() ? "" : StrCat(": ", msg);
  switch (code) {
    case CURLE_OPERATION_TIMEDOUT:
      type = "Timed out";
      status_code = util::error::DEADLINE_EXCEEDED;
      break;

    case CURLE_URL_MALFORMAT:
      type = "Bad url";
      status_code = util::error::INVALID_ARGUMENT;
      break;

    case CURLE_SSL_ISSUER_ERROR:
      type = "SSL Issuer Check Failed.";
      status_code = util::error::INVALID_ARGUMENT;
      break;

    case CURLE_COULDNT_CONNECT:
      type = "Couldnt connect";
      status_code = util::error::UNAVAILABLE;
      break;

    default:
      status_code = util::error::UNKNOWN;
  }

  return googleapis::util::Status(status_code, StrCat(type, ". curl=", code, detail));
}

// The CurlProcessor is an individual stateful request processor.
// It can be reused across requests but can only fulfill one at a time.
// These are internally used to allow the connection-based class to
// process multiple requests at a time (using one of these per request).
//
// NOTE(user): 20120925
// The use of "const HttpRequest" in this class' methods is an implementation
// detail, not a design constraint. The external interface does permit
// modifications to the request, intended for injecting security headers.
// This happens at a higher level than this class so we dont need to worry
// here, but other mutations may follow in the future if a design need arises.
//
// If the curl processor cannot initialize, it will set the internal curl_
// handle to NULL and fail future requests.
class CurlProcessor {
 public:
  explicit CurlProcessor(HttpTransport* transport)
      : curl_(NULL), transport_(transport) {
  }

  ~CurlProcessor() {
    if (curl_) {
      curl_easy_cleanup(curl_);
    }
  }

 private:
  CURL* curl_;
  HttpTransport* transport_;
  DataReader* send_content_reader_;
  HttpResponse* response_;
  const HttpRequest* request_;
  int http_code_;
  char error_buffer_[CURL_ERROR_SIZE];

  googleapis::util::Status LazyInitCurl() {
    if (!curl_) {
      curl_ = curl_easy_init();
      if (!curl_) {
        return StatusInternalError("Could not initialize curl");
      }
      googleapis::util::Status status = InitStandardOptions();
      if (!status.ok()) {
        curl_easy_cleanup(curl_);
        curl_ = NULL;
        return status;
      }
    }
    return StatusOk();
  }

  googleapis::util::Status InitStandardOptions() {
    bool ok = true;
    ok = ok && !curl_easy_setopt(
        curl_, CURLOPT_HEADERFUNCTION, ResultHeaderCallback);
    ok = ok && !curl_easy_setopt(curl_, CURLOPT_WRITEHEADER, this);
    ok = ok && !curl_easy_setopt(
        curl_, CURLOPT_WRITEFUNCTION, ResultBodyCallback);
    ok = ok && !curl_easy_setopt(curl_, CURLOPT_WRITEDATA, this);

    // We'll set the readdata but set the function on demand since we dont
    // always want to send data.
    ok = ok && !curl_easy_setopt(curl_, CURLOPT_READDATA, this);
    ok = ok && !curl_easy_setopt(curl_, CURLOPT_ERRORBUFFER, error_buffer_);
    if (!ok) {
      const char error[] = "Unexpected error setting up basic IO helpers";
      LOG(ERROR) << error;
      return StatusInternalError(error);
    }

    const HttpTransportOptions& options = transport_->options();
    if (!options.proxy_host().empty()) {
      ok = ok && !curl_easy_setopt(
          curl_, CURLOPT_PROXY, options.proxy_host().c_str());
      if (options.proxy_port()) {
        ok = ok && !curl_easy_setopt(
            curl_, CURLOPT_PROXYPORT, options.proxy_port());
      }
      if (!ok) {
        const char error[] = "Unexpected error setting proxy";
        LOG(ERROR) << error;
        return StatusInternalError(error);
      }
      VLOG(1) << "Using proxy host=" << options.proxy_host()
              << " port=" << options.proxy_port();
    }
    const string& cacerts_path = options.cacerts_path();
    if (options.ssl_verification_disabled()) {
      LOG_FIRST_N(WARNING, 1)
          << "Disabling SSL_VERIFYPEER.";
      curl_easy_setopt(curl_, CURLOPT_SSL_VERIFYPEER, false);
    } else if (cacerts_path.empty()) {
      const char error[] = "Transport options have no caccerts_path.";
      LOG(ERROR) << error;
      return StatusInternalError(error);
    } else if (curl_easy_setopt(curl_, CURLOPT_CAINFO, cacerts_path.c_str())) {
      string error = StrCat("Error setting certs from ", cacerts_path);
      LOG(ERROR) << error;
      return StatusInvalidArgument(error);
    } else {
      LOG_FIRST_N(INFO, 1) << "Using cacerts from " << cacerts_path;
    }

    // nosignals because we are multithreaded
    ok = ok && !curl_easy_setopt(curl_, CURLOPT_NOSIGNAL, 1L);

    ok = ok &&
         !curl_easy_setopt(curl_, CURLOPT_CONNECTTIMEOUT_MS,
                           options.connect_timeout_ms() <= 0
                               ? 10000L
                               : options.connect_timeout_ms());

    // This timeout is in seconds.
    ok = ok && !curl_easy_setopt(curl_, CURLOPT_DNS_CACHE_TIMEOUT, 60L);

    // For security we'll handle redirects ourself.
    ok = ok && !curl_easy_setopt(curl_, CURLOPT_FOLLOWLOCATION, 0);

    if (!ok) {
      const char error[] = "Failed some transport configuration";
      LOG(ERROR) << error;
      return StatusInternalError(error);
    }

    return StatusOk();
  }

  googleapis::util::Status PrepareRequestOptions(
      const HttpRequest* request, struct curl_slist** curl_headers) {
    googleapis::util::Status status = LazyInitCurl();
    if (!status.ok()) {
      return status;
    }

    // Set the headers
    for (HttpHeaderMap::const_iterator it = request->headers().begin();
         it != request->headers().end();
         ++it) {
      string s = StrCat(it->first, ": ", it->second);
      *curl_headers = curl_slist_append(*curl_headers, s.c_str());
    }

    bool ok = true;
    const HttpRequest::HttpMethod& method = request->http_method();
    if (method == HttpRequest::GET) {
      ok = ok && !curl_easy_setopt(curl_, CURLOPT_HTTPGET, true);
    } else if (method == HttpRequest::POST) {
      ok = ok && !curl_easy_setopt(curl_, CURLOPT_POST, true);
    } else if (method == HttpRequest::PUT) {
      ok = ok && !curl_easy_setopt(curl_, CURLOPT_PUT, true);
      ok = ok && !curl_easy_setopt(curl_, CURLOPT_UPLOAD, true);
    } else if (method == HttpRequest::HEAD) {
      ok = ok && !curl_easy_setopt(curl_, CURLOPT_NOBODY, true);
    } else {
      ok = ok && !curl_easy_setopt(
          curl_, CURLOPT_CUSTOMREQUEST, method.data());
      if (method == HttpRequest::PATCH) {
        ok = ok && !curl_easy_setopt(curl_, CURLOPT_UPLOAD, true);
      }
    }
    if (!ok) {
      return StatusInternalError("Error setting up http method options");
    }

    if (send_content_reader_) {
      ok = ok && !curl_easy_setopt(
          curl_, CURLOPT_READFUNCTION, RequestContentCallback);
      int64 num_bytes = send_content_reader_->TotalLengthIfKnown();
      if (num_bytes >= 0) {
        ok = ok && !curl_easy_setopt(curl_, CURLOPT_POSTFIELDSIZE, num_bytes);
      }
      if (!ok) {
        return StatusInternalError("Error setting up message options");
      }
    }

    // Note headers will be NULL if we didnt set any headers. That's ok.
    if (curl_easy_setopt(curl_, CURLOPT_HTTPHEADER, *curl_headers)) {
      return StatusInternalError("Error setting headers");
    }
    if (curl_easy_setopt(curl_, CURLOPT_URL, request->url().c_str())) {
      return StatusInternalError(StrCat("Error setting url=", request->url()));
    }

    int64 timeout_ms = request->options().timeout_ms();
    if (timeout_ms) {
      VLOG(1) << "Setting timeout to ms=" << timeout_ms;
      if (curl_easy_setopt(curl_, CURLOPT_TIMEOUT_MS, timeout_ms)) {
        return StatusInvalidArgument("Invalid timeout");
      }
    }

    return StatusOk();
  }

  // Restores options that may have been modified by PrepareRequestOptions.
  bool RestoreRequestOptions() {
    bool ok = true;

    // Http method related options
    ok = ok && !curl_easy_setopt(curl_, CURLOPT_HTTPGET, false);
    ok = ok && !curl_easy_setopt(curl_, CURLOPT_NOBODY, false);
    ok = ok && !curl_easy_setopt(curl_, CURLOPT_POST, false);
    ok = ok && !curl_easy_setopt(curl_, CURLOPT_PUT, false);
    ok = ok && !curl_easy_setopt(curl_, CURLOPT_UPLOAD, false);
    ok = ok && !curl_easy_setopt(curl_, CURLOPT_CUSTOMREQUEST, NULL);

    // Other options
    ok = ok && !curl_easy_setopt(curl_, CURLOPT_POSTFIELDSIZE, -1);
    ok = ok && !curl_easy_setopt(curl_, CURLOPT_READFUNCTION, NULL);
    ok = ok && !curl_easy_setopt(curl_, CURLOPT_HTTPHEADER, NULL);
    ok = ok && !curl_easy_setopt(curl_, CURLOPT_TIMEOUT_MS, 0L);

    return ok;
  }

  // TODO(user): this is just for the purpose of making the review
  // easier. Move this above to the public section.
 public:
  void PerformRequest(CurlHttpRequest* request, HttpResponse* response) {
    error_buffer_[0] = 0;
    http_code_ = 0;
    send_content_reader_ = request->content_reader();

    struct curl_slist* curl_headers = NULL;
    googleapis::util::Status status = PrepareRequestOptions(request, &curl_headers);
    HttpRequestState* state = request->mutable_state();

    if (status.ok()) {
      response_ = response;
      request_ = request;
      CURLcode code = curl_easy_perform(curl_);
      if (code) {
        status = StatusFromCurlCode(code, error_buffer_);
        state->set_transport_status(status);
      } else {
        VLOG(1) << "Got http_code="  << http_code_
                << " for " << request->http_method()
                << " url=" << request->url();
        // We havent yet bound the http_code_ into the response
        // so we cannot yet create the result status instance.
        // See below.
      }
      DataWriter* writer = response->body_writer();
      if (writer->size() == 0) {
        response->body_writer()->Begin();
      }
      response->body_writer()->End();
      response_ = NULL;
      request_ = NULL;
    }

    RestoreRequestOptions();

    if (curl_headers) {
      curl_slist_free_all(curl_headers);
    }

    // Set status at the very end in case this acts as a condition variable
    // on another thread waiting for the response to finish populating.
    if (http_code_ != 0) {
      state->set_http_code(http_code_);
      if (HttpStatusCode::IsRedirect(http_code_)) {
        // We're still outstanding.
        // The base class will handle redirects or finalization.
        return;
      } else {
        status = state->status();
      }
      // status is not necessarily ok because http_code might be bad,
      // but we arent going to log those since we already did.
    } else {
      // Status should be an error from the curl library before the
      // request was even sent. But let's double-check this in debug.
      DCHECK(!status.ok());
      LOG(ERROR) << "Call to url=" << request->url() <<
                 " failed with http status " << http_code_;
      state->set_transport_status(status);
    }
    return;
  }

 private:
  static bool FindHttpStatus(const StringPiece& header, int* http_code) {
    // We could use a regexp "^HTTP[^ ]* *(\\d+)" but dont want to minimize
    // external dependencies and this is simple enough to do by hand.
    const char kHttp[] = "HTTP/";
    if (!header.starts_with(kHttp)) {
      return false;
    }

    const char* cstr = header.data() + sizeof(kHttp);
    const char* end = header.data() + header.size();
    for (; cstr < end && *cstr != ' '; ++cstr) {}  // skip to space
    for (; cstr < end && *cstr == ' '; ++cstr) {}  // skip over spaces

    if (cstr == end) {
      return false;
    }

    const char* last_digit;
    for (last_digit = cstr;
         last_digit < end && ascii_isdigit(*last_digit);
         ++last_digit) {
    }
    return safe_strto32(StringPiece(cstr, last_digit - cstr), http_code);
  }

  static size_t ResultHeaderCallback(
      void* data, size_t size, size_t nmemb, void* instance) {
    CurlProcessor* processor = static_cast<CurlProcessor*>(instance);
    size_t data_len = size * nmemb;

    int http_code;
    StringPiece header(static_cast<const char*>(data), data_len);
    if (FindHttpStatus(header, &http_code)) {
      processor->http_code_ = http_code;
    } else {
      StripWhitespaceAndCntrl(&header);  // remove whitespace and trailing \r\n
      if (!header.empty()) {
        int colon = header.find(':');
        CHECK_NE(string::npos, colon) << "Header=[" << header << "]";
        StringPiece value = header.substr(colon + 1);
        StripWhitespace(&value);
        processor->response_->AddHeader(header.substr(0, colon).as_string(),
                                        value.as_string());
      }
    }

    return data_len;
  }

  static size_t ResultBodyCallback(
      void* data, size_t size, size_t nmemb, void* instance) {
    CurlProcessor* processor = static_cast<CurlProcessor*>(instance);
    size_t data_len = size * nmemb;
    DataWriter* writer = processor->response_->body_writer();
    if (writer->size() == 0 && data_len > 0) {
      writer->Begin();
    }
    googleapis::util::Status status = writer->Write(data_len, static_cast<char*>(data));
    if (status.ok()) {
      return data_len;
    }
    LOG(ERROR) << "Error handling HTTP response body data";
    return 0;
  }

  static size_t RequestContentCallback(
      void* target, size_t size, size_t nmemb, void* instance) {
    CurlProcessor* processor = static_cast<CurlProcessor*>(instance);
    char* output = static_cast<char*>(target);
    int64 bytes_to_read = size * nmemb;
    int64 read =
        processor->send_content_reader_->ReadToBuffer(bytes_to_read, output);
    if (processor->send_content_reader_->error()) {
      LOG(ERROR) << "Failed preparing content to send";
      return CURL_READFUNC_ABORT;
    }
    VLOG(8) << "RequestCallback(" << size << ", "
            << nmemb << ") read " << read;
    return read;
  }
};

CurlHttpRequest::CurlHttpRequest(
    const HttpMethod& method, CurlHttpTransport* transport)
    : HttpRequest(method, transport) {
}

CurlHttpRequest::~CurlHttpRequest() {
}

void CurlHttpRequest::DoExecute(HttpResponse* response) {
  CurlHttpTransport* curl_transport =
      static_cast<CurlHttpTransport*>(transport());
  CurlProcessor* processor = curl_transport->AcquireProcessor();
  processor->PerformRequest(this, response);
  curl_transport->ReleaseProcessor(processor);
}

// static
const char CurlHttpTransport::kTransportIdentifier[] = "Curl";

CurlHttpTransport::CurlHttpTransport(const HttpTransportOptions& options)
    : HttpTransport(options) {
  set_id(kTransportIdentifier);
}

CurlHttpTransport::~CurlHttpTransport() {
  for (CurlProcessor* processor : processors_)
    delete processor;
}

CurlProcessor* CurlHttpTransport::AcquireProcessor() {
  {
    MutexLock lock(&mutex_);
    if (!processors_.empty()) {
      CurlProcessor* processor = processors_.back();
      processors_.pop_back();
      return processor;
    }
  }
  return new CurlProcessor(this);
}

void CurlHttpTransport::ReleaseProcessor(CurlProcessor* processor) {
  MutexLock lock(&mutex_);
  processors_.push_back(processor);
}

HttpRequest* CurlHttpTransport::NewHttpRequest(
    const HttpRequest::HttpMethod& method) {
  if (InShutdown()) {
    LOG(ERROR) << "shutdown";
    return nullptr;
  }
  return new CurlHttpRequest(method, this);
}

CurlHttpTransportFactory::CurlHttpTransportFactory() {
  set_default_id(CurlHttpTransport::kTransportIdentifier);
}

CurlHttpTransportFactory::CurlHttpTransportFactory(
    const HttpTransportLayerConfig* config)
    : HttpTransportFactory(config) {
  set_default_id(CurlHttpTransport::kTransportIdentifier);
}

CurlHttpTransportFactory::~CurlHttpTransportFactory() {}

HttpTransport* CurlHttpTransportFactory::DoAlloc(
    const HttpTransportOptions& options) {
  HttpTransport* transport = NewCurlHttpTransport(options);
  transport->set_id(default_id());
  return transport;
}

// static
HttpTransport* CurlHttpTransportFactory::NewCurlHttpTransport(
    const HttpTransportOptions& options) {
  return new CurlHttpTransport(options);
}

}  // namespace client

}  // namespace googleapis
