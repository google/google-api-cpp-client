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


#include <time.h>
#include <string>
using std::string;

#include "googleapis/client/util/date_time.h"
#include <glog/logging.h>
#include <gtest/gtest.h>

namespace googleapis {

using client::DateTime;

class DateTimeTestFixture : public testing::Test {
 protected:
  void TweakField(const struct tm& from,
                  struct tm* to,
                  int* adjust) {
    to->tm_year = from.tm_year - 1;
    to->tm_mon = from.tm_mon - 1;
    to->tm_mday = from.tm_mday - 1;
    to->tm_hour = from.tm_hour - 1;
    to->tm_min = from.tm_min - 1;
    to->tm_sec = from.tm_sec - 1;

#define MAYBE_TWEAK_AND_RETURN(FIELD) \
    if (adjust == &to->FIELD) {       \
      to->FIELD = from.FIELD + 1;     \
      return;                         \
    }                                 \
    to->FIELD = from.FIELD

    MAYBE_TWEAK_AND_RETURN(tm_year);
    MAYBE_TWEAK_AND_RETURN(tm_mon);
    MAYBE_TWEAK_AND_RETURN(tm_mday);
    MAYBE_TWEAK_AND_RETURN(tm_hour);
    MAYBE_TWEAK_AND_RETURN(tm_min);
    MAYBE_TWEAK_AND_RETURN(tm_sec);
#undef MAYBE_TWEAK_AND_RETURN
  }
};

TEST_F(DateTimeTestFixture, TestConstructor) {
  time_t epoch = time(0);
  DateTime date_now;
  time_t now_epoch = date_now.ToEpochTime();
  time_t diff = now_epoch - epoch;
  if (diff != 1) {  // clock may have crossed second threshold
    EXPECT_EQ(0, diff);
  }

  DateTime date_from_string(date_now.ToString());
  EXPECT_EQ(date_from_string.ToEpochTime(), date_now.ToEpochTime());

  struct tm utc;
  date_now.GetUniversalTime(&utc);
  struct tm local;
  date_now.GetLocalTime(&local);

  DateTime date_local = DateTime::DateTimeFromLocal(local);
  EXPECT_EQ(now_epoch, date_local.ToEpochTime());

  DateTime date_utc = DateTime::DateTimeFromUtc(utc);
  EXPECT_EQ(now_epoch, date_utc.ToEpochTime());

  string zulu = "2012-01-02T03:04:05.000Z";
  DateTime date_zulu(zulu);
  EXPECT_TRUE(date_zulu.is_valid());
  EXPECT_EQ(zulu, date_zulu.ToString());

  string plus_530 = "2012-01-02T08:34:05+05:30";
  DateTime date_plus_530(plus_530);
  EXPECT_TRUE(date_plus_530.is_valid());

  // Time is converted back into UTC
  EXPECT_EQ(0, date_zulu.ToEpochTime() - date_plus_530.ToEpochTime());
  EXPECT_EQ(zulu, date_plus_530.ToString());

  string zulu_frac = "2012-01-02T03:04:05.67Z";
  string zulu_frac_millis = "2012-01-02T03:04:05.670Z";

  struct timeval tv_ms;
  tv_ms.tv_sec = date_zulu.ToEpochTime();
  tv_ms.tv_usec = 67 * 10000;
  DateTime date_tv_ms(tv_ms);
  EXPECT_TRUE(date_tv_ms.is_valid());
  EXPECT_EQ(zulu_frac_millis, date_tv_ms.ToString());

  DateTime date_zulu_frac(zulu_frac);
  EXPECT_TRUE(date_zulu_frac.is_valid());
  EXPECT_TRUE(date_zulu != date_zulu_frac);
  EXPECT_TRUE(date_tv_ms == date_zulu_frac);
  EXPECT_EQ(zulu_frac_millis, date_zulu_frac.ToString());
  struct timeval tv;
  date_zulu_frac.GetTimeval(&tv);
  EXPECT_EQ(tv.tv_sec, date_zulu.ToEpochTime());
  EXPECT_EQ(tv.tv_usec, tv_ms.tv_usec);

  struct timeval tv_us = tv_ms;
  ++tv_us.tv_usec;
  DateTime date_tv_us(tv_us);
  EXPECT_TRUE(date_tv_us.is_valid());
  EXPECT_EQ("2012-01-02T03:04:05.670001Z", date_tv_us.ToString());

  string time_offset = "2012-01-02T03:04:05.6-08:09";
  string converted_time_offset = "2012-01-02T11:13:05.600Z";
  DateTime date_time_offset(time_offset);
  EXPECT_TRUE(date_time_offset.is_valid());
  EXPECT_EQ(date_zulu.ToEpochTime() + (8 * 60 + 9) * 60,
            date_time_offset.ToEpochTime());
  EXPECT_TRUE(date_time_offset > date_zulu);
  EXPECT_EQ(converted_time_offset, date_time_offset.ToString());
}

TEST_F(DateTimeTestFixture, TestInvalid) {
  string bad_tz = "2011-02-29T03:04:05+01";
  DateTime date_bad_tz(bad_tz);
  EXPECT_FALSE(date_bad_tz.is_valid());

  string extra_zulu = "2011-01-01T00:00:00Z+01:01";
  DateTime date_extra_zulu(extra_zulu);
  EXPECT_FALSE(date_extra_zulu.is_valid());
}

TEST_F(DateTimeTestFixture, TestCompare) {
  struct tm now;  // really 10/10/2010
  memset(&now, 0, sizeof(now));

  now.tm_year = 110;
  now.tm_mon = 10;
  now.tm_mday = 10;
  now.tm_hour = 10;
  now.tm_min = 10;
  now.tm_sec = 10;
  DateTime date_now = DateTime::DateTimeFromLocal(now);

  EXPECT_EQ(0, date_now.Compare(date_now));
  EXPECT_FALSE(date_now < date_now);
  EXPECT_FALSE(date_now > date_now);
  EXPECT_FALSE(date_now != date_now);
  EXPECT_TRUE(date_now == date_now);
  EXPECT_TRUE(date_now <= date_now);

  for (int test = 0; test < 6; ++test) {
    struct tm later;
    switch (test) {
      case 0:
        TweakField(now, &later, &later.tm_year);
        break;
      case 1:
        TweakField(now, &later, &later.tm_mon);
        break;
      case 2:
        TweakField(now, &later, &later.tm_mday);
        break;
      case 3:
        TweakField(now, &later, &later.tm_hour);
        break;
      case 4:
        TweakField(now, &later, &later.tm_min);
        break;
      case 5:
        TweakField(now, &later, &later.tm_sec);
        break;
      default:
        LOG(FATAL) << "Oops";
    }
    DateTime date_later = DateTime::DateTimeFromLocal(later);
    EXPECT_GT(0, date_now.Compare(date_later)) << "test=" << test;
    EXPECT_LT(0, date_later.Compare(date_now)) << "test=" << test;


    EXPECT_TRUE(date_now <= date_later) << "test=" << test;
    EXPECT_TRUE(date_now < date_later) << "test=" << test;
    EXPECT_TRUE(date_later > date_now) << "test=" << test;
    EXPECT_TRUE(date_later >= date_now) << "test=" << test;
  }
}

}  // namespace googleapis
