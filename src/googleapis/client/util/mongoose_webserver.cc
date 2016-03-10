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
#include <string>
using std::string;
#include <vector>

#include "googleapis/client/util/mongoose_webserver.h"
#include "googleapis/client/util/uri_utils.h"
#include "googleapis/client/util/status.h"
#include <glog/logging.h>
#include "googleapis/strings/numbers.h"
#include "googleapis/strings/strcat.h"

namespace googleapis {

namespace client {

static inline string OriginalUri(const struct mg_request_info* request_info) {
  if (request_info->query_string && *request_info->query_string) {
    return StrCat(request_info->uri, "?", request_info->query_string);
  } else {
    return request_info->uri;
  }
}

class MongooseResponse : public WebServerResponse {
 public:
  explicit MongooseResponse(struct mg_connection* connection)
    : connection_(connection) {
  }

  virtual ~MongooseResponse() {}

  struct mg_connection* connection()  { return connection_; }

  virtual googleapis::util::Status SendReply(
      const string& content_type, int http_code, const string& payload) {
    const string& http_code_msg = HttpCodeToHttpErrorMessage(http_code);
    string headers =
        StrCat("HTTP/1.1 ", http_code, " ", http_code_msg, "\r\n");
    StrAppend(&headers,
              "Content-Type: ", content_type, "\r\n",
              "Content-Length: ", payload.size(), "\r\n");
    for (auto it = headers_.begin(); it != headers_.end(); ++it) {
      StrAppend(&headers, it->first, ": ", it->second, "\r\n");
    }

    for (std::vector<string>::const_iterator it = cookies_.begin();
         it != cookies_.end();
         ++it) {
      StrAppend(&headers, "Set-Cookie:", *it, "\r\n");
    }
    StrAppend(&headers, "\r\n");
    int wrote = mg_write(connection_, headers.c_str(), headers.size());
    if (wrote != headers.size()) {
      if (wrote == 0) return StatusAborted("Connection was closed");
      if (wrote < 0) return StatusUnknown("Error sending response");
      return StatusUnknown(
          StrCat("Only send ", wrote, " of ", headers.size()));
    }
    if (payload.size()) {
      wrote = mg_write(connection_, payload.data(), payload.size());
      if (wrote != headers.size()) {
        if (wrote == 0) return StatusAborted("Connection was closed");
        if (wrote < 0) return StatusUnknown("Error sending response");
        return StatusUnknown(
            StrCat("Only send ", wrote, " of ", headers.size()));
      }
    }
    return StatusOk();
  }

  virtual googleapis::util::Status AddHeader(const string& name, const string& value) {
    headers_.push_back(std::make_pair(name, value));
    return StatusOk();
  }

  virtual googleapis::util::Status AddCookie(const string& name, const string& value) {
    cookies_.push_back(StrCat(name, "=", value));
    return StatusOk();
  }

 private:
  struct mg_connection* connection_;
  std::vector<std::pair<string, string> > headers_;
  std::vector<string> cookies_;
};

// TODO(user): 20130626
// This doesnt read the post data, but there isnt really an
// interface to get at it anyway. The current interface is only
// intended to suppport OAuth 2.0 redirects, which GET a redirect.
class MongooseRequest : public WebServerRequest {
 public:
  MongooseRequest(const struct mg_request_info* request_info,
                  struct mg_connection* connection)
      : WebServerRequest(
            request_info->request_method,
            OriginalUri(request_info),
            new MongooseResponse(connection)),
        request_info_(request_info) {
  }

  virtual ~MongooseRequest() {}

  virtual bool GetCookieValue(const char* key, string* value) const {
    char local_storage[1 << 8];
    char* buffer = local_storage;
    MongooseResponse* webserver_response =
        static_cast<MongooseResponse*>(response());
    const char* cookies =
        mg_get_header(webserver_response->connection(), "Cookie");
    int result = mg_get_cookie(cookies, key, buffer, sizeof(local_storage));

    size_t size = 0;
    std::unique_ptr<char[]> heap_storage;

    if (result == -2) {
      heap_storage.reset(new char[size]);
      buffer = heap_storage.get();
      result = mg_get_cookie(cookies, key, buffer, size);
    }
    if (result >= 0) {
      value->assign(buffer, result);
      return true;
    } else if (result < 2) {
      LOG(ERROR)
          << "cookie " << key << " is bigger than " << size << " bytes.";
    }
    return false;
  }

