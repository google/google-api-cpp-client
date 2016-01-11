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

//
// Implementation of RFC 6570 based on (open source implementation) at
//   java/com/google/api/client/http/UriTemplate.java
// The URI Template spec is at http://tools.ietf.org/html/rfc6570
#include <set>
#include <string>
using std::string;

#include "googleapis/client/util/status.h"
#include "googleapis/client/util/uri_template.h"
#include "googleapis/client/util/uri_utils.h"
#include "googleapis/strings/strcat.h"

namespace googleapis {


namespace {
const char* kNonExplodeJoiner = ",";

}  // anonymous namespace

namespace client {

// Configuration for expanding composite values. These are constructed
// within the UriTemplate and passed back to AppendVariable.
struct UriTemplateConfig {
 public:
  string variable_name_;
  const char* prefix_;
  const char* joiner_;
  bool requires_variable_assignment_;
  bool reserved_expansion_;
  bool explode_;

  UriTemplateConfig(
      const char* prefix, const char* joiner, bool var, bool reserved)
      : prefix_(prefix), joiner_(joiner), requires_variable_assignment_(var),
        reserved_expansion_(reserved), explode_(false) {
  }

  void AppendValue(const string& value, string* target) const {
    string escaped;
    if (reserved_expansion_) {
      // reserved expansion passes through everything.
      escaped = EscapeForReservedExpansion(value);
    } else {
      escaped = EscapeForUrl(value);
    }
    target->append(escaped);
  }

  void AppendKeyValue(
      const string& key, const string& value, string* target) const {
    const char* pair_joiner = explode_ ? "=" : kNonExplodeJoiner;
    AppendValue(key, target);
    target->append(pair_joiner);
    AppendValue(value, target);
  }
};

// variable is an in-out argument. On input it is the content between the
// '{}' in the source. On result the control parameters are stripped off
// leaving just the variable name that should be passed to the user-supplied
// AppendVariableCallback.
static UriTemplateConfig MakeConfig(string* variable) {
  switch (*variable->data()) {
    // Reserved expansion.
    case '+':
      *variable = variable->substr(1);
      return UriTemplateConfig("", ",", false, true);

    // Fragment expansion.
    case '#':
      *variable = variable->substr(1);
      return UriTemplateConfig("#", ",", false, true);

    // Label with dot-prefix.
    case '.':
      *variable = variable->substr(1);
      return UriTemplateConfig(".", ".", false, false);

    // Path segment expansion.
    case '/':
      *variable = variable->substr(1);
      return UriTemplateConfig("/", "/", false, false);

    // Path segment parameter expansion.
    case ';':
      *variable = variable->substr(1);
      return UriTemplateConfig(";", ";", true, false);

    // Form-style query expansion.
    case '?':
      *variable = variable->substr(1);
      return UriTemplateConfig("?", "&", true, false);

    // Form-style query continuation.
    case '&':
      *variable = variable->substr(1);
      return UriTemplateConfig("&", "&", true, false);

    // Simple expansion.
    default:
      return UriTemplateConfig("", ",", false, false);
  }
}

static googleapis::util::Status ProcessVariable(
    string* variable,
    UriTemplate::AppendVariableCallback* provider,
    string* target) {
  bool explode = variable->back() == '*';
  if (explode) {
    variable->resize(variable->size()-1);
  }
  // Note that this function will modify the template to remove the decorators
  // leaving just the name.
  UriTemplateConfig config = MakeConfig(variable);
  config.variable_name_ = *variable;
  config.explode_ = explode;

  return provider->Run(*variable, config, target);
}

// static
util::Status UriTemplate::Expand(
    const string& path_uri, AppendVariableCallback* provider,
    string* target, std::set<string>* vars_found) {
  googleapis::util::Status final_status = StatusOk();
  provider->CheckIsRepeatable();
  int cur = 0;
  int length = path_uri.length();
  while (cur < length) {
    int next = path_uri.find('{', cur);
    if (next == string::npos) {
      target->append(path_uri.substr(cur).data(), path_uri.length() - cur);
      return StatusOk();
    }
    if (next > cur) {
      target->append(path_uri.data(), cur, next - cur);
    }
    int close = path_uri.find('}', next + 1);
    if (close == string::npos) {
      string error = StrCat("Malformed variable near ", next, " ", path_uri);
      return StatusInvalidArgument(error);
    }
    string variable(path_uri, next + 1, close - next - 1);
    cur = close + 1;
    googleapis::util::Status status = ProcessVariable(&variable, provider, target);
    if (!status.ok()) {
      // Remember the last status to make a best effort expanding all the
      // variables that we can.
      final_status = status;

      // Keep the variable reference since we could not resolve it.
      target->append(path_uri.data() + next, close - next + 1);
    } else if (vars_found) {
      vars_found->insert(variable);
    }
  }
  return final_status;
}

// static
void UriTemplate::AppendListFirst(
    const string& value, const UriTemplateConfig& config, string* target) {
  target->append(config.prefix_);
  if (config.requires_variable_assignment_) {
    target->append(EscapeForUrl(config.variable_name_));
    target->append("=");
  }
  config.AppendValue(value, target);
}

// static
void UriTemplate::AppendListNext(
    const string& value, const UriTemplateConfig& config, string* target) {
  const char* joiner = config.explode_ ? config.joiner_ : kNonExplodeJoiner;
  target->append(joiner);

  if (config.explode_ && config.requires_variable_assignment_) {
    target->append(EscapeForUrl(config.variable_name_));
    target->append("=");
  }
  config.AppendValue(value, target);
}

// static
void UriTemplate::AppendMapFirst(
    const string& key, const string& value,
    const UriTemplateConfig& config, string* target) {
  target->append(config.prefix_);
  if (!config.explode_ && config.requires_variable_assignment_) {
    target->append(EscapeForUrl(config.variable_name_));
    target->append("=");
  }

  config.AppendKeyValue(key, value, target);
}

// static
void UriTemplate::AppendMapNext(
    const string& key, const string& value,
    const UriTemplateConfig& config, string* target) {
  const char* joiner = config.explode_ ? config.joiner_ : kNonExplodeJoiner;
  target->append(joiner);
  config.AppendKeyValue(key, value, target);
}

// static
void UriTemplate::AppendValueString(
    const string& value, const UriTemplateConfig& config, string* target) {
  config.AppendValue(value, target);
}

}  // namespace client

}  // namespace googleapis
