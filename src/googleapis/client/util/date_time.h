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


#ifndef GOOGLEAPIS_UTIL_DATE_TIME_H_
#define GOOGLEAPIS_UTIL_DATE_TIME_H_

#include <time.h>
#ifndef _MSC_VER
#include <sys/time.h>
#else
#include <stdlib.h>
#include "googleapis/base/windows_compatability.h"
#endif
#include <string>
using std::string;

#include "googleapis/base/integral_types.h"
#include "googleapis/base/port.h"
namespace googleapis {
#ifdef _MSC_VER
inline int localtime_r(const long* tv_secs, struct tm* out) {  // NOLINT
  time_t secs = *tv_secs;
  return _localtime64_s(out, &secs);
}

inline void gmtime_r(const long* secs, struct tm* out) {  // NOLINT
  time_t timer = *secs;
  gmtime_s(out, &timer);
}
#endif

namespace client {
class Date;


/*
 * Represents a date convertable among various standard
 * date represenations including RFC 3339 used by JSON encodings.
 * @ingroup PlatformLayer
 */
class DateTime {
 public:
  /*
   * Construct a date from a UTC struct tm.
   * @return An invalid date if the specified time is not valid.
   *
   * @see DateTimeFromLocal
   */
  static DateTime DateTimeFromUtc(const struct tm& utc);

  /*
   * Construct a date from a local-time struct tm.
   * @return An invalid date if the specified time is not valid.
   *
   * @see DateTimeFromUTC
   */
  static DateTime DateTimeFromLocal(const struct tm& local) {
    return DateTime(local);
  }

  /*
   * Construct a date with the current time.
   */
  DateTime();

  /*
   * Construct a date from an RFC 3339 formatted string.
   */
  explicit DateTime(const string& date);

  /*
   * Construct a date from an epoch time.
   */
  explicit DateTime(time_t time);

  /*
   * Construct a date from a timeval.
   *
   * @param[in] time Can contain fractional seconds.
   */
  explicit DateTime(const struct timeval& time);

  /*
   * Copy constructor.
   */
  DateTime(const DateTime& date);

  /*
   * Standard destructor.
   */
  ~DateTime();

  /*
   * Convert the date to an RFC 3339 encoded string.
   */
  string ToString() const;

  /*
   * Convert the date to local time.
   */
  void GetLocalTime(struct tm* out) const { localtime_r(&t_.tv_sec, out); }

  /*
   * Convert the date to universal time.
   */
  void GetUniversalTime(struct tm* out) const { gmtime_r(&t_.tv_sec, out); }

  void GetTimeval(struct timeval* timeval) const { *timeval = t_; }

  /*
   * Convert the date to epoch time.
   */
  time_t ToEpochTime() const { return t_.tv_sec; }

  /*
   * Determine if we have a valid date or not.
   */
  bool is_valid() const { return t_.tv_sec != kInvalidEpoch_; }

  /*
   * Determine relative ordering of this date relative to another.
   * @param[in]  date The date to compare against
   * @return < 0 if this date is earlier, > 0 if this date is later, or 0
   *          if the dates are equal.
   */
  int Compare(const DateTime& date) const {
    return t_.tv_sec == date.t_.tv_sec
        ? t_.tv_usec - date.t_.tv_usec
        : t_.tv_sec - date.t_.tv_sec;
  }

  /*
   * Determine if this date is equal to another.
   */
  bool operator ==(const DateTime& date) const {
    return Compare(date) == 0 && is_valid();
  }

  /*
   * Determine if this date is earlier than another.
   */
  bool operator <(const DateTime& date) const {
    return Compare(date) < 0 && is_valid();
  }

  /*
   * Determine if this date is later than another.
   */
  bool operator >(const DateTime& date) const {
    return Compare(date) > 0 && date.is_valid();
  }

  /*
   * Determine if this date is different from another.
   */
  bool operator !=(const DateTime& date) const {
    return Compare(date) != 0 && is_valid();
  }

  /*
   * Determine if this earlier or equal to another.
   */
  bool operator <=(const DateTime& date) const {
    return Compare(date) <= 0 && is_valid();
  }

  /*
   * Determine if this later or equal to another.
   */
  bool operator >=(const DateTime& date) const {
    return Compare(date) >= 0 && date.is_valid();
  }

  /*
   * Reasign this date to another.
   */
  DateTime& operator=(const DateTime& date) {
    t_ = date.t_;
    return *this;
  }

 protected:
  struct timeval t_;
  static const time_t kInvalidEpoch_;
  friend class Date;


  /*
   * Marks this date as being invalid.
   */
  void MarkInvalid();

  /*
   * Construct the data from a struct tm that is in local time.
   *
   * This constructir is exposed through the public factory methods
   * the struct tm is in.
   */
  explicit DateTime(const struct tm& local);
};

/*
 * Represents a simple date used by JSON encodings.
 * @ingroup PlatformLayer
 */
class Date {
 public:
  /*
   * Construct a Date from a DateTime instance.
   */
  explicit Date(const DateTime& date_time) : date_time_(date_time) {}

  /*
   * Construct a Date from a string in the form YYYY-MM-DD.
   */
  explicit Date(const string& yyyymmdd);

  /*
   * Copy constructor from another date.
   */
  Date(const Date& date) : date_time_(date.date_time_) {}

  /*
   * Default constructor for today.
   */
  Date() {}

  /*
   * Standard destructor.
   */
  ~Date() {}

  /*
   * Standard assignment operator.
   */
  Date& operator=(const Date& date) {
    date_time_ = date.date_time_;
    return *this;
  }

  /*
   * Convert date to YYYY-MM-DD string.
   */
  string ToYYYYMMDD() const;

 private:
  DateTime date_time_;
};

}  // namespace client

}  // namespace googleapis
#endif  // GOOGLEAPIS_UTIL_DATE_TIME_H_