  virtual bool GetHeaderValue(const char* key, string* value) const {
    for (int i = 0; i < request_info_->num_headers; ++i) {
      if (strcmp(key, request_info_->http_headers[i].name) == 0) {
        *value = request_info_->http_headers[i].value;
        return true;
      }
    }
    return false;
  }

 private:
  const struct mg_request_info* request_info_;
  DISALLOW_COPY_AND_ASSIGN(MongooseRequest);
};


const char MongooseWebServer::ACCESS_LOG_FILE[] = "access_log_file";
const char MongooseWebServer::DOCUMENT_ROOT[] = "document_root";
const char MongooseWebServer::ENABLE_KEEP_ALIVE[] = "enable_keep_alive";
const char MongooseWebServer::ERROR_LOG_FILE[] = "error_log_file";
const char MongooseWebServer::LISTENING_PORTS[] = "listening_ports";
const char MongooseWebServer::NUM_THREADS[] = "num_threads";
const char MongooseWebServer::REQUEST_TIMEOUT_MS[] = "request_timeout_ms";
const char MongooseWebServer::SSL_CERTIFICATE[] = "ssl_certificate";

int MongooseWebServer::BeginRequestHandler(struct mg_connection* connection) {
  const struct mg_request_info* request_info = mg_get_request_info(connection);
  MongooseWebServer* server =
      static_cast<MongooseWebServer*>(request_info->user_data);
  MongooseRequest request(request_info, connection);

  VLOG(1) << "Got " << request.parsed_url().url() << " " << request.method();
  googleapis::util::Status status = server->DoHandleRequest(&request);
  VLOG(1) << "Completed " << request.parsed_url().url();

  return status.ok();  // We sent a reply.
}

MongooseWebServer::MongooseWebServer(int port)
    : AbstractWebServer(port),
      kSslCertificateOption(SSL_CERTIFICATE),
      mg_context_(NULL) {
  memset(&callbacks_, 0, sizeof(callbacks_));
  callbacks_.begin_request = BeginRequestHandler;
}

MongooseWebServer::~MongooseWebServer() {
  if (mg_context_) {
    Shutdown();
  }
}

string MongooseWebServer::url_protocol() const {
  return use_ssl() ? "https" : "http";
}

util::Status MongooseWebServer::DoStartup() {
  string port_str = SimpleItoa(port());
  const string kPortOption = LISTENING_PORTS;
  string port_option = mongoose_option(kPortOption);
  if (!port_option.empty() && port_option != port_str) {
    return StatusFailedPrecondition("Inconsistent port and LISTENING_PORTS");
  }
  options_.insert(std::make_pair(kPortOption, port_str));

  if (!use_ssl()) {
    LOG(WARNING) << "Starting embedded MicroHttpd webserver without SSL";
  }
  std::unique_ptr<const char* []> options(
      new const char*[2 * options_.size() + 1]);
  const char** next_option_ptr = options.get();
  for (std::map<string, string>::const_iterator it = options_.begin();
       it != options_.end();
       ++it, next_option_ptr += 2) {
    next_option_ptr[0] = it->first.c_str();
    next_option_ptr[1] = it->second.c_str();
  }
  *next_option_ptr = NULL;
  mg_context_ = mg_start(&callbacks_, this, options.get());

  if (mg_context_) {
    return googleapis::util::Status();
  } else {
    return googleapis::util::Status(util::error::UNKNOWN, "Could not start Mongoose");
  }
}

void MongooseWebServer::DoShutdown() {
  if (mg_context_) {
    mg_stop(mg_context_);
    mg_context_ = NULL;
  }
}

}  // namespace client

}  // namespace googleapis
