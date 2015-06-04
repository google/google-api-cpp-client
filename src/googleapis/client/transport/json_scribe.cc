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


#include "googleapis/client/data/data_reader.h"
#include "googleapis/client/data/data_writer.h"
#include "googleapis/client/transport/http_request.h"
#include "googleapis/client/transport/http_request_batch.h"
#include "googleapis/client/transport/http_response.h"
#include "googleapis/client/transport/json_scribe.h"
#include "googleapis/client/util/date_time.h"
#include "googleapis/strings/numbers.h"
#include <json/value.h>
#include <json/writer.h>

namespace googleapis {

using client::JsonScribe;
const char JsonScribe::kStartTime[] = "StartTime";
const char JsonScribe::kEndTime[]   = "EndTime";
const char JsonScribe::kMaxSnippet[] = "MaxSnippet";
const char JsonScribe::kMessages[]  = "Messages";
const char JsonScribe::kMethod[]    = "Method";
const char JsonScribe::kUrl[]       = "Url";
const char JsonScribe::kHttpCode[]       = "HttpCode";
const char JsonScribe::kStatusCode[]     = "StatusCode";
const char JsonScribe::kStatusMessage[]  = "StatusMsg";

const char JsonScribe::kBatched[]   = "Batch";
const char JsonScribe::kRequest[]   = "Request";
const char JsonScribe::kResponse[]  = "Response";
const char JsonScribe::kHeaders[]   = "Headers";
const char JsonScribe::kPayload[]   = "Bytes";
const char JsonScribe::kPayloadSize[]     = "Size";
const char JsonScribe::kPayloadCensored[] = "Censored";

const char JsonScribe::kSendMicros[]      = "SentMicros";
const char JsonScribe::kResponseMicros[]  = "ReceiveMicros";
const char JsonScribe::kErrorMicros[]     = "ErrorMicros";

namespace client {
namespace {

inline void set_json(const string& s, Json::Value* value) {
  *value = s.c_str();
}
inline void set_json(int64 n, Json::Value* value) {
  *value = SimpleItoa(n).c_str();
}

class JsonEntry : public HttpEntryScribe::Entry {
 public:
  JsonEntry(
      HttpEntryScribe* scribe,
      const HttpRequest* request,
      Json::Value* array)
      : HttpEntryScribe::Entry(scribe, request), json_array_(array) {
  }
  JsonEntry(
      HttpEntryScribe* scribe,
      const HttpRequestBatch* batch,
      Json::Value* array)
      : HttpEntryScribe::Entry(scribe, batch), json_array_(array) {
  }
  ~JsonEntry() {}

  virtual void FlushAndDestroy() {
    VLOG(1) << "Flushing " << json_array_->size();
    VLOG(9) << json_.toStyledString();
    json_array_->append(json_);
    delete this;
  }

  void ConstructRequestJson(
      const HttpRequest* request, Json::Value* json) {
    const int restriction_mask = request->scribe_restrictions();
    bool censored;
    const HttpScribeCensor* censor = scribe()->censor();
    set_json(request->http_method(), &(*json)[JsonScribe::kMethod]);
    set_json(censor->GetCensoredUrl(*request, &censored),
             &(*json)[JsonScribe::kUrl]);

    if ((restriction_mask & HttpScribe::FLAG_NO_REQUEST_HEADERS) == 0) {
      Json::Value* json_request = &(*json)[JsonScribe::kRequest];
      const HttpHeaderMap& headers = request->headers();
      Json::Value* json_headers = &(*json_request)[JsonScribe::kHeaders];
      for (HttpHeaderMap::const_iterator
               it = headers.begin();
           it != headers.end();
           ++it) {
        set_json(
            censor->GetCensoredRequestHeaderValue(
                *request, it->first, it->second, &censored),
            &(*json_headers)[it->first]);
      }
    }

    if ((restriction_mask & HttpScribe::FLAG_NO_REQUEST_PAYLOAD) == 0) {
      Json::Value* json_request = &(*json)[JsonScribe::kRequest];
      int64 original_size;
      string content = censor->GetCensoredRequestContent(
          *request, scribe()->max_snippet(), &original_size, &censored);
      set_json(content, &(*json_request)[JsonScribe::kPayload]);
      set_json(original_size, &(*json_request)[JsonScribe::kPayloadSize]);
      if (censored) {
        (*json_request)[JsonScribe::kPayloadCensored] = true;
      }
    }
  }

  void ConstructRequestBatchJson(
      const HttpRequestBatch* batch, Json::Value* json) {
    Json::Value* container = &(*json)[JsonScribe::kBatched];
    const HttpRequestBatch::BatchedRequestList& list = batch->requests();
    int i = 0;
    for (HttpRequestBatch::BatchedRequestList::const_iterator it
             = list.begin();
         it != list.end();
         ++it, ++i) {
      ConstructRequestJson(*it, &(*container)[i]);
    }
  }

  virtual void Sent(const HttpRequest* request) {
    ConstructRequestJson(request, &json_);
    set_json(MicrosElapsed(), &json_[JsonScribe::kSendMicros]);
  }

  virtual void SentBatch(const HttpRequestBatch* batch) {
    ConstructRequestBatchJson(batch, &json_);
  }

