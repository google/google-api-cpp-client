#ifndef GOOGLEAPIS_UTIL_STATUS_TEST_UTIL_H__
#define GOOGLEAPIS_UTIL_STATUS_TEST_UTIL_H__

// #include "testing/base/public/gunit.h"
#include "googleapis/util/status.h"

// Macros for testing the results of functions that return util::Status.

#define EXPECT_OK(statement) EXPECT_EQ(googleapis::util::error::OK, (statement))
#define ASSERT_OK(statement) ASSERT_EQ(googleapis::util::error::OK, (statement))

#endif  // GOOGLEAPIS_UTIL_STATUS_TEST_UTIL_H__
