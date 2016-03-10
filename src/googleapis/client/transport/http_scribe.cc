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


#include <deque>
#include <set>
#include <string>
using std::string;
#include <vector>

#include "googleapis/client/data/data_reader.h"
#include "googleapis/client/transport/http_request.h"
#include "googleapis/client/transport/http_request_batch.h"
#include "googleapis/client/transport/http_response.h"
#include "googleapis/client/transport/http_scribe.h"
#include "googleapis/client/util/date_time.h"
#include "googleapis/client/util/uri_utils.h"
#include "googleapis/base/integral_types.h"
#include <glog/logging.h>
#include "googleapis/base/macros.h"
#include "googleapis/strings/strcat.h"
#include "googleapis/strings/stringpiece.h"

namespace googleapis {

namespace {
using client::DataReader;

const StringPiece kCensored("CENSORED");

// Make sure we have a value at base + offset.
// We are looking for a complete match of a json value.
// Json values are in the form ' : \"<value>\".
//                                  ^        ^
//                        start ----+        +---- end
bool FindValueRangeWithQuotes(
    const char* base, int64 offset, int64* start, int64* end) {
  const char* pc = base + offset;
  while (*pc <= ' ' && *pc) {
    ++pc;
  }
  if (*pc != ':') {
    // perhaps this is a ',' because the tag was the value.
    return false;
  }
  ++pc;
  while (*pc <= ' ' && *pc) {
    ++pc;
  }
  char quote = *pc;
  if (quote != '\"' && quote != '\'') {
    return false;
  }
  *start = pc - base;

  for (++pc; *pc != quote && *pc; ++pc) {
    if (*pc == '\\') {
      ++pc;
      if (!*pc) break;  // not well formed but keep it anyway.
    }
  }
  if (*pc) ++pc;  // keep closing quote.
  *end = pc - base;
  return true;
}

string CensorAllJsonValuesForTagHelper(
    const string& json, const StringPiece& tag, bool* censored) {
  string result;
  int64 offset = 0;
  while (offset < json.size()) {
    int64 found = json.find(tag.data(), offset, tag.size());
    if (found == string::npos) {
      result.append(json.c_str() + offset);
      break;
    }

    int64 start_quote;
    int64 end_quote;
    int64 end_tag = found + tag.size();
    result.append(json.c_str() + offset, end_tag - offset);
    if (!FindValueRangeWithQuotes(
            json.c_str(), end_tag, &start_quote, &end_quote)) {
      offset = end_tag;
      continue;
    }
    StrAppend(&result,
              StringPiece(json.c_str() + end_tag, start_quote - end_tag),
              "\"", kCensored, "\"");
    *censored = true;
    offset = end_quote;
  }
  return result;
}

string ReadSnippet(int64 max_len, DataReader* reader) {
  string result;
  bool elide = false;
  if (reader->offset() != 0) {
    reader->Reset();
  }
  reader->ReadToString(max_len, &result);
  if (reader->error()) {
    result = StrCat("ERROR: ", reader->status().error_message());
    elide = result.size() > max_len;
  } else {
    elide = !reader->done();
  }
  if (!reader->Reset()) {
    LOG(WARNING) << "Censor could not reset the request reader.";
  }
  if (elide) {
    const char kEllipse[] = "...";
    if (max_len > 3) {
      result.erase(max_len - 3);
      result.append(kEllipse);
    } else {
      result = string(kEllipse, max_len);
    }
  }
  return result;
}

}  // anonymous namespace

namespace client {

HttpScribeCensor::HttpScribeCensor() {
  censored_url_prefixes_.insert("https://accounts.google.com");

  censored_query_param_names_.insert("access_token");
  censored_query_param_names_.insert("refresh_token");
  censored_query_param_names_.insert("client_secret");
  censored_query_param_names_.insert("Passwd");

  censored_request_header_names_.insert(HttpRequest::HttpHeader_AUTHORIZATION);
}
HttpScribeCensor::~HttpScribeCensor() {}

string HttpScribeCensor::GetCensoredUrl(
    const HttpRequest& request, bool* censored) const {
  if (request.scribe_restrictions() & HttpScribe::FLAG_NO_URL) {
    *censored = true;
    return "URL was not made available";
  }
  ParsedUrl parsed_url(request.url());
  string censored_query = GetCensoredUrlQuery(parsed_url, censored);

  // Build normal url parts up to the query parameters.
  // NOTE(user): 20130520
  // We're assuming there's nothing sensitive in the path.
  string censored_url =
      StrCat(ParsedUrl::SegmentOrEmpty(!parsed_url.scheme().empty(),
                                       parsed_url.scheme(), "://"),
             parsed_url.netloc(),
             parsed_url.path(),
             ParsedUrl::SegmentOrEmpty(!parsed_url.params().empty(),
                                       ";", parsed_url.params()),
             ParsedUrl::SegmentOrEmpty(!censored_query.empty(),
                                       "?", censored_query),
             ParsedUrl::SegmentOrEmpty(!parsed_url.fragment().empty(),
                                       "#", parsed_url.fragment()));;
  return censored_url;
}

string HttpScribeCensor::GetCensoredRequestHeaderValue(
    const HttpRequest& request,
    const string& name, const string& value,
    bool* censored) const {
  if (request.scribe_restrictions() & HttpScribe::FLAG_NO_REQUEST_HEADERS) {
    *censored = true;
    return "Request headers was not made available";
  }
  if (censored_request_header_names_.find(name)
      != censored_request_header_names_.end()) {
    *censored = true;
    return kCensored.as_string();
  }
  *censored = false;
  return value;
}

string HttpScribeCensor::GetCensoredResponseHeaderValue(
    const HttpRequest& request,
    const string& name, const string& value,
    bool* censored) const {
  if (request.scribe_restrictions() & HttpScribe::FLAG_NO_RESPONSE_HEADERS) {
    *censored = true;
    return "Request headers was not made available";
  }
  if (censored_response_header_names_.find(name)
      != censored_response_header_names_.end()) {
    *censored = true;
    return kCensored.as_string();
  }
  *censored = false;
  return value;
}

string HttpScribeCensor::GetCensoredRequestContent(
    const HttpRequest& request,
    int64 max_len,
    int64* original_size,
    bool* censored) const {
  *censored = false;
  DataReader* reader = request.content_reader();
  if (!reader) {
    *original_size = 0;
    return "";
  }
  *original_size = reader->TotalLengthIfKnown();
  if (request.scribe_restrictions() & HttpScribe::FLAG_NO_REQUEST_PAYLOAD) {
    *censored = true;
    return "Request payload was not made available";
  }

  string result;
  if (IsSensitiveContent(request.url())) {
    *censored = true;
    if (max_len < kCensored.size()) {
      result = string(kCensored.data(), max_len);
    } else {
      result = kCensored.as_string();
    }
  } else {
    result = ReadSnippet(max_len, reader);

    const string* content_type_ptr = request.FindHeaderValue(
        HttpRequest::HttpHeader_CONTENT_TYPE);
    StringPiece content_type;
    if (content_type_ptr) {
      content_type = *content_type_ptr;
    }

    if (content_type.starts_with(HttpRequest::ContentType_FORM_URL_ENCODED)) {
      // Jam this payload into a bogus url and censor it using the
      // query parameter mechanism.
      string fake_url = StrCat("http://netloc?", result);
      ParsedUrl parsed_url(fake_url);
      result = GetCensoredUrlQuery(parsed_url, censored);
    }
  }
  return result;
}

string HttpScribeCensor::GetCensoredResponseBody(
    const HttpRequest& request, int64 max_len,
    int64* original_size,
    bool* censored) const {
  *censored = false;
  DataReader* reader = request.response()->body_reader();
  if (!reader) {
    *original_size = 0;
    return "";
  }
  *original_size = reader->TotalLengthIfKnown();
  if (request.scribe_restrictions() & HttpScribe::FLAG_NO_RESPONSE_PAYLOAD) {
    *censored = true;
    return "Response payload was not made available";
  }

  string result;
  if (IsSensitiveContent(request.url())) {
    *censored = true;
    if (max_len < kCensored.size()) {
      result = string(kCensored.data(), max_len);
    } else {
      result = kCensored.as_string();
    }
  } else {
    result = ReadSnippet(max_len, reader);

    const string* content_type_ptr = request.response()->FindHeaderValue(
        HttpRequest::HttpHeader_CONTENT_TYPE);
    StringPiece content_type;
    if (content_type_ptr) {
      content_type = *content_type_ptr;
    }

    if (content_type.starts_with(HttpRequest::ContentType_JSON)) {
      // We're not going to parse into json for now because when we render
      // the result back we'll lose the original formatting.
      result = CensorAllJsonValuesForTagHelper(
          result, "\"refresh_token\"", censored);
      result = CensorAllJsonValuesForTagHelper(
          result, "\"access_token\"", censored);
    }
  }

  return result;
}

string HttpScribeCensor::GetCensoredUrlQuery(
    const ParsedUrl& parsed_url, bool* censored) const {
  *censored = false;
  const char* sep = "";

  // Check each query parameter for censoring.
  const std::vector<ParsedUrl::QueryParameterAssignment>& params =
      parsed_url.GetQueryParameterAssignments();
  string censored_query;
  for (std::vector<ParsedUrl::QueryParameterAssignment>::const_iterator it =
           params.begin();
       it != params.end();
       ++it) {
    const StringPiece& name = it->first;
    const StringPiece& value = it->second;
    StrAppend(&censored_query, sep, name);
    if (!value.empty()) {
      if (censored_query_param_names_.find(name.as_string())
          != censored_query_param_names_.end()) {
        *censored = true;
        StrAppend(&censored_query, "=", kCensored);
      } else {
        StrAppend(&censored_query, "=", value);
      }
    }
    sep = "&";
  }

  return censored_query;
}

bool HttpScribeCensor::IsSensitiveContent(const string& url) const {
  StringPiece check(url);
  for (std::set<string>::const_iterator it = censored_url_prefixes_.begin();
       it != censored_url_prefixes_.end();
       ++it) {
    if (check.starts_with(*it)) return true;
  }
  return false;
}


HttpEntryScribe::Entry::Entry(
    HttpEntryScribe* scribe, const HttpRequest* request)
    : scribe_(scribe), request_(request), batch_(NULL),
      received_request_(false), received_batch_(false) {
  DateTime now_time;
  now_time.GetTimeval(&timeval_);
}

HttpEntryScribe::Entry::Entry(
    HttpEntryScribe* scribe, const HttpRequestBatch* batch)
    : scribe_(scribe), request_(&batch->http_request()), batch_(batch),
      received_request_(false), received_batch_(false) {
  DateTime now_time;
  now_time.GetTimeval(&timeval_);
}

HttpEntryScribe::Entry::~Entry() {
}

void HttpEntryScribe::Entry::CancelAndDestroy() {
  delete this;
}

int64 HttpEntryScribe::Entry::MicrosElapsed() const {
  struct timeval now;
  DateTime now_time;
  now_time.GetTimeval(&now);

  int64 delta_s = now.tv_sec - timeval_.tv_sec;
  int64 delta_us = now.tv_usec - timeval_.tv_usec;
  return delta_us + 1000000 * delta_s;
}

HttpScribe::HttpScribe(HttpScribeCensor* censor)
    : censor_(censor), max_snippet_(kint64max) {
}

HttpScribe::~HttpScribe() {
}

HttpEntryScribe::HttpEntryScribe(HttpScribeCensor* censor)
    : HttpScribe(censor) {
}

HttpEntryScribe::~HttpEntryScribe() {
  DiscardQueue();
}

void HttpEntryScribe::AboutToSendRequest(const HttpRequest* request) {
  GetEntry(request)->Sent(request);
}

void HttpEntryScribe::AboutToSendRequestBatch(const HttpRequestBatch* batch) {
  GetBatchEntry(batch)->SentBatch(batch);
}


// This class is used to provide common code where we implement
// regular Entry and BatchEntry essentially the same way but they use
// different entry constructors or lookup methods.
class HttpEntryScribe::Internal {
 public:
  static HttpEntryScribe::Entry* GetEntryHelper(
      HttpEntryScribe* scribe,
      const HttpRequest* request,
      const HttpRequestBatch* batch) {
    MutexLock l(&scribe->mutex_);
    EntryMap::iterator it = scribe->map_.find(request);
    if (it != scribe->map_.end()) return it->second;

    HttpEntryScribe::Entry* entry =
        batch ? scribe->NewBatchEntry(batch) : scribe->NewEntry(request);

    VLOG(1) << "Adding " << entry;
    scribe->map_.insert(std::make_pair(request, entry));
    scribe->queue_.push_back(entry);
    VLOG(1) << "Added " << entry << " as " << scribe->queue_.size();
    return entry;
  }

