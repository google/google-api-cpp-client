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


#include <errno.h>
#include <string.h>

#include <string>
using std::string;
#include <utility>

#include "googleapis/client/util/status.h"

namespace googleapis {

namespace {

typedef std::pair< util::error::Code, string> CodeNamePair;

CodeNamePair HttpCodeToPair(int http_status) {
  util::error::Code code;
  const char* msg;

  switch (http_status) {
    case 400:
      code = util::error::INVALID_ARGUMENT;
      msg = "Bad Request";
      break;
    case 401:
      code = util::error::PERMISSION_DENIED;
      msg = "Unauthorized";
      break;
    case 402:
      code = util::error::UNKNOWN;
      msg = "Payment Required";
      break;
    case 403:
      code = util::error::PERMISSION_DENIED;
      msg = "Forbidden";
      break;
    case 404:
      code = util::error::NOT_FOUND;
      msg = "Not Found";
      break;
    case 405:
      code = util::error::UNIMPLEMENTED;
      msg = "Method Not Allowed";
      break;
    case 408:
      code = util::error::DEADLINE_EXCEEDED;
      msg = "Request Timeout";
      break;
    case 409:
      code = util::error::FAILED_PRECONDITION;
      msg = "Conflict";
      break;
    case 410:
      code = util::error::NOT_FOUND;
      msg = "Gone";
      break;
    case 411:
      code = util::error::INVALID_ARGUMENT;
      msg = "Length Required";
      break;
    case 412:
      code = util::error::FAILED_PRECONDITION;
      msg = "Precondition Failed";
      break;
    case 413:
      code = util::error::INVALID_ARGUMENT;
      msg = "Request Entity Too Large";
      break;
    case 414:
      code = util::error::INVALID_ARGUMENT;
      msg = "Request URI Too Long";
      break;
    case 415:
      code = util::error::INVALID_ARGUMENT;
      msg = "Unsupported Media Type";
      break;
    case 416:
      code = util::error::OUT_OF_RANGE;
      msg = "Requested Range Not Satisfiable";
      break;
    case 500:
      code = util::error::INTERNAL;
      msg = "Internal Server Error";
      break;
    case 501:
      code = util::error::UNIMPLEMENTED;
      msg = "Not Implemented";
      break;
    case 502:
      code = util::error::INTERNAL;
      msg = "Bad Gateway";
      break;
    case 503:
      code = util::error::UNAVAILABLE;
      msg = "Unavailable";
      break;
    case 504:
      code = util::error::DEADLINE_EXCEEDED;
      msg = "Gateway Timeout";
      break;
    case 505:
      code = util::error::UNIMPLEMENTED;
      msg = "HTTP Version Not Supported";
      break;
    case 507:
      code = util::error::RESOURCE_EXHAUSTED;
      msg = "Insufficient Storage";
      break;
    case 509:
      code = util::error::RESOURCE_EXHAUSTED;
      msg = "Bandwidth Limit Exceeded";
      break;
    default:
      if (http_status >= 200 && http_status < 300) {
        code = util::error::OK;
        msg = "OK";
      } else {
        code = util::error::UNKNOWN;
        msg = "Unknown";
      }
  }
  string result("Http(");
  result.append(std::to_string(http_status));
  result.append(") ");
  result.append(msg);
  return std::make_pair(code, result);
}

CodeNamePair ErrnoCodeToPair(int errno_code) {
#ifdef _MSC_VER
  char msg[128];
  strerror_s(msg, sizeof(msg), errno_code);
#else
  string msg = strerror(errno_code);
#endif

  switch (errno_code) {
    case 0:  // EOK
      return std::make_pair(util::error::OK, "OK");
    case EPERM:
      return std::make_pair(util::error::PERMISSION_DENIED, msg);
    case ENOENT:
      return std::make_pair(util::error::NOT_FOUND, msg);
    case EINVAL:
      return std::make_pair(util::error::INVALID_ARGUMENT, msg);
    case EEXIST:
      return std::make_pair(util::error::ALREADY_EXISTS, msg);
    case ERANGE:
      return std::make_pair(util::error::OUT_OF_RANGE, msg);
    case ENOMEM:
      return std::make_pair(util::error::RESOURCE_EXHAUSTED, msg);
    case EINTR:
      return std::make_pair(util::error::ABORTED, msg);
    case EIO:
      return std::make_pair(util::error::DATA_LOSS, msg);
    default:
      return std::make_pair(util::error::UNKNOWN, msg);
  }
}

}  // anonymous namespace

namespace client {

util::error::Code ErrnoCodeToStatusEnum(int errno_code) {
  return ErrnoCodeToPair(errno_code).first;
}

util::Status StatusFromErrno(int errno_code, const string& msg) {
  CodeNamePair values = ErrnoCodeToPair(errno_code);
  return googleapis::util::Status(values.first, msg.empty() ? values.second : msg);
}


util::error::Code HttpCodeToStatusEnum(int http_status) {
  return HttpCodeToPair(http_status).first;
}

const string HttpCodeToHttpErrorMessage(int http_status) {
  return HttpCodeToPair(http_status).second;
}

util::Status StatusFromHttp(int http_status, const string& msg) {
  CodeNamePair values = HttpCodeToPair(http_status);
  return googleapis::util::Status(values.first, msg.empty() ? values.second : msg);
}

}  // namespace client

}  // namespace googleapis