  void Received(const HttpRequest* request) {
    set_json(MicrosElapsed(), &json_[JsonScribe::kResponseMicros]);
    HandleResponse(request, &json_);
  }

  void ReceivedBatch(const HttpRequestBatch* batch) {
    HandleResponseBatch(batch, &json_);
  }

  void Failed(const HttpRequest* request, const googleapis::util::Status& status) {
    if (json_.isNull()) {
      ConstructRequestJson(request, &json_);
    }
    set_json(MicrosElapsed(), &json_[JsonScribe::kErrorMicros]);
    json_[JsonScribe::kStatusCode] = status.error_code();
    set_json(status.error_message(), &json_[JsonScribe::kStatusMessage]);
  }

  void FailedBatch(const HttpRequestBatch* batch, const googleapis::util::Status& status) {
    if (json_.isNull()) {
      ConstructRequestBatchJson(batch, &json_);
    }
  }

  void HandleResponseBatch(const HttpRequestBatch* batch, Json::Value* json) {
    Json::Value* container = &(*json)[JsonScribe::kBatched];
    const HttpRequestBatch::BatchedRequestList& list = batch->requests();
    int i = 0;
    for (HttpRequestBatch::BatchedRequestList::const_iterator it
             = list.begin();
         it != list.end();
         ++it, ++i) {
      HandleResponse(*it, &(*container)[i]);
    }
  }

  void HandleResponse(
      const HttpRequest* request, Json::Value* json) {
    const int restriction_mask = request->scribe_restrictions();
    (*json)[JsonScribe::kHttpCode] = request->response()->http_code();

    bool censored;
    const HttpScribeCensor* censor = scribe()->censor();

    if ((restriction_mask & HttpScribe::FLAG_NO_RESPONSE_HEADERS) == 0) {
      Json::Value* json_response = &(*json)[JsonScribe::kResponse];
      Json::Value* json_headers = &(*json_response)[JsonScribe::kHeaders];
      const HttpHeaderMultiMap& headers = request->response()->headers();
      for (HttpHeaderMultiMap::const_iterator
               it = headers.begin();
           it != headers.end();
           ++it) {
        (*json_headers)[it->first] = censor->GetCensoredResponseHeaderValue(
            *request, it->first, it->second, &censored).c_str();
      }
    }

    if ((restriction_mask & HttpScribe::FLAG_NO_RESPONSE_PAYLOAD) == 0) {
      Json::Value* json_response = &(*json)[JsonScribe::kResponse];
      int64 original_size;
      set_json(
          censor->GetCensoredResponseBody(
              *request, scribe()->max_snippet(), &original_size, &censored),
          &(*json_response)[JsonScribe::kPayload]);
      set_json(original_size, &(*json_response)[JsonScribe::kPayloadSize]);
      if (censored) {
        (*json_response)[JsonScribe::kPayloadCensored] = true;
      }
    }
  }

 private:
  Json::Value *json_array_;
  Json::Value json_;
};

}  // anonymous namespace

JsonScribe::JsonScribe(
    HttpScribeCensor* censor, DataWriter* writer, bool compact)
    : HttpEntryScribe(censor), writer_(writer), last_checkpoint_(0) {
  set_json(DateTime().ToString(), &json_[kStartTime]);
  if (compact) {
    json_writer_.reset(new Json::FastWriter);
  } else {
    json_writer_.reset(new Json::StyledWriter);
  }
}

JsonScribe::~JsonScribe() {
  DiscardQueue();
  if (json_.isMember(kMessages)
      && json_[kMessages].size() != last_checkpoint_) {
    Checkpoint();
  }
}

void JsonScribe::Checkpoint() {
  int64 num_messages = -1;
  if (json_.isMember(kMessages)) {
    num_messages = json_[kMessages].size();
  }
  if (num_messages == last_checkpoint_) return;

  // TODO(user): 20130522
  // Change this so we keep a running stream to disk rather than in memory.
  // Need to add an autocorrect mechanism in the playback first so it can
  // deal with improperly terminated trasncripts.
  //
  // This should be ok for an initial implementation dealing with small
  // transcripts.
  json_[kEndTime] = DateTime().ToString().c_str();
  writer_->Clear();
  writer_->Begin();
  writer_->Write(json_writer_->write(json_)).IgnoreError();
  writer_->End();
  last_checkpoint_ = num_messages;
}

// Note that this method is already protected by the base class so is
// threa-safe
HttpEntryScribe::Entry* JsonScribe::NewEntry(const HttpRequest* request) {
  if (!started_) {
    // Record the snippet size configuration.
    json_[kMaxSnippet] = SimpleItoa(max_snippet()).c_str();
    started_ = true;
  }
  return new JsonEntry(this, request, &json_[kMessages]);
}

HttpEntryScribe::Entry* JsonScribe::NewBatchEntry(
    const HttpRequestBatch* batch) {
  if (!started_) {
    // Record the snippet size configuration.
    json_[kMaxSnippet] = SimpleItoa(max_snippet()).c_str();
    started_ = true;
  }
  return new JsonEntry(this, batch, &json_[kMessages]);
}

}  // namespace client

}  // namespace googleapis
