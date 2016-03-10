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
#include "googleapis/client/util/uri_utils.h"
#include <glog/logging.h>
#include "googleapis/base/macros.h"
#include "googleapis/strings/ascii_ctype.h"
#include "googleapis/strings/numbers.h"
#include "googleapis/strings/stringpiece.h"

namespace googleapis {


namespace client {

inline bool EndsWith(string s, const char *x) {
  int l = strlen(x);
  if (s.size() < l) return false;
  return s.compare(s.size()-l, l, x) == 0;
}

ParsedUrl::ParsedUrl(const string& url)
    : url_(url), path_(url_), valid_(true) {
  // Section 2.4.1
  int hash = path_.find('#');
  if (hash != string::npos && hash != path_.size() - 1) {
    fragment_ = path_.substr(hash + 1);  // Do not include leading '#'.
    path_.resize(hash);  // End before '#'.
  }

  // Section 2.4.2
  int colon = path_.find(':');
  if (colon != string::npos) {
    scheme_ = path_.substr(0, colon);  // Do not include ':' in the scheme.
    path_ = path_.substr(colon + 1);  // Start path after ':'.
  }

  // Section 2.4.3
  if (path_.compare(0, 2, "//") == 0) {
    int slash = path_.find('/', 3);
    // The spec just mentions the slash (for the path)
    // but we'll look for the other component separators as well
    // so we can handle things like scheme:://netloc?query.
    // We'll pretend this is the slash to get by here. The path
    // parsing later will figure out the path is empty.
    if (slash == string::npos) {
      int sym =  path_.find(';', 3);
      if (sym == string::npos) {
        sym = path_.find('?', 3);
      }
      if (sym == string::npos) {
        sym = path_.find('#', 3);
      }
      slash = sym;
    }
    if (slash == string::npos) {
      netloc_ = path_.substr(2);  // Strip leading '//'.
      path_.clear();  // Path was empty.
    } else {
      netloc_ = path_.substr(2, slash - 2);  // Strip leading '//'.
      path_ = path_.substr(slash);  // Start with leading '/'
    }
  }

  // Section 2.4.4
  int question = path_.find('?');
  if (question != string::npos) {
    query_ = path_.substr(question + 1);  // Strip leading '?'.
    path_.resize(question);  // End before '?'.
  }

  // Section 2.4.5
  int semi = path_.find(';');
  if (semi != string::npos) {
    params_ = path_.substr(semi + 1);  // Strip leading ';'.
    path_.resize(semi);  // End before ';'.
  }
}

ParsedUrl::~ParsedUrl() {
}

bool
ParsedUrl::GetQueryParameter(const string& name, string* value) const {
  const std::vector<ParsedUrl::QueryParameterAssignment>& list =
      GetQueryParameterAssignments();
  for (std::vector<ParsedUrl::QueryParameterAssignment>::const_iterator
           it = list.begin();
       it != list.end();
       ++it) {
    if (it->first == name) {
      *value = it->second;
      return true;
    }
  }
  value->clear();
  return false;
}

bool ParsedUrl::IsValid() const {
  if (valid_) {
    // Force lazy initialization to confirm.
    GetQueryParameterAssignments();
  }
  return valid_;
}

const std::vector<ParsedUrl::QueryParameterAssignment>&
ParsedUrl::GetQueryParameterAssignments() const {
  if (!query_param_assignments_.empty() || query_.empty()) {
    return query_param_assignments_;
  }

  std::vector<string> parts;
  size_t last = 0;
  for (size_t i = 0; i < query_.size(); ++i) {
    if (query_[i] == '&') {
      parts.push_back(query_.substr(last, i-last));
      last = i+1;
    }
  }
  if (last < query_.size()) {
    parts.push_back(query_.substr(last));
  }
  for (int i = 0; i < parts.size(); ++i) {
    int offset = parts[i].find('=');

    // Note that query_param_assignments_ is mutable.
    if (offset == string::npos) {
      query_param_assignments_.push_back(std::make_pair(parts[i], ""));
    } else {
      string unescaped;
      if (!UnescapeFromUrl(parts[i].substr(offset + 1), &unescaped)) {
        valid_ = false;
      }
      query_param_assignments_.push_back(
          std::make_pair(parts[i].substr(0, offset), unescaped));
    }
  }
  return query_param_assignments_;
}

string JoinPath(const StringPiece& base, const string& path) {
  if (base.empty()) return path;
  if (path.empty()) return base.as_string();

  bool base_has_slash = base[base.size() - 1] == '/';
  bool path_has_slash = path[0] == '/';

  string ret(base.as_string());
  if (base_has_slash != path_has_slash) {
    ret.append(path);
  } else if (path_has_slash) {
    ret.append(path.substr(1));
    return ret;
  } else {
    ret.append("/");
    ret.append(path);
  }
  return ret;
}

// http://en.wikipedia.org/wiki/Percent-encoding
// Reserved ascii chars from RFC 3986
#define kMinReserved '!'
#define kMaxReserved ']'
#if ((kMaxReserved - kMinReserved) > 64)
#error "This wont work"
#endif
#define RESERVE_BIT(c) (static_cast<uint64>(1) << (c - kMinReserved))
static const uint64 kReservedMask =
      RESERVE_BIT('%')
      | RESERVE_BIT('!') | RESERVE_BIT('*') | RESERVE_BIT('\'')
      | RESERVE_BIT('(') | RESERVE_BIT(')') | RESERVE_BIT(';')
      | RESERVE_BIT(':') | RESERVE_BIT('@') | RESERVE_BIT('&')
      | RESERVE_BIT('=') | RESERVE_BIT('+') | RESERVE_BIT('$')
      | RESERVE_BIT(',') | RESERVE_BIT('/') | RESERVE_BIT('?')
      | RESERVE_BIT('#') | RESERVE_BIT('[') | RESERVE_BIT(']');

static bool NeedsEscaping(char c) {
  // If not in the special range then only needs encoding if it isnt
  // graphical (i.e. binary)
  if (c < kMinReserved || c > kMaxReserved) return !ascii_isgraph(c);

  // Otherwise check the value against our mask.
  return (kReservedMask & 1LL << (c - kMinReserved)) != 0;
}

static bool IsNotGraphic(char c) {
  return !ascii_isgraph(c);
}

static string EscapeReservedCharacters(const string& from,
                                       bool (*needs_escaping)(char)) {
  const char *hex_digits = "0123456789ABCDEF";
  string escaped;
  const char* ptr = from.data();
  const char* end = ptr + from.size();
  for (; ptr < end; ++ptr) {
    if (needs_escaping(*ptr)) {
      escaped.push_back('%');
      escaped.push_back(hex_digits[(*ptr >> 4) & 0xf]);
      escaped.push_back(hex_digits[(*ptr) & 0xf]);
    } else {
      escaped.push_back(*ptr);
    }
  }
  return escaped;
}

string EscapeForUrl(const string& from) {
  return EscapeReservedCharacters(from, NeedsEscaping);
}

string EscapeForReservedExpansion(const string& from) {
  // TODO(user): This is not quite precise according to RFC 6570, but it is
  // good enough.
  return EscapeReservedCharacters(from, IsNotGraphic);
}

bool UnescapeFromUrl(const string& from, string* to) {
  const char* ptr = from.data();
  const char* end = ptr + from.size();
  to->clear();
  for (; ptr < end; ++ptr) {
    if (*ptr == '%') {
      if (end - ptr >= 2) {
        int32 value;
        if (safe_strto32_base(string(ptr + 1, 2), &value, 16)
            && value <= 255) {
          ptr += 2;
          to->push_back(static_cast<char>(value));
        } else {
          return false;
        }
      } else {
        return false;
      }
    } else {
      to->push_back(*ptr);
    }
  }
  return true;
}


string ResolveUrl(const string& base_url,
                  const string& relative_url) {
  // Implements Section 4 of http://www.ietf.org/rfc/rfc1808.txt
  // Step 1
  if (base_url.empty()) return relative_url;

  // Step 2
  // part a
  if (relative_url.empty()) return base_url;

  // part b
  if (relative_url.find(':') != string::npos) {
    return relative_url;
  }

  // The segments_handled is an index we'll use for how far along in the
  // url segments we've gotten before we hit to goto statement indicating
  // that we've finished and just need to add the remaining segments
  // (Step 7 in the RFC).
  int segments_handled = 0;

  // The resolved result url as we build it up.
  string result;

  ParsedUrl parsed_base(base_url);
  ParsedUrl parsed_relative(relative_url);

  // This scope holds some temporary variables that we're going to jump over.
  {
    // part c
    result = ParsedUrl::SegmentOrEmpty(!parsed_base.scheme().empty(),
                                       parsed_base.scheme(), ":");

    // Step 3
    if (!parsed_relative.netloc().empty()) {
      goto step_7;
    }
    result.append(ParsedUrl::SegmentOrEmpty(!parsed_base.netloc().empty(),
                                             "//", parsed_base.netloc()));
    ++segments_handled;

    // Step 4
    if (parsed_relative.path().size()
        && parsed_relative.path()[0] == '/') {
      goto step_7;
    }

    // Step 5
    if (parsed_relative.path().empty()) {
      result.append(parsed_base.path());
      ++segments_handled;

      // a
      if (!parsed_relative.params().empty()) {
        goto step_7;
      }
      result.append(ParsedUrl::SegmentOrEmpty(!parsed_base.params().empty(),
                                              ";", parsed_base.params()));
      ++segments_handled;

      // b
      if (!parsed_relative.query().empty()) {
        goto step_7;
      }
      result.append(ParsedUrl::SegmentOrEmpty(!parsed_base.query().empty(),
                                              "?", parsed_base.query()));
      ++segments_handled;
      goto step_7;
    }

    // Step 6
    int last_slash = parsed_base.path().rfind('/');
    string path;
    if (last_slash != string::npos) {
      path =
          string(parsed_base.path().data(), last_slash + 1);  // leave slash
    }
    path.append(parsed_relative.path());

    // a
    int offset = 0;
    while (true) {
      int dot = path.find("/./", offset);
      if (dot == string::npos) {
        break;
      }
      path.erase(dot, 2);
      offset = dot;
    }

    // b
    string path_piece = string(path);
    if (EndsWith(path_piece, "/./")) {
      // strip trailing "./"
      path.erase(path.size() - 2, 2);
    } else if (EndsWith(path_piece, "/.")) {
      path.erase(path.size() - 1, 1);
    } else if (path.size() == 1 && path.c_str()[0] == '.') {
      path.clear();
    }

    // c
    offset = 0;
    while (true) {
      int dotdot = path.find("/../", offset);
      if (dotdot == string::npos) {
        break;
      }

      if (dotdot == 0) {
        return "";  // base started with .. which is invalid.
      }
      int slash = path.rfind('/', dotdot - 1);
      path.erase(slash, dotdot + 3 - slash);
      offset = slash;
    }

    // d
    if (EndsWith(path, "/..")) {
      int slash = path.rfind('/', path.size() - 4);
      path.erase(slash + 1, path.size() - slash - 1);  // leave last slash
    }
    result.append(path);
    ++segments_handled;
  }

step_7:
  switch (segments_handled) {
    case 0:
      result.append(ParsedUrl::SegmentOrEmpty(!parsed_relative.netloc().empty(),
                                              "//", parsed_relative.netloc()));
      FALLTHROUGH_INTENDED;
    case 1:
      result.append(parsed_relative.path());
      FALLTHROUGH_INTENDED;
    case 2:
      result.append(ParsedUrl::SegmentOrEmpty(!parsed_relative.params().empty(),
                                              ";", parsed_relative.params()));
      FALLTHROUGH_INTENDED;
    case 3:
      result.append(ParsedUrl::SegmentOrEmpty(!parsed_relative.query().empty(),
                                              "?", parsed_relative.query()));
      FALLTHROUGH_INTENDED;
    case 4:
      result.append(
          ParsedUrl::SegmentOrEmpty(!parsed_relative.fragment().empty(),
                                    "#", parsed_relative.fragment()));
      break;
    default:
      // This is a programming error above.
      LOG(FATAL) << "segments_handled=" << segments_handled;
  }

  return result;
}

// Bounce from SDK namespace to global namespace.
string SimpleFtoa(float value) {
  return googleapis::SimpleFtoa(value);
}
string SimpleDtoa(double value) {
  return googleapis::SimpleDtoa(value);
}

}  // namespace client

}  // namespace googleapis
