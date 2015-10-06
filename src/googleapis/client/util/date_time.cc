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


#include <limits.h>
#include <time.h>
#include <stdio.h>

#include <cstdio>
#include <string>
using std::string;

#if defined(ANDROID) && !defined(__LP64__)
// 32-bit Android has only timegm64() and not timegm().
#include <time64.h>
#define timegm timegm64
#endif

#include "googleapis/client/util/date_time.h"
#include <glog/logging.h>

namespace googleapis {

namespace {

#ifdef _MSC_VER
// Helper function for parsing time string where strptime isnt available.
// We're hardcoding the calls for what we expect to parse
// Params:
//    skip is a string literal we expect to see before the number (e.g. ':')
//    num_digits is the number of digits we expect to have
//          and the next character after num_digits should not be a digit.
//    *from is the string to parse, updated to point to the first unconsumed
//          character after we've parsed the component
//    value is the value that we've extracted
// Returns true if the string conforms to expectations, false otherwise.
bool ParseIntComponent(
    const char* skip, int num_digits, const char** from, int* value) {
  const char* input = *from;
  for (; *skip && *skip == *input; ++skip, ++input) {
  }
  if (*skip) return false;
  *value = 0;
  for (int i = 0; i < num_digits; ++i) {
    if (!isdigit(input[i])) return false;
    *value = 10 * *value + (input[i] - '0');
  }
  if (isdigit(input[num_digits])) return false;
  *from = input + num_digits;
  return true;
}

void SystemTimeToTimeval(
    const SYSTEMTIME& systime, struct timeval* t) {
  FILETIME filetime;
  SystemTimeToFileTime(&systime, &filetime);
  int64 low = filetime.dwLowDateTime;
  int64 high = filetime.dwHighDateTime;

  // SystemTime is in 100-nanos. So this is 1/10th of a microsecond.
  const int64 kMicrosPerSecond = 1000000;
  const int64 kNanos100PerMicro = 10;
  const int64 kWindowsEpochDifference = 11644473600LL;  // 1/1/1970 - 1/1/1601
  int64 nanos100SinceWindowsEpoch = (high << 32 | low);
  int64 microsSinceWindowsEpoch = nanos100SinceWindowsEpoch / kNanos100PerMicro;
  int64 secsSinceWindowsEpoch = microsSinceWindowsEpoch / kMicrosPerSecond;
  int64 secs = secsSinceWindowsEpoch - kWindowsEpochDifference;
  t->tv_sec = secs;
  t->tv_usec = microsSinceWindowsEpoch % kMicrosPerSecond;
}

void gettimeofday(struct timeval* t, void *ignore) {
  SYSTEMTIME systime;
  ::GetSystemTime(&systime);
  SystemTimeToTimeval(systime, t);
}

// tm is local time
time_t timegm(struct tm* tm) {
  SYSTEMTIME systime;
  systime.wYear = tm->tm_year + 1900;
  systime.wMonth = tm->tm_mon + 1;
  systime.wDayOfWeek = tm->tm_wday;
  systime.wDay = tm->tm_mday;
  systime.wHour = tm->tm_hour;
  systime.wMinute = tm->tm_min;
  systime.wSecond = tm->tm_sec;
  systime.wMilliseconds = 0;

  struct timeval t;
  SystemTimeToTimeval(systime, &t);
  return t.tv_sec;
}
#endif

inline struct timeval make_timeval(time_t sec, int usec) {
  struct timeval tv;
  tv.tv_sec = sec;
  tv.tv_usec = usec;
  return tv;
}

}  // anonymous namespace

namespace client {

#ifndef _WIN32
using std::snprintf;
#endif

static const struct timeval kInvalidTimeval_ =  make_timeval(-1, 0);
const time_t DateTime::kInvalidEpoch_ = -1;

DateTime DateTime::DateTimeFromUtc(const struct tm& utc) {
  struct tm copy = utc;  // timegm modifies the input!
  return DateTime(timegm(&copy));
}

DateTime::DateTime() {
  gettimeofday(&t_, NULL);
}

DateTime::DateTime(const DateTime& date) : t_(date.t_) {
}

DateTime::DateTime(time_t time) : t_(make_timeval(time, 0)) {
}

DateTime::DateTime(const struct timeval& tv) : t_(tv) {
}

DateTime::DateTime(const struct tm& local_tm) {
  time_t utc = mktime(const_cast<struct tm*>(&local_tm));
  t_ = make_timeval(utc, 0);
}

DateTime::DateTime(const string& date) {
  struct tm utc;
  memset(&utc, 0, sizeof(utc));

#ifndef _MSC_VER
  // strptime %z doesnt match 'Z' or HH:MM nor does it support
  // the fractional seconds that can appear in a RFC 3339 time
  // so we'll do the timezone part ourselves and just
  // match the date/time part.
  const char* format = "%Y-%m-%dT%H:%M:%S";
  const char* remaining = strptime(date.c_str(), format, &utc);
#else
  const char* remaining = date.c_str();
  if (ParseIntComponent("", 4, &remaining, &utc.tm_year)
      && ParseIntComponent("-", 2, &remaining, &utc.tm_mon)
      && ParseIntComponent("-", 2, &remaining, &utc.tm_mday)
      && ParseIntComponent("T", 2, &remaining, &utc.tm_hour)
      && ParseIntComponent(":", 2, &remaining, &utc.tm_min)
      && ParseIntComponent(":", 2, &remaining, &utc.tm_sec)) {
    utc.tm_year -= 1900;
    --utc.tm_mon;
  } else {
    remaining = NULL;
  }
#endif

  int usec = 0;
  if (remaining && *remaining == '.' && isdigit(remaining[1])) {
    int multiple = 1000000;
    for (++remaining; isdigit(*remaining); ++remaining) {
      multiple /= 10;
      usec = 10 * usec + *remaining - '0';
    }
    usec *= multiple;
    if (multiple == 0) {
      remaining = NULL;
    }
  }

  if (!remaining) {
    MarkInvalid();
  } else if (*remaining == 'Z') {
    if (remaining[1]) {
      MarkInvalid();
    } else {
      t_ = make_timeval(timegm(&utc), usec);
    }
  } else if (*remaining == '+' || *remaining == '-') {
    int hours = 0, mins = 0;
    if (sscanf(remaining + 1, "%02d:%02d", &hours, &mins) != 2
        || strlen(remaining + 1) != 5
        || mins < 0 || mins > 59 || hours < 0 || hours > 23) {
      MarkInvalid();
    } else {
      int factor = *remaining == '-' ? 1 : -1;
      time_t adjustment = ((hours * 60) + mins) * 60;
      time_t epoch = timegm(&utc);
      t_ = make_timeval(epoch + factor * adjustment, usec);
    }
  } else {
    MarkInvalid();
  }
  if (!is_valid()) {
    LOG(ERROR) << "Invalid date [" << date << "]";
  }
}


DateTime::~DateTime() {
}

void DateTime::MarkInvalid() {
  t_ = kInvalidTimeval_;
}

string DateTime::ToString() const {
  struct tm utc;
  string frac;
  char tmp[30];  // more than enough for "%04d-%02d-%02dT%02d:%02d:%02d%sZ",

  // add fraction as either millis or micros depending on resolution we need.
  int micros = t_.tv_usec;
  int millis = micros / 1000;
  if (millis * 1000 == micros) {
    snprintf(tmp, sizeof(tmp), ".%03d", millis);
    frac = tmp;
  } else {
    snprintf(tmp, sizeof(tmp), ".%06d", micros);
    frac = tmp;
  }

  gmtime_r(&t_.tv_sec, &utc);
  snprintf(tmp, sizeof(tmp), "%04d-%02d-%02dT%02d:%02d:%02d%sZ",
           utc.tm_year + 1900, utc.tm_mon + 1, utc.tm_mday,
           utc.tm_hour, utc.tm_min, utc.tm_sec, frac.c_str());
  return tmp;
}

Date::Date(const string& yyyymmdd) {
  struct tm local;
  memset(&local, 0, sizeof(local));

#ifndef _MSC_VER
  // strptime %z doesnt match 'Z' or HH:MM nor does it support
  // the fractional seconds that can appear in a RFC 3339 time
  // so we'll do the timezone part ourselves and just
  // match the date/time part.
  const char* format = "%Y-%m-%d";
  const char* remaining = strptime(yyyymmdd.c_str(), format, &local);
#else
  const char* remaining = yyyymmdd.c_str();
  if (ParseIntComponent("", 4, &remaining, &local.tm_year)
      && ParseIntComponent("-", 2, &remaining, &local.tm_mon)
      && ParseIntComponent("-", 2, &remaining, &local.tm_mday)) {
    local.tm_year -= 1900;
    --local.tm_mon;
    if (*remaining) {
      remaining = NULL;
    }
  } else {
    remaining = NULL;
  }
#endif
  if (!remaining) {
    date_time_.MarkInvalid();
    LOG(ERROR) << "Invalid date [" << yyyymmdd << "]";
  } else {
    date_time_ = DateTime(local);
  }
}

string Date::ToYYYYMMDD() const {
  struct tm local;
  date_time_.GetLocalTime(&local);
  char tmp[20];
  snprintf(tmp, sizeof(tmp), "%04d-%02d-%02d",
           local.tm_year + 1900, local.tm_mon + 1, local.tm_mday);
  return tmp;
}

}  // namespace client

}  // namespace googleapis
