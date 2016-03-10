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
 * @defgroup PlatformLayerUri Platform Layer - URI Support Module
 *
 * The URI Support module provides various classes and free
 * functions to support the use and application of URIs in general.
 */
#ifndef GOOGLEAPIS_UTIL_URI_UTILS_H_
#define GOOGLEAPIS_UTIL_URI_UTILS_H_

#include <string>
using std::string;
#include <utility>
#include <vector>

#include "googleapis/client/util/date_time.h"
namespace googleapis {

class StringPiece;

namespace  client {

/*
 * Parses a url as described in 2.4 of
 * <a href='http://www.ietf.org/rfc/rfc1808.txt'>RFC 1808</a>.
 * @ingroup PlatformLayerUri
 *
 * This is an extension of
 * <a href='http://www.ietf.org/rfc/rfc1838.txt'>RFC 1738</a>
 * that includes fragments.
 *
 * In RFC 1808 the URL is in the form:
 *   [scheme]://[netloc]/[path];[params]?[query]#[fragment]
 * Each of these parts is optional.
 *
 * In RFC 1738 the URL form is a function of the scheme, but for http it is
 *   http://[host]:[port]/[path]?[searchpart]
 * where here we:
 *   - treat [netloc] = [host]:[port]
 *   - allow [path] to have ';'[params]
 *   - subdivide [searchpart] into [query]'#'[fragment]
 *
 * Note that the [fragment] and '#' that proceeds it is not actually
 * part of the URL but is here because it is commonly used within the
 * same string context as a URL as specified in RFC 1808.
 */
class ParsedUrl {
 public:
  /*
   * Type used for getting individual query parameter bindings.
   * The values will be unescaped.
   */
  typedef std::pair<string, string> QueryParameterAssignment;

  /*
   * Construct the parsed url from a URL.
   */
  explicit ParsedUrl(const string& url);

  /*
   * Standard destructor.
   */
  ~ParsedUrl();

  /*
   * Returns the URL that was parsed.
   */
  const string& url() const      { return url_; }

  /*
   * Returns the URL's scheme.
   *
   * @return The scheme (e.g. 'https') or empty if none
   */
  string scheme() const   { return scheme_; }

  /*
   * Returns the URL's network location.
   *
   * @return The network location, including port if explicitly specified
   *         (e.g. 'www.googleapis.com'). This might be empty.
   */
  string netloc() const   { return netloc_; }

  /*
   * Returns the URL's path.
   *
   * @return The URL's path (e.g. '/drive/v2/files'). This might be empty.
   */
  string path() const     { return path_; }

  /*
   * Returns the URL's parameters.
   *
   * @return The URL's parameters are the content between the ';' and
   * query parameters or fragment or end of string. It may be empty.
   */
  string params() const   { return params_; }

  /*
   * Returns the URL's query string.
   *
   * @return The URL's query string is the URL content between the '?' and
   * fragment (or end of string if no fragment). It may be empty.
   */
  string query() const    { return query_; }

  /*
   * Returns the URL's fragment.
   *
   * @return The URL's fragment does not include the leading '#'. It may
   * be empty.
   */
  string fragment() const { return fragment_; }

  /*
   * Returns whether the URL was valid or not.
   */
  bool IsValid() const;

  /*
   * Returns the detailed assignments for individual query parameters.
   *
   * This method is not thread-safe for the first invocation.
   */
  const std::vector<
      QueryParameterAssignment>& GetQueryParameterAssignments() const;

  /*
   * Looks up value of query paraemter if it is there.
   *
   * @param[in] name The value to look for.
   * @param[out] value The [unescaped] value found.
   * @return true if parameter was present, false if not.
   */
  bool GetQueryParameter(const string& name, string* value) const;

  /*
   * Conditionally joins two strings for a URL segment.
   *
   * This is a helper function for joining together an attribute with its
   * separator.
   *
   * @param[in] join If true then join the strings, if false then return empty.
   * @param[in] a The prefix of the joined string.
   * @param[in] b The postfix of the joined string.
   *
   * @return  Either a + b or "".
   */
  static string SegmentOrEmpty(
      bool join, const string& a, const string& b) {
    if (!join) return "";
    string ret(a);
    ret.append(b);
    return ret;
  }

 private:
  string url_;
  string scheme_;    // does not include trailing ':'
  string netloc_;    // does not include leading '//'
  string path_;      // includes leading '/' if absolute
  string params_;    // does not include leading ';'
  string query_;     // does not include leading '?'
  string fragment_;  // does not include leading '#'

  // Constructed on first request, not thread-safe but not typically
  // used in a multi-threaded context and not typically used at all.
  mutable std::vector<QueryParameterAssignment> query_param_assignments_;

