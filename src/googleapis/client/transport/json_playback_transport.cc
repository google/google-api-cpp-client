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


#include <map>
#include <memory>
#include <string>
using std::string;
#include <vector>

#include "googleapis/client/data/data_reader.h"
#include "googleapis/client/data/data_writer.h"
#include "googleapis/client/transport/json_playback_transport.h"
#include "googleapis/client/transport/json_scribe.h"
#include "googleapis/client/transport/http_request.h"
#include "googleapis/client/transport/http_response.h"
#include "googleapis/client/transport/http_transport.h"
#include "googleapis/client/transport/http_types.h"
#include "googleapis/client/util/status.h"
#include "googleapis/base/integral_types.h"
#include "googleapis/base/macros.h"
#include <glog/logging.h>
#include "googleapis/base/mutex.h"
#include "googleapis/strings/strcat.h"
#include "googleapis/strings/stringpiece.h"
#include <json/reader.h>
#include <json/value.h>

namespace googleapis {

using client::HttpRequest;
using client::HttpResponse;
using client::HttpTransport;
using client::HttpHeaderMap;
using client::HttpHeaderMultiMap;
using client::HttpScribeCensor;
using client::JsonScribe;

const char kFakeUserAgent[] = "PlaybackStrippedUserAgent";

namespace {

struct RequestRecord {
  RequestRecord() : http_code(-1), error_code(util::error::OK) {
  }

  bool Init(const Json::Value* msg) {
    if (!msg->isMember("Method")) {
      LOG(ERROR) << "Missing 'Method'";
      return false;
    }
    if (!msg->isMember("Url")) {
      LOG(ERROR) << "Missing 'Url'";
      return false;
    }

    method = (*msg)[JsonScribe::kMethod].asString();
    url = (*msg)["Url"].asString();

    if (msg->isMember(JsonScribe::kStatusCode)) {
      int n = (*msg)[JsonScribe::kStatusCode].asInt();
      if (n < util::error::Code_MIN || n > util::error::Code_MAX) {
        LOG(ERROR) << "Unknown error code " << n;
        n = util::error::UNKNOWN;
      }
      error_code = static_cast<util::error::Code>(n);
    }
    if (msg->isMember(JsonScribe::kStatusMessage)) {
      error_message = (*msg)[JsonScribe::kStatusMessage].asString();
    }

    if (msg->isMember(JsonScribe::kHttpCode)) {
      http_code = (*msg)[JsonScribe::kHttpCode].asInt();
    }

    const Json::Value* request = &(*msg)[JsonScribe::kRequest];
    const Json::Value* response = &(*msg)[JsonScribe::kResponse];
    if (request->isMember(JsonScribe::kPayload)) {
      request_payload = (*request)[JsonScribe::kPayload].asString();
    }
    if (response->isMember(JsonScribe::kPayload)) {
      response_payload = (*response)[JsonScribe::kPayload].asString();
    }
    if (request->isMember(JsonScribe::kHeaders)) {
      Json::Value json_request_headers = (*request)[JsonScribe::kHeaders];
      for (Json::Value::iterator it = json_request_headers.begin();
           it != json_request_headers.end();
           ++it) {
        const char* name = it.memberName();
        if (!*name) {
          LOG(ERROR) << "Empty request header name";
          return false;
        }
        if (StringPiece(name) == HttpRequest::HttpHeader_USER_AGENT) {
          // Dont bother matching user agent headers.
          request_headers.insert(
              std::make_pair(name, kFakeUserAgent));
          continue;
        }
        request_headers.insert(std::make_pair(name, (*it).asCString()));
      }
    }
    if (response->isMember(JsonScribe::kHeaders)) {
      Json::Value json_response_headers = (*response)[JsonScribe::kHeaders];
      for (Json::Value::iterator it = json_response_headers.begin();
           it != json_response_headers.end();
           ++it) {
        const char* name = it.memberName();
        if (!*name) {
          LOG(ERROR) << "Empty response header name";
          return false;
        }
        response_headers.insert(std::make_pair(name, (*it).asCString()));
      }
    }
    return true;
  }

  string method;
  string url;
  string request_payload;
  string response_payload;
  string error_message;
  int http_code;
  util::error::Code error_code;
  HttpHeaderMap request_headers;
  HttpHeaderMultiMap response_headers;
};

}  // anonymous namespace

namespace client {

// static
const char JsonPlaybackTransport::kTransportIdentifier[] = "JSON Playback";

class JsonPlaybackTranscript {
 public:
  explicit JsonPlaybackTranscript(const HttpScribeCensor* censor)
      : censor_(censor), max_snippet_(kint64max) {
  }

