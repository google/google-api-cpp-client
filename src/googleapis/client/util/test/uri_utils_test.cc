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
#include <vector>
#include "googleapis/client/util/uri_utils.h"
#include "googleapis/client/util/date_time.h"
#include "googleapis/strings/strcat.h"
#include <gtest/gtest.h>

namespace googleapis {

using client::CppValueToEscapedUrlValue;
using client::Date;

using client::DateTime;
using client::EscapeForReservedExpansion;
using client::EscapeForUrl;
using client::JoinPath;
using client::ParsedUrl;
using client::ResolveUrl;
using client::UnescapeFromUrl;

TEST(Test, TestParsedUrl) {
  const string scheme = "http";
  const string netloc = "www.google.com";
  const string abs_path = "/abs/b/c";
  const string rel_path = "relative/b/c";
  const string params = "parameters";
  const string query = "a=1&b=2";
  const string fragment = "fragment";

  ParsedUrl simple(StrCat(scheme, "://", netloc));
  EXPECT_EQ(scheme, simple.scheme());
  EXPECT_EQ(netloc, simple.netloc());
  EXPECT_EQ("", simple.path());
  EXPECT_EQ("", simple.params());
  EXPECT_EQ("", simple.query());
  EXPECT_EQ("", simple.fragment());

  ParsedUrl simple_with_fragment(StrCat(scheme, "://", netloc, "#", fragment));
  EXPECT_EQ("", simple_with_fragment.path());
  EXPECT_EQ("", simple_with_fragment.params());
  EXPECT_EQ("", simple_with_fragment.query());
  EXPECT_EQ(fragment, simple_with_fragment.fragment());

  ParsedUrl relative(StrCat(rel_path, "?", query));
  EXPECT_EQ("", relative.scheme());
  EXPECT_EQ("", relative.netloc());
  EXPECT_EQ(rel_path, relative.path());
  EXPECT_EQ("", relative.params());
  EXPECT_EQ(query, relative.query());
  EXPECT_EQ("", relative.fragment());

  ParsedUrl full(StrCat(scheme, "://", netloc,
                        abs_path, ";", params, "?", query, "#", fragment));
  EXPECT_EQ(scheme, full.scheme());
  EXPECT_EQ(netloc, full.netloc());
  EXPECT_EQ(abs_path, full.path());
  EXPECT_EQ(params, full.params());
  EXPECT_EQ(query, full.query());
  EXPECT_EQ(fragment, full.fragment());

  ParsedUrl no_path(StrCat(scheme, "://", netloc, "?", query));
  EXPECT_EQ(netloc, no_path.netloc());
  EXPECT_EQ("", no_path.path());
  EXPECT_EQ(query, no_path.query());
}

TEST(Test, TestParseQueryParameters) {
  const ParsedUrl::QueryParameterAssignment tests[] = {
      std::make_pair("A", "a"), std::make_pair("Number", "23"),
      std::make_pair("Escaped", "This&That=25%"), std::make_pair("Empty", ""),
  };

  string query;
  const char* sep = "";
  for (int i = 0; i < ARRAYSIZE(tests); ++i) {
    StrAppend(&query, sep, tests[i].first);
    if (!tests[i].second.empty()) {
      StrAppend(&query, "=", EscapeForUrl(tests[i].second));
      sep = "&";
    }
  }
  string url = StrCat("http://www.url.com/stuff?", query);
  ParsedUrl parsed(url);
  EXPECT_EQ(query, parsed.query());
  const std::vector<ParsedUrl::QueryParameterAssignment>& values =
      parsed.GetQueryParameterAssignments();
  EXPECT_TRUE(parsed.IsValid());
  EXPECT_EQ(values.size(), ARRAYSIZE(tests));
  for (int i = 0; i < values.size(); ++i) {
    EXPECT_EQ(tests[i].first, values[i].first);
    EXPECT_EQ(tests[i].second, values[i].second);
  }

  ParsedUrl no_params("http://www.google.com");
  EXPECT_EQ(0, no_params.GetQueryParameterAssignments().size());
}

TEST(Test, TestJoinPath) {
  EXPECT_EQ("/abs/path", JoinPath("", "/abs/path"));
  EXPECT_EQ("rel/path", JoinPath("", "rel/path"));

  EXPECT_EQ("BASE/abs/path", JoinPath("BASE", "/abs/path"));
  EXPECT_EQ("BASE/rel/path", JoinPath("BASE", "rel/path"));

  EXPECT_EQ("BASE/abs/path", JoinPath("BASE/", "/abs/path"));
  EXPECT_EQ("BASE/rel/path", JoinPath("BASE/", "rel/path"));

  EXPECT_EQ("BASE/", JoinPath("BASE/", ""));
  EXPECT_EQ("BASE", JoinPath("BASE", ""));
}

TEST(Test, TestEscapeForUrl) {
  const char kBinaryString[] = { 'B', 1, '1', 0 };

  std::pair<const char*, const char*> tests[] = {
      std::make_pair("simple", "simple"),
      std::make_pair("a long phrase", "a%20long%20phrase"),
      std::make_pair("!#$&'()*+,/:;=?@[]",
                     "%21%23%24%26%27%28%29%2A%2B%2C%2F%3A%3B%3D%3F%40%5B%5D"),
      std::make_pair(" 9:<=>{}~", "%209%3A<%3D>{}~"),
      std::make_pair(kBinaryString, "B%011"), std::make_pair("%", "%25"),
  };

  for (int i = 0; i < ARRAYSIZE(tests); ++i) {
    const char* kUnescaped = tests[i].first;
    const char* kEscaped = tests[i].second;
    EXPECT_EQ(kEscaped, EscapeForUrl(kUnescaped));

    string unescape;
    EXPECT_TRUE(UnescapeFromUrl(kEscaped, &unescape));
    EXPECT_EQ(kUnescaped, unescape);
  }

  string s;
  EXPECT_FALSE(UnescapeFromUrl("Invalid%", &s));
}

TEST(Test, TestEscapeForReservedExpansion) {
  EXPECT_EQ("path/to/Hello%20World?",
            EscapeForReservedExpansion("path/to/Hello World?"));
}

TEST(Test, TestValueToEscapedUrlValue) {
  EXPECT_EQ("a", CppValueToEscapedUrlValue('a'));
  EXPECT_EQ("a%20long%20phrase",
            CppValueToEscapedUrlValue(string("a long phrase")));
  EXPECT_EQ("-128", CppValueToEscapedUrlValue(static_cast<int8>(-128)));
  EXPECT_EQ("255", CppValueToEscapedUrlValue(static_cast<uint8>(255)));
  EXPECT_EQ("-32768", CppValueToEscapedUrlValue(static_cast<int16>(-32768)));
  EXPECT_EQ("65535", CppValueToEscapedUrlValue(static_cast<uint16>(65535)));
  EXPECT_EQ("-2147483648", CppValueToEscapedUrlValue(-2147483648));
  EXPECT_EQ("4294967295", CppValueToEscapedUrlValue(4294967295));
  EXPECT_EQ("-9223372036854775808",
            CppValueToEscapedUrlValue(
                static_cast<int64>(-GG_LONGLONG(0x8000000000000000))));
  EXPECT_EQ("18446744073709551615",
              CppValueToEscapedUrlValue(
                  static_cast<uint64>(GG_ULONGLONG(0xFFFFFFFFFFFFFFFF))));
  EXPECT_EQ("true", CppValueToEscapedUrlValue(true));
  EXPECT_EQ("3.1415", CppValueToEscapedUrlValue(3.1415f));
  EXPECT_EQ("3.14159265359", CppValueToEscapedUrlValue(3.14159265359));
  EXPECT_EQ("1998-09-04", CppValueToEscapedUrlValue(Date("1998-09-04")));
  EXPECT_EQ("1998-09-04T18%3A00%3A00.000Z",
            CppValueToEscapedUrlValue(DateTime("1998-09-04T10:00:00-08:00")));
}

TEST(Test, TestArrayIteratorExpansion) {
  const string kParamName = "test";
  string expect;
  std::vector<string> v;
  v.push_back("a=1");
  StrAppend(&expect, kParamName, "=", "a%3D1");

  v.push_back("b,?");
  StrAppend(&expect, "&", kParamName, "=", "b%2C%3F");

  v.push_back("&20");
  StrAppend(&expect, "&", kParamName, "=", "%2620");

  string target;
  client::AppendIteratorToUrl(
      v.begin(), v.end(), kParamName, &target);
  EXPECT_EQ(expect, target);
}


TEST(Test, TestResolveUrl) {
  // These tests are from.
  // section 5.1 in http://www.ietf.org/rfc/rfc1808.txt
  string original_url = "http://a/b/c/d;p?q#f";

  std::pair<string, string> tests[] = {
      std::make_pair("g:h", "g:h"),
      std::make_pair("g", "http://a/b/c/g"),
      std::make_pair("./g", "http://a/b/c/g"),
      std::make_pair("g/", "http://a/b/c/g/"),
      std::make_pair("/g", "http://a/g"),
      std::make_pair("//g", "http://g"),
      std::make_pair("?y", "http://a/b/c/d;p?y"),
      std::make_pair("g?y", "http://a/b/c/g?y"),
      std::make_pair("g?y/./x", "http://a/b/c/g?y/./x"),
      std::make_pair("#s", "http://a/b/c/d;p?q#s"),
      std::make_pair("g#s", "http://a/b/c/g#s"),
      std::make_pair("g#s/./x", "http://a/b/c/g#s/./x"),
      std::make_pair("g?y#s", "http://a/b/c/g?y#s"),
      std::make_pair(";x", "http://a/b/c/d;x"),
      std::make_pair("g;x", "http://a/b/c/g;x"),
      std::make_pair("g;x?y#s", "http://a/b/c/g;x?y#s"),
      std::make_pair(".", "http://a/b/c/"),
      std::make_pair("./", "http://a/b/c/"),
      std::make_pair("..", "http://a/b/"),
      std::make_pair("../", "http://a/b/"),
      std::make_pair("../g", "http://a/b/g"),
      std::make_pair("../..", "http://a/"),
      std::make_pair("../../", "http://a/"),
      std::make_pair("../../g", "http://a/g"),
  };

  for (int i = 0; i < arraysize(tests); ++i) {
    EXPECT_EQ(tests[i].second, ResolveUrl(original_url, tests[i].first))
        << "url=" << tests[i].first;
  }
}

}  // namespace googleapis