  // Indicates whether the url is valid or not.
  mutable bool valid_;
};

/*
 * Resolve a [possibly] relative url into an absolute url.
 * @ingroup PlatformLayerUri
 *
 * This function implements <a href='http://www.ietf.org/rfc/rfc1808.txt'>
 * RFC 1808 Relative Uniform Resource Locators</a> to resolve a url relative
 * to a base url. Note that the actual RFC handles URLs embedded into complex
 * documents. Here we only have a single base url.
 *
 * @param[in] base_url The base url.
 * @param[in] new_url The url to be resolved against the base.
 * @return The resolved URL or empty string on failure.
 */
string ResolveUrl(const string& base_url, const string& new_url);

/*
 * Join two fragments together into a path.
 * @ingroup PlatformLayerUri
 *
 * @param[in] a The first fragment may or may not have a trailing '/'.
 * @param[in] b The second fragment may or may not have a leading '/'.
 * @return The path "a/b" containing exactly one '/' between fragments a and b.
 */
string JoinPath(const StringPiece& a, const string& b);

/*
 * Escape a string so that it is valid in a URL.
 * @ingroup PlatformLayerUri
 *
 * @param[in] s The string to escape.
 * @return the escaped string.
 */
string EscapeForUrl(const string& s);
bool UnescapeFromUrl(const string& s, string* to);

/*
 * Escape a string according to URI Template reserved expansion rules.
 * @ingroup PlatformLayerUri
 *
 * @param[in] s The string to escape.
 * @return the escaped string.
 */
string EscapeForReservedExpansion(const string& s);

/*
 * Templated function that encodes a primitive C++ value for use in a URL.
 * @ingroup PlatformLayerUri
 *
 * @param[in] value The C++ value
 * @return The string encoding suitable for use in a URL.
 */
template<typename T> string CppValueToEscapedUrlValue(const T& value);

/*
 * Templated function that encodes a C++ STL container for use as in a URL.
 * @ingroup PlatformLayerUri
 *
 * The values in the array are those returned by the iterator.
 * This does not put a leading (or ending) separator so you should
 * form the uri with either a leading '&' or '?' to indicate that parameters
 * are following.
 *
 * @param[in] begin The start of the array sequence.
 * @param[in] end The end of the array sequence.
 * @param[in] param_name The name of the URL parameter being added.
 * @param[in,out] target The string to append the string-encoded value into.
 */
template<typename T>
void AppendIteratorToUrl(
    const T& begin, const T& end,
    const string& param_name, string *target);

/*
 * Implements append a scalar value into a URL.
 *
 * This function is intended for scalar types: [u]int(16|32|64)
 */
template<typename T>
inline string CppValueToEscapedUrlValue(const T& value) {
  return std::to_string(value);
}

/*
 * Implements append a character value into a URL.
 */
template<>
inline string CppValueToEscapedUrlValue<char>(const char& value) {
  return string(1, value);
}

/*
 * Implements append a boolean value into a URL.
 */
template<>
inline string CppValueToEscapedUrlValue<bool>(const bool& value) {
  return value ? "true" : "false";
}

/*
 * Implements append a float value into a URL.
 */
string SimpleFtoa(float value);
template<>
inline string CppValueToEscapedUrlValue<float>(const float& value) {
  return SimpleFtoa(value);
}

/*
 * Implements append a double value into a URL.
 */
string SimpleDtoa(double value);
template<>
inline string CppValueToEscapedUrlValue<double>(const double& value) {
  return SimpleDtoa(value);
}

/*
 * Implements append a string value into a URL.
 */
template<>
inline string CppValueToEscapedUrlValue<string>(const string& value) {
  return EscapeForUrl(value);
}

/*
 * Implements append a Date value into a URL.
 */
template<>
inline string CppValueToEscapedUrlValue<Date>(const Date& value) {
  return EscapeForUrl(value.ToYYYYMMDD());
}

/*
 * Implements append a DateTime value into a URL.
 */
template<>
inline string CppValueToEscapedUrlValue<DateTime>(const DateTime& value) {
  return EscapeForUrl(value.ToString());
}

/*
 * Implements default templated AppendIteratorToUrl.
 */
template<typename T>
void AppendIteratorToUrl(
    const T& begin, const T& end, const string& param_name,
    string* target) {
  const char* sep = "";
  for (T it = begin; it != end; ++it) {
    target->append(sep);
    target->append(param_name);
    target->append("=");
    target->append(CppValueToEscapedUrlValue(*it));
    sep = "&";
  }
}

}  // namespace client

}  // namespace googleapis
#endif  // GOOGLEAPIS_UTIL_URI_UTILS_H_
