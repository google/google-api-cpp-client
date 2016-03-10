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
#include <vector>
#include "samples/command_processor.h"
#include <gtest/gtest.h>

namespace googleapis {

using sample::CommandProcessor;

TEST(Test, SplitArgs) {
  std::vector<string> list;
  EXPECT_TRUE(CommandProcessor::SplitArgs("  a  b  ", &list));
  EXPECT_EQ(2, list.size());
  EXPECT_EQ("a", list[0]);
  EXPECT_EQ("b", list[1]);

  list.clear();
  EXPECT_TRUE(CommandProcessor::SplitArgs("  \"a  b\"  ", &list));
  EXPECT_EQ(1, list.size());
  EXPECT_EQ("a  b", list[0]);

  list.clear();
  EXPECT_TRUE(CommandProcessor::SplitArgs("a\\\"  b", &list));
  EXPECT_EQ(2, list.size());
  EXPECT_EQ("a\"", list[0]);
  EXPECT_EQ("b", list[1]);

  list.clear();
  EXPECT_TRUE(CommandProcessor::SplitArgs("\\  a\\ b\\\\ c", &list));
  EXPECT_EQ(3, list.size());
  EXPECT_EQ(" ", list[0]);
  EXPECT_EQ("a b\\", list[1]);
  EXPECT_EQ("c", list[2]);

  list.clear();
  EXPECT_FALSE(CommandProcessor::SplitArgs("\"a b", &list));
  EXPECT_EQ(1, list.size());
  EXPECT_EQ("a b", list[0]);

  list.clear();
  EXPECT_FALSE(CommandProcessor::SplitArgs("a b\\", &list));
  EXPECT_EQ(2, list.size());
  EXPECT_EQ("a", list[0]);
  EXPECT_EQ("b", list[1]);
}

}  // namespace googleapis