  ~JsonPlaybackTranscript() {
    for (RequestToListMap::iterator it = request_to_list_map_.begin();
         it != request_to_list_map_.end();
         ++it) {
      delete it->second;  // delete the list
    }
  }

  googleapis::util::Status Load(DataReader* reader);
  RequestRecord* GetNextRecord(const HttpRequest& request);
  HttpRequest* NewRequest(
      const HttpRequest::HttpMethod& method, HttpTransport* transport);

 private:
  struct RequestRecordList {
    RequestRecordList() : index(0) {}
    ~RequestRecordList() {
      auto begin = records.begin();
      auto end = records.end();
      while (begin != end) {
        auto temp = begin;
        ++begin;
        delete *temp;
      }
      records.clear();
    }
    int index;
    std::vector<RequestRecord*> records;
  };
  typedef std::map<string, RequestRecordList*> RequestToListMap;

  // Really just the individual lists need to be protected,
  // and they are each independent from one another (just need to protect
  // concurrent access into the same list). But performance doesnt matter
  // so we'll just use a single mutex across all the lists and protect
  // the map as a whole.
  Mutex mutex_;
  RequestToListMap request_to_list_map_ GUARDED_BY(mutex_);
  const HttpScribeCensor* censor_;
  int64 max_snippet_;

  DISALLOW_COPY_AND_ASSIGN(JsonPlaybackTranscript);
};

}  // namespace client

namespace {

using client::JsonPlaybackTranscript;
using client::StatusInternalError;

class PlaybackRequest : public HttpRequest {
 public:
  PlaybackRequest(
      const HttpRequest::HttpMethod& method,
      HttpTransport* transport,
      JsonPlaybackTranscript* transcript)
      : HttpRequest(method, transport), transcript_(transcript) {
  }
  virtual ~PlaybackRequest() {}

 protected:
  virtual void DoExecute(HttpResponse* response) {
    RequestRecord* record = transcript_->GetNextRecord(*this);
    if (!record) {
      mutable_state()->set_transport_status(
          StatusInternalError("No playback for request"));
      return;
    }
    if (record->http_code > 0) {
      response->set_http_code(record->http_code);
      response->body_writer()->Begin();
      response->body_writer()->Write(record->response_payload).IgnoreError();
      response->body_writer()->End();
      if (!response->body_writer()->ok()) {
        LOG(ERROR) << "Error writing response body";
        mutable_state()->set_transport_status(
            response->body_writer()->status());
      }
    } else {
      mutable_state()->set_transport_status(
          googleapis::util::Status(record->error_code, record->error_message));
    }
    for (HttpHeaderMultiMap::const_iterator it =
             record->response_headers.begin();
         it != record->response_headers.end();
         ++it) {
      response->AddHeader(it->first, it->second);
    }
  }

