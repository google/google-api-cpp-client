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


#ifndef GOOGLEAPIS_UTIL_TASK_STATUS_H_
#define GOOGLEAPIS_UTIL_TASK_STATUS_H_

#include <string>
namespace googleapis {
using std::string;

namespace util {
namespace error {

/*
 * <table><tr><th>Code<th>Indented Purpose
 * <tr><td>OK         <td>Everything is fine; no error.
 * <tr><td>CANCELLED  <td>The operation has been cancelled.
 * <tr><td>UNKNOWN    <td>The cause of error is unknown.
 * <tr><td>INVALID_ARGUMENT <td>The operation received an invalid argument.
 * <tr><td>DEADLINE_EXCEEDED
 *     <td>The operation terminated early due to a deadline.
 * <tr><td>NOT_FOUND  <td>The requested resource or data element was missing.
 * <tr><td>ALREADY_EXISTS <td>The resource or data element already exists.
 * <tr><td>PERMISSION_DENIED
 *     <td>The caller has insufficient permission to perform the operation.
 * <tr><td>RESOURCE_EXHAUSTED
 *     <td>Not enough resources (usually memory, disk etc) to perform the
 *         operation.
 * <tr><td>FAILED_PRECONDITION
 *     <td>The caller did not meet the operation's requirements.
 * <tr><td>ABORTED      <td>The operation aborted prematurely for some reason.
 * <tr><td>OUT_OF_RANGE
 *     <td>The requested resource or data element is not valid. Usually this
 *         refers to some index or key.
 * <tr><td>UNIMPLEMENTED <td>The requestd operation is not fully implemented.
 * <tr><td>INTERNAL  <td>An error in the implementation was detected.
 * <tr><td>UNAVAILABLE
 *     <td>Some resource or data is not available to perform the operation now.
 * <tr><td>DATA_LOSS
 *     <td>The operation could not access all the data, or lost some along
 *         the way.
 * </table>
 */
enum Code {
  OK = 0,
  CANCELLED = 1,
  UNKNOWN = 2,
  INVALID_ARGUMENT = 3,
  DEADLINE_EXCEEDED = 4,
  NOT_FOUND = 5,
  ALREADY_EXISTS = 6,
  PERMISSION_DENIED = 7,
  RESOURCE_EXHAUSTED = 8,
  FAILED_PRECONDITION = 9,
  ABORTED = 10,
  OUT_OF_RANGE = 11,
  UNIMPLEMENTED = 12,
  INTERNAL = 13,
  UNAVAILABLE = 14,
  DATA_LOSS = 15,
};

/*
 * Smallest code value (inclusive).
 */
const Code Code_MIN = OK;

/*
 * Largest code value (inclusive).
 */
const Code Code_MAX = DATA_LOSS;

}  // namespace error

/*
 * Denotes whether a call or object is error free, and explains why if not.
 *
 * Status objects are used through the Google APIs Client Library for C++
 * to return and propagate errors rather than using C++ exceptions. They
 * are simple data objects that support copy and assignment so that they
 * can propagate across scopes and out of dynamic objects that may be
 * destroyed before the error is propagated.
 *
 * If the status is not ok() then the code() and error_message() will
 * indicate why.
 */
class Status {
 public:
  /*
   * Constructs a default OK status.
   */
  Status() : code_(googleapis::util::error::OK) {}

  /*
   * Constructs a status with the given code and message.
   * @param[in] code The status code for the instance.
   * @param[in] msg If the code is other than OK then this should not be empty.
   */
  Status(googleapis::util::error::Code code, const std::string& msg)
      : code_(code), msg_(msg) {}

  /*
   * Copy constructor.
   */
  Status(const Status& status) : code_(status.code_), msg_(status.msg_) {}

  /*
   * Standard destructor.
   */
  ~Status() {}

  /*
   * Assignment operator.
   */
  Status& operator =(const Status& status) {
    code_ = status.code_;
    msg_ = status.msg_;
    return *this;
  }

  /*
   * Equality operator.
   */
  bool operator ==(const Status& status) const {
    return code_ == status.code_ && msg_ == status.msg_;
  }

  /*
   * Determine if the status is ok().
   * @return true if the error code is OK, false otherwise.
   */
  bool ok() const { return code_ == googleapis::util::error::OK; }

  /*
   * Get explanation bound at construction.
   */
  const std::string& error_message() const  { return msg_; }

  /*
   * Get error_code bound at construction.
   */
  googleapis::util::error::Code error_code() const { return code_; }

  /*
   * Convert the status to a detailed string.
   *
   * If displaying the error to a user than error_message might be preferred
   * since it has less technical jargon.
   *
   * @see error_message()
   */
  std::string ToString() const;

  /*
   * This method is a NOP that confirms we are ignoring a status.
   */
  void IgnoreError() const {}

 private:
  googleapis::util::error::Code code_;
  std::string msg_;
};

}  // namespace util

}  // namespace googleapis
#endif  // GOOGLEAPIS_UTIL_TASK_STATUS_H_
