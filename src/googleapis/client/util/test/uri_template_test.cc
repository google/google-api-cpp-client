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

#include <memory>
#include <string>
using std::string;

#include "googleapis/client/util/uri_template.h"
#include "googleapis/client/util/status.h"
#include "googleapis/base/callback.h"
#include <glog/logging.h>
#include <gtest/gtest.h>

namespace googleapis {

using client::StatusOk;
using client::StatusUnknown;
using client::UriTemplate;
using client::UriTemplateConfig;

class UriTemplateTestFixture : public testing::Test {
 public:
  UriTemplateTestFixture() {
    provider_.reset(
        NewPermanentCallback(this, &UriTemplateTestFixture::UriTemplateHelper));
  }

  googleapis::util::Status UriTemplateHelper(
      const string& name, const UriTemplateConfig& config, string* out) {
    if (name == "var") {
      out->append("value");
      return StatusOk();
    }
    if (name == "ivar") {
      int v = 42;
      UriTemplate::AppendValue(v, config, out);
      return StatusOk();
    }
    if (name == "varwithslash") {
      string v("i/am a/path");
      UriTemplate::AppendValue(v, config, out);
      return StatusOk();
    }
    if (name == "list") {
      UriTemplate::AppendListFirst("red", config, out);
      UriTemplate::AppendListNext("green", config, out);
      UriTemplate::AppendListNext("blue", config, out);
      return StatusOk();
    }

    if (name == "map") {
      UriTemplate::AppendMapFirst("semi", ";", config, out);
      UriTemplate::AppendMapNext("dot", ".", config, out);
      UriTemplate::AppendMapNext("comma", ",", config, out);
      return StatusOk();
    }

    return StatusUnknown("Testing failure");
  }

 protected:
  std::unique_ptr<UriTemplate::AppendVariableCallback> provider_;

  string Expand(const string uri) {
    string result;
    UriTemplate::Expand(uri, provider_.get(), &result).IgnoreError();
    return result;
  }
};

TEST_F(UriTemplateTestFixture, TestSimpleExpansion) {
  EXPECT_EQ("value", Expand("{var}"));
  EXPECT_EQ("42", Expand("{ivar}"));
  EXPECT_EQ("i%2Fam%20a%2Fpath", Expand("{varwithslash}"));
  EXPECT_EQ("red,green,blue", Expand("{list}"));
  EXPECT_EQ("red,green,blue", Expand("{list*}"));

  EXPECT_EQ("semi,%3B,dot,.,comma,%2C", Expand("{map}"));
  EXPECT_EQ("semi=%3B,dot=.,comma=%2C", Expand("{map*}"));
}

TEST_F(UriTemplateTestFixture, TestReservedExpansion) {
  EXPECT_EQ("value", Expand("{+var}"));
  EXPECT_EQ("42", Expand("{+ivar}"));
  EXPECT_EQ("i/am%20a/path", Expand("{+varwithslash}"));

  EXPECT_EQ("red,green,blue", Expand("{+list}"));
  EXPECT_EQ("red,green,blue", Expand("{+list*}"));

  EXPECT_EQ("semi,;,dot,.,comma,,", Expand("{+map}"));
  EXPECT_EQ("semi=;,dot=.,comma=,", Expand("{+map*}"));
}

TEST_F(UriTemplateTestFixture, TestFragmentExpansion) {
  EXPECT_EQ("#red,green,blue", Expand("{#list}"));
  EXPECT_EQ("#red,green,blue", Expand("{#list*}"));

  EXPECT_EQ("#semi,;,dot,.,comma,,", Expand("{#map}"));
  EXPECT_EQ("#semi=;,dot=.,comma=,", Expand("{#map*}"));
}

TEST_F(UriTemplateTestFixture, TestLabelExpansion) {
  EXPECT_EQ("X.red,green,blue", Expand("X{.list}"));
  EXPECT_EQ("X.red.green.blue", Expand("X{.list*}"));

  EXPECT_EQ("X.semi,%3B,dot,.,comma,%2C", Expand("X{.map}"));
  EXPECT_EQ("X.semi=%3B.dot=..comma=%2C", Expand("X{.map*}"));
}

TEST_F(UriTemplateTestFixture, TestPathSegmentExpansion) {
  EXPECT_EQ("/red,green,blue", Expand("{/list}"));
  EXPECT_EQ("/red/green/blue", Expand("{/list*}"));

  EXPECT_EQ("/semi,%3B,dot,.,comma,%2C", Expand("{/map}"));
  EXPECT_EQ("/semi=%3B/dot=./comma=%2C", Expand("{/map*}"));
}

TEST_F(UriTemplateTestFixture, TestPathSegmentParameterExpansion) {
  EXPECT_EQ(";list=red,green,blue", Expand("{;list}"));
  EXPECT_EQ(";list=red;list=green;list=blue", Expand("{;list*}"));

  EXPECT_EQ(";map=semi,%3B,dot,.,comma,%2C", Expand("{;map}"));
  EXPECT_EQ(";semi=%3B;dot=.;comma=%2C", Expand("{;map*}"));
}

TEST_F(UriTemplateTestFixture, TestFormStyleQueryExpansion) {
  EXPECT_EQ("?list=red,green,blue", Expand("{?list}"));
  EXPECT_EQ("?list=red&list=green&list=blue", Expand("{?list*}"));

  EXPECT_EQ("?map=semi,%3B,dot,.,comma,%2C", Expand("{?map}"));
  EXPECT_EQ("?semi=%3B&dot=.&comma=%2C", Expand("{?map*}"));
}

TEST_F(UriTemplateTestFixture, TestFormStyleQueryContinuation) {
  EXPECT_EQ("&list=red,green,blue", Expand("{&list}"));
  EXPECT_EQ("&list=red&list=green&list=blue", Expand("{&list*}"));

  EXPECT_EQ("&map=semi,%3B,dot,.,comma,%2C", Expand("{&map}"));
  EXPECT_EQ("&semi=%3B&dot=.&comma=%2C", Expand("{&map*}"));
}

TEST_F(UriTemplateTestFixture, TestEmbeddedValue) {
  EXPECT_EQ("XvalueY", Expand("X{var}Y"));
}

}  // namespace googleapis