 private:
  JsonPlaybackTranscript* transcript_;
  DISALLOW_COPY_AND_ASSIGN(PlaybackRequest);
};

}  // anonymous namespace

namespace client {

util::Status JsonPlaybackTranscript::Load(DataReader* reader) {
  // This method isnt called by multiple threads so this lock is superfluous.
  // However we declared we are protecting the map so thread-analysis wants
  // us to guard it.
  MutexLock l(&mutex_);

  string json = reader->RemainderToString();
  if (!reader->ok()) return reader->status();

  Json::Value journal;
  Json::Reader json_reader;
  if (!json_reader.parse(json, journal)) {
return StatusUnknown(json_reader.getFormatedErrorMessages());
  }

  if (journal.isMember(JsonScribe::kMaxSnippet)) {
    Json::Value max_snippet = journal[JsonScribe::kMaxSnippet];
    int64 value;
    if (safe_strto64(max_snippet.asCString(), &value)) {
      max_snippet_ = value;
    } else {
      LOG(ERROR) << "Could not parse max_snippet=" << max_snippet.asCString();
    }
  }

  Json::Value array = journal[JsonScribe::kMessages];
  VLOG(1) << "Loading num_messages=" << array.size();

  for (int i = 0; i < array.size(); ++i) {
    const Json::Value* msg = &array[i];
    std::unique_ptr<RequestRecord> record(new RequestRecord);
    CHECK(record->Init(msg)) << "i=" << " msg=" << msg->toStyledString();

    string key = StrCat(record->method, record->url);
    RequestToListMap::iterator found = request_to_list_map_.find(key);
    RequestRecordList* list;
    if (found == request_to_list_map_.end()) {
      list = new RequestRecordList;
      request_to_list_map_.insert(std::make_pair(key, list));
    } else {
      list = found->second;
    }
    list->records.push_back(record.release());
  }

  return StatusOk();
}

RequestRecord* JsonPlaybackTranscript::GetNextRecord(
    const HttpRequest& request) {
  bool censored;
  string censored_url =
      censor_
      ? censor_->GetCensoredUrl(request, &censored)
      : request.url();
  string key = StrCat(request.http_method(), censored_url);

  MutexLock l(&mutex_);
  RequestToListMap::iterator found = request_to_list_map_.find(key);
  if (found == request_to_list_map_.end()) {
    LOG(ERROR) << "No playback entry for method=" << request.http_method()
               << " url=" << request.url();
    return NULL;
  }

  string content;
  if (censor_) {
    int64 original_size;
    content =
        censor_->GetCensoredRequestContent(
            request, max_snippet_, &original_size, &censored);
  } else {
    DataReader* reader = request.content_reader();
    if (reader) {
      content = reader->RemainderToString();
    }
  }

  RequestRecordList* list = found->second;
  HttpHeaderMap headers;
  if (!censor_) {
    headers = request.headers();
  } else {
    for (HttpHeaderMap::const_iterator it = request.headers().begin();
         it != request.headers().end();
         ++it) {
      if (it->first == HttpRequest::HttpHeader_USER_AGENT) {
        headers.insert(std::make_pair(it->first, kFakeUserAgent));
        continue;
      }
      headers.insert(std::make_pair(
          it->first, censor_->GetCensoredRequestHeaderValue(
                         request, it->first, it->second, &censored)));
    }
  }

  for (std::vector<RequestRecord*>::iterator it = list->records.begin();
       it != list->records.end();
       ++it) {
    RequestRecord* record = *it;
    if (content != record->request_payload) continue;
    if (record->request_headers != headers) continue;
    // Move this record to the end so we match other responses to the
    // same query before returning this one again.
    list->records.erase(it);
    list->records.push_back(record);
    return record;
  }
  LOG(ERROR) << "Could not find a matching record";
  return NULL;
}

HttpRequest* JsonPlaybackTranscript::NewRequest(
    const HttpRequest::HttpMethod& method, HttpTransport* transport) {
  return new PlaybackRequest(method, transport, this);
}


JsonPlaybackTransport::JsonPlaybackTransport(
    const HttpTransportOptions& options)
    : HttpTransport(options), transcript_(NULL), censor_(NULL) {
  set_id(kTransportIdentifier);
}

JsonPlaybackTransport::~JsonPlaybackTransport() {
}

HttpRequest* JsonPlaybackTransport::NewHttpRequest(
    const HttpRequest::HttpMethod& method) {
  CHECK(transcript_)
      << "Either set_transcript or LoadTranscript";
  return transcript_->NewRequest(method, this);
}

util::Status JsonPlaybackTransport::LoadTranscript(
     DataReader* reader) {
  std::unique_ptr<JsonPlaybackTranscript> t(
      new JsonPlaybackTranscript(censor_));
  transcript_ = NULL;
  googleapis::util::Status status = t->Load(reader);
  if (status.ok()) {
    transcript_storage_.reset(t.release());
    transcript_ = transcript_storage_.get();
  }
  return status;
}


JsonPlaybackTransportFactory::JsonPlaybackTransportFactory() {
  set_default_id(JsonPlaybackTransport::kTransportIdentifier);
}

JsonPlaybackTransportFactory::JsonPlaybackTransportFactory(
    const HttpTransportLayerConfig* config)
    : HttpTransportFactory(config) {
  set_default_id(JsonPlaybackTransport::kTransportIdentifier);
}


JsonPlaybackTransportFactory::~JsonPlaybackTransportFactory() {}

void JsonPlaybackTransportFactory::ResetCensor(HttpScribeCensor* censor) {
  censor_.reset(censor);
}

HttpTransport* JsonPlaybackTransportFactory::DoAlloc(
    const HttpTransportOptions& options) {
  CHECK(transcript_.get());
  JsonPlaybackTransport* transport =
      new JsonPlaybackTransport(options);
  transport->set_transcript(transcript_.get());
  transport->set_censor(censor_.get());
  return transport;
}

util::Status JsonPlaybackTransportFactory::LoadTranscript(
     DataReader* reader) {
  std::unique_ptr<JsonPlaybackTranscript> t(
      new JsonPlaybackTranscript(censor_.get()));
  googleapis::util::Status status = t->Load(reader);
  if (status.ok()) {
    transcript_.reset(t.release());
  }
  return status;
}

}  // namespace client

}  // namespace googleapis