  static HttpEntryScribe::Entry* GetEntry(
      HttpEntryScribe* scribe, const HttpRequest* request) {
    return GetEntryHelper(scribe, request, NULL);
  }

  static HttpEntryScribe::Entry* GetBatchEntry(
      HttpEntryScribe* scribe, const HttpRequestBatch* batch) {
    return GetEntryHelper(scribe, &batch->http_request(), batch);
  }

  static void ReceiveResponseForRequest(
      HttpEntryScribe* scribe, const HttpRequest* request) {
    HttpEntryScribe::Entry* entry = scribe->GetEntry(request);
    entry->set_received_request(true);
    entry->Received(request);
    if (!entry->is_batch()) {
      scribe->DiscardEntry(entry);
    }
  }

  static void ReceiveResponseForRequestBatch(
      HttpEntryScribe* scribe, const HttpRequestBatch* batch) {
    HttpEntryScribe::Entry* entry = scribe->GetBatchEntry(batch);
    entry->set_received_batch(true);
    entry->ReceivedBatch(batch);
    scribe->DiscardEntry(entry);
  }

  static void RequestFailedWithTransportError(
      HttpEntryScribe* scribe,
      const HttpRequest* request,
      const googleapis::util::Status& status) {
    HttpEntryScribe::Entry* entry = scribe->GetEntry(request);
    entry->set_received_request(true);
    entry->Failed(request, status);
    if (!entry->is_batch()) {
      scribe->DiscardEntry(entry);
    }
  }

