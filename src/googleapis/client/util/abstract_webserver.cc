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


#include <string>
using std::string;
#include <utility>
using std::make_pair;
using std::pair;
#include <vector>
using std::vector;
#include "googleapis/client/util/abstract_webserver.h"
#include "googleapis/client/util/status.h"
#include "googleapis/client/util/uri_utils.h"
#include "googleapis/base/callback.h"
#include <glog/logging.h>
#include "googleapis/base/macros.h"
#include "googleapis/strings/strcat.h"
#include "googleapis/strings/stringpiece.h"
#include "googleapis/util/stl_util.h"

namespace googleapis {

namespace client {

util::Status WebServerResponse::SendRedirect(
     int http_code, const StringPiece& url) {
  googleapis::util::Status status = AddHeader("Location", url);
  if (status.ok()) {
    status = SendReply("", http_code, "");
  }
  return status;
}


WebServerRequest::WebServerRequest(
    const StringPiece& method,
    const StringPiece& url,
    WebServerResponse* response_storage)
    : method_(method.as_string()),
      parsed_url_(url),
      response_(response_storage) {
}

WebServerRequest::~WebServerRequest() {
}

AbstractWebServer::AbstractWebServer(int port) : port_(port) {
}

AbstractWebServer::~AbstractWebServer() {
  STLDeleteContainerPairSecondPointers(hooks_.begin(), hooks_.end());
}

util::Status AbstractWebServer::Startup() {
  return DoStartup();
}

void AbstractWebServer::Shutdown() {
  return DoShutdown();
}

string AbstractWebServer::url_protocol() const { return "https"; }

string AbstractWebServer::MakeEndpointUrl(
    bool use_localhost, const StringPiece& path) const {
string url = StrCat(url_protocol(), "://");
   if (use_localhost) {
     url.append("localhost");
   } else {
     char hostname[256];
     int result = gethostname(hostname, sizeof(hostname));
     hostname[sizeof(hostname) - 1] = 0;
     url.append(hostname);
   }
   StrAppend(&url, ":", port_, path);
   return url;
}

void AbstractWebServer::AddPathHandler(
    const string& path, PathHandler* handler) {
  CHECK(handler != NULL);
  handler->CheckIsRepeatable();
  hooks_.push_back(std::make_pair(path, handler));
}

AbstractWebServer::PathHandler* AbstractWebServer::FindPathHandler(
    WebServerRequest* request) const {
  for (vector<Hook>::const_iterator it = hooks_.begin();
       it != hooks_.end();
       ++it) {
    if (request->parsed_url().path().starts_with(it->first)) {
      return it->second;
    }
  }
  return NULL;
}

util::Status AbstractWebServer::DoHandleRequest(
     WebServerRequest* request) {
  PathHandler* handler = FindPathHandler(request);
  if (handler) {
    return handler->Run(request);
  } else {
    return request->response()->SendReply(
        "text/plain", 404,
        StrCat("NOT FOUND\n", request->parsed_url().path()));
  }
}

}  // namespace client

}  // namespace googleapis
