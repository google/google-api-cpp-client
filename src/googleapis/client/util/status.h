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

/*
 * @defgroup PlatformLayer Platform Layer - General Programming Support
 *
 * These are generic utility classes and functions within the Platform Layer.
 */
#ifndef GOOGLEAPIS_UTIL_STATUS_H_
#define GOOGLEAPIS_UTIL_STATUS_H_

#include <string>
using std::string;

#include "googleapis/util/status.h"
namespace googleapis {

namespace client {


/*
 * Determine status error::Code to use from a standard Posix errno code.
 * @ingroup PlatformLayer
 *
 * This is more a suggestion than a definitive mapping.
 *
 * @param[in] errno_code A posix errno.
 * @return error code to use when creating a status for the Posix error.
 */
util::error::Code ErrnoCodeToStatusEnum(int errno_code);

/*
 * Create a status from a standard Posix errno code.
 * @ingroup PlatformLayer
 *
 * @param[in] errno_code A posix errno.
 * @param[in] msg A detailed message explanation can be empty to use a generic
 *            explanation based on the errno_code.
 * @return The status returned will be ok for errno_code 0, otherwise,
 *         it will be some form of failure.
 */
util::Status StatusFromErrno(int errno_code, const string& msg = "");

/*
 * Determine status error::Code to use from a standard HTTP response status
 * code.
 * @ingroup PlatformLayer
 *
 * This is more a suggestion than a definitive mapping.
 *
 * @param[in] http_code An HTTP response status code.
 * @return error code to use when creating a status for the HTTP status code.
 */
util::error::Code HttpCodeToStatusEnum(int http_code);

/*
 * Determine the standard HTTP error message for a given code.
 * @ingroup PlatformLayer
 *
 * @param[in] http_code An HTTP response status code.
 * @return short capitalized error phrase.
 */
const string HttpCodeToHttpErrorMessage(int http_code);

/*
 * Create a status from a standard HTTP response status code.
 * @ingroup PlatformLayer
 *
 * @param[in] http_code An HTTP status response code.
 * @param[in] msg A detailed message explanation can be empty to use a generic
 *            explanation based on the http_code.
 * @return The status returned will be ok for 2xx series responses, otherwise,
 *         it will be some form of failure.
 */
util::Status StatusFromHttp(int http_code, const string& msg = "");

/*
 * Shorthand notation for creating a status from a standard util::error enum
 * The symbol parameter is the symbolic enum name with the util::error
 * namespace stripped from it.
 */
#define STATUS_FROM_ENUM(symbol, msg) \
  googleapis::util::Status(util::error::symbol, msg)

/*
 * Creates a standard OK status.
 */
inline googleapis::util::Status  StatusOk() { return googleapis::util::Status(); }

/*
 * Creates a standard ABORTED status.
 */
inline googleapis::util::Status StatusAborted(const string& msg) {
  return STATUS_FROM_ENUM(ABORTED, msg);
}

/*
 * Creates a standard CANCELLED status.
 */
inline googleapis::util::Status StatusCanceled(const string& msg) {
  return STATUS_FROM_ENUM(CANCELLED, msg);
}

/*
 * Creates a standard DATA_LOSS status.
 */
inline googleapis::util::Status StatusDataLoss(const string& msg) {
  return STATUS_FROM_ENUM(DATA_LOSS, msg);
}

/*
 * Creates a standard DEADLINE_EXCEEDED status.
 */
inline googleapis::util::Status StatusDeadlineExceeded(const string& msg) {
  return STATUS_FROM_ENUM(DEADLINE_EXCEEDED, msg);
}

/*
 * Creates a standard INTERNAL status.
 */
inline googleapis::util::Status StatusInternalError(const string& msg) {
  return STATUS_FROM_ENUM(INTERNAL, msg);
}

/*
 * Creates a standard INVALID_ARGUMENT status.
 */
inline googleapis::util::Status StatusInvalidArgument(const string& msg) {
  return STATUS_FROM_ENUM(INVALID_ARGUMENT, msg);
}

/*
 * Creates a standard OUT_OF_RANGE status.
 */
inline googleapis::util::Status StatusOutOfRange(const string& msg) {
  return STATUS_FROM_ENUM(OUT_OF_RANGE, msg);
}

/*
 * Creates a standard PERMISSION_DENIED status.
 */
inline googleapis::util::Status StatusPermissionDenied(const string& msg) {
  return STATUS_FROM_ENUM(PERMISSION_DENIED, msg);
}

/*
 * Creates a standard UNIMPLEMENTED status.
 */
inline googleapis::util::Status StatusUnimplemented(const string& msg) {
  return STATUS_FROM_ENUM(UNIMPLEMENTED, msg);
}

/*
 * Creates a standard UNKNOWN status.
 */
inline googleapis::util::Status StatusUnknown(const string& msg) {
  return STATUS_FROM_ENUM(UNKNOWN, msg);
}

/*
 * Creates a standard RESOURCE_EXHAUSTED status.
 */
inline googleapis::util::Status StatusResourceExhausted(const string& msg) {
  return STATUS_FROM_ENUM(RESOURCE_EXHAUSTED, msg);
}

/*
 * Creates a standard FAILED_PRECONDITION status.
 */
inline googleapis::util::Status StatusFailedPrecondition(const string& msg) {
  return STATUS_FROM_ENUM(FAILED_PRECONDITION, msg);
}

}  // namespace client

}  // namespace googleapis
#endif  // GOOGLEAPIS_UTIL_STATUS_H_