  static void RequestBatchFailedWithTransportError(
      HttpEntryScribe* scribe,
      const HttpRequestBatch* batch,
      const googleapis::util::Status& status) {
    HttpEntryScribe::Entry* entry = scribe->GetBatchEntry(batch);
    entry->set_received_batch(true);
    entry->FailedBatch(batch, status);
    scribe->DiscardEntry(entry);
  }
};

void HttpEntryScribe::ReceivedResponseForRequest(const HttpRequest* request) {
  Internal::ReceiveResponseForRequest(this, request);
}

void HttpEntryScribe::ReceivedResponseForRequestBatch(
    const HttpRequestBatch* batch) {
  Internal::ReceiveResponseForRequestBatch(this, batch);
}

void HttpEntryScribe::RequestFailedWithTransportError(
    const HttpRequest* request, const googleapis::util::Status& status) {
  Internal::RequestFailedWithTransportError(this, request, status);
}

void HttpEntryScribe::RequestBatchFailedWithTransportError(
    const HttpRequestBatch* batch, const googleapis::util::Status& status) {
  Internal::RequestBatchFailedWithTransportError(this, batch, status);
}

void HttpEntryScribe::DiscardQueue() {
  MutexLock l(&mutex_);
  if (!queue_.empty()) {
    LOG(WARNING) << "Discarding scribe's queue with " << queue_.size()
                 << " entries still outstanding.";
    while (!queue_.empty()) {
      UnsafeDiscardEntry(queue_.front());
    }
  }
}

HttpEntryScribe::Entry* HttpEntryScribe::GetEntry(
    const HttpRequest* request) {
  return Internal::GetEntry(this, request);
}

HttpEntryScribe::Entry* HttpEntryScribe::GetBatchEntry(
    const HttpRequestBatch* batch) {
  return Internal::GetBatchEntry(this, batch);
}

void HttpEntryScribe::DiscardEntry(HttpEntryScribe::Entry* entry) {
  MutexLock l(&mutex_);
  VLOG(1) << "Discard " << entry;
  UnsafeDiscardEntry(entry);
}

void HttpEntryScribe::UnsafeDiscardEntry(
    HttpEntryScribe::Entry* entry) {
  VLOG(1) << "Removing " << entry;
  EntryMap::iterator found = map_.find(entry->request());
  if (VLOG_IS_ON(1) && found == map_.end()) {
    for (EntryMap::const_iterator it = map_.begin();
         it != map_.end();
         ++it) {
      VLOG(1) << "  map has " << it->second;
    }
  }
  CHECK(found != map_.end());
  bool front = entry == queue_.front();
  if (front) {
    queue_.pop_front();
  } else {
    VLOG(1) << "Still waiting on " << queue_.front();
    for (EntryQueue::iterator it = queue_.begin();
         it != queue_.end();
         ++it) {
      if (*it == entry) {
        queue_.erase(it);
        break;
      }
    }
  }
  map_.erase(found);
  entry->FlushAndDestroy();
}

}  // namespace client

}  // namespace googleapis
