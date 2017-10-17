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


#include <cstdio>
#include <set>
#include <vector>

#include "googleapis/client/data/data_reader.h"
#include "googleapis/client/data/data_writer.h"
#include "googleapis/client/transport/http_authorization.h"
#include "googleapis/client/transport/http_request.h"
#include "googleapis/client/transport/http_request_batch.h"
#include "googleapis/client/transport/http_scribe.h"
#include "googleapis/client/transport/http_transport.h"
#include "googleapis/client/util/status.h"
#include "googleapis/strings/strcat.h"

namespace googleapis {

namespace client {

#ifndef _WIN32
using std::snprintf;
#endif

// TODO(user): In batch request, we encode the pointer to the batch into the
// request as an identifier, then in ExtractPartResponse we convert back to
// an address. That is dangerous. Replace that scheme with something where
// we do not blindly dereference user data.
string HttpRequestBatch::PointerToHex(void *p) {
  char tmp[40];  // more than enough for 128 bit address
  snprintf(tmp, sizeof(tmp), "%p", p);
  return tmp;
}

namespace {

// These are the requests that we'll make to be batched.
// They are specialized to act as a specification for the batch to
// form its multipart message rather than to actually be sent themselves.
class IndividualRequest : public HttpRequest {
 public:
  IndividualRequest(
      HttpMethod method,
      HttpTransport* transport,
      HttpRequestCallback* callback)
      : HttpRequest(method, transport) {
    if (callback) {
      set_callback(callback);
    }
  }
  ~IndividualRequest() {}

  // Set the response from the multipart response entry for this request.
  void ParseResponse(const StringPiece& payload) {
    std::unique_ptr<DataReader> payload_reader(
        NewUnmanagedInMemoryDataReader(payload));
    HttpTransport::ReadResponse(payload_reader.get(), this->response());
    this->response()->set_body_reader(
        this->response()->body_writer()->NewUnmanagedDataReader());
  }

  // Adds multipart body for this individual request, minus the boundary.
  void Encode(std::vector<DataReader*>* readers,
              std::vector<DataReader*>* destroy_later) {
    string* header_str = new string;
    std::unique_ptr<DataWriter> writer(NewStringDataWriter(header_str));
    HttpTransport::WriteRequestPreamble(this, writer.get());
    Closure* delete_str = DeletePointerClosure(header_str);
    DataReader* header_reader = writer->NewManagedDataReader(delete_str);
    readers->push_back(header_reader);
    destroy_later->push_back(header_reader);

    DataReader* content = content_reader();
    if (content) {
      readers->push_back(content);
    }
  }

 protected:
  void DoExecute(HttpResponse* response) {
    mutable_state()->set_transport_status(
        StatusInternalError(
            "Elements in batch requests should not be executed individually"));
  }

 private:
  DISALLOW_COPY_AND_ASSIGN(IndividualRequest);
};

// Helper function to get the next block in a multipart response.
static StringPiece GetMultipartBlock(
    const StringPiece whole_response,   // the mutipart response body
    int64 offset,                       // in whole_response where block begins
    const string& kBoundaryMarker,      // for intermediary blocks
    const string& kLastBoundaryMarker,  // for final block
    bool* processing_last,              // true if we matched the last marker
    int64* next_offset) {               // where next block after this begins
  *processing_last = false;
  int end_offset = whole_response.find(kBoundaryMarker, offset);
  if (end_offset == string::npos) {
    *processing_last = true;
    end_offset = whole_response.find(kLastBoundaryMarker, offset);
  }
  if (end_offset == string::npos) {
    *next_offset = string::npos;
    return "";
  }

  *next_offset = end_offset +
      (*processing_last ? kLastBoundaryMarker.size() : kBoundaryMarker.size());

  return whole_response.substr(offset, end_offset - offset);
}

// Given an indivdiual response, verify that it makes sense and identify
// the original HttpRequest (our specialized IndividualRequest) that it is for.
static googleapis::util::Status ExtractPartResponse(
    const StringPiece& multipart_block,
    std::set<HttpRequest*>* expected_requests,  // remove the one we find.
    StringPiece* http_response_message,    // subset of multipart_block on out
    IndividualRequest** http_request) {    // request found or NULL
  http_response_message->clear();
  *http_request = NULL;
  int double_eoln_offset = multipart_block.find(kCRLFCRLF);
  if (double_eoln_offset == StringPiece::npos) {
    return StatusUnknown(
        StrCat("Missing response part separator for batched message."));
  }

  // +kCRLF to keep last eoln.
  int64 end_metadata = double_eoln_offset + kCRLF.size();
  *http_response_message = multipart_block.substr(
      double_eoln_offset + kCRLFCRLF.size(),
      multipart_block.size() - double_eoln_offset - kCRLFCRLF.size());

  string batch_metadata(multipart_block.data(), end_metadata);
  LowerString(&batch_metadata);
  if (batch_metadata.find("content-type: application/http\r\n")
      == string::npos) {
    return StatusUnknown("Missing or wrong batch part content-type");
  }

  const char content_id_header_prefix[] = "content-id: <response-";
  int64 id_header_offset = batch_metadata.find(content_id_header_prefix);
  if (id_header_offset == string::npos) {
    return StatusUnknown("Missing batch part content-id");
  }

  void *id;
  int pointer_offset =
      id_header_offset + sizeof(content_id_header_prefix) - 1;
  if (sscanf(batch_metadata.c_str() + pointer_offset,
             "%p>\r\n",
             &id) != 1) {
    return StatusUnknown("content-id batch part was not as expected");
  }

  // The id is an HttpRequest which is really our internal IndividualRequest
  // since that's how we encapsulate it.
  *http_request = reinterpret_cast<IndividualRequest*>(id);
  std::set<HttpRequest*>::iterator found = expected_requests->find(
      *http_request);
  if (found == expected_requests->end()) {
    *http_request = NULL;
    return StatusUnknown("Got unexpected content-id in batch response");
  }
  expected_requests->erase(found);
  return StatusOk();
}

static googleapis::util::Status ResolveResponses(
    const string& boundary_text,
    DataReader* reader,
    std::vector<HttpRequest*>* requests) {
  const string kBoundaryMarker = StrCat(kCRLF, "--", boundary_text, kCRLF);
  const string kLastBoundaryMarker =
      StrCat(kCRLF, "--", boundary_text, "--", kCRLF);
  string whole_response = reader->RemainderToString();

  googleapis::util::Status return_status = StatusOk();
  googleapis::util::Status transport_status = StatusOk();
  if (!StringPiece(whole_response).starts_with(
          kBoundaryMarker.c_str() + kCRLF.size())) {
    transport_status = StatusUnknown(
        "Response does not begin with boundary marker");
    return_status = transport_status;
    LOG(ERROR) << return_status.error_message();
  }

  // Collect all the requests we expect to be in this batch.
  std::set<HttpRequest*> expected_requests;
  for (HttpRequestBatch::BatchedRequestList::iterator it = requests->begin();
       it != requests->end();
       ++it) {
    HttpRequestState* state = (*it)->mutable_state();
    if (!transport_status.ok()) {
      state->set_transport_status(transport_status);
    }
    if (!state->transport_status().ok()) {
      // Was never sent, so just finish it now.
      state->AutoTransitionAndNotifyIfDone().IgnoreError();
    } else {
      expected_requests.insert(*it);
    }
  }

  // Iterate over the parts in the response.
  // Resolve their content-id back to the original request
  // (we used the pointer address as the id).
  // We'll remove the requests as we resolve them so we can catch those
  // which never had a response, etc.
  int64 offset = kBoundaryMarker.size() - kCRLF.size();
  int64 next_offset;
  bool processing_last = false;
  for (; !processing_last; offset = next_offset) {
    StringPiece this_multipart_block = GetMultipartBlock(
        whole_response, offset,
        kBoundaryMarker, kLastBoundaryMarker, &processing_last, &next_offset);
    if (next_offset == string::npos) {
      googleapis::util::Status status =
          StatusUnknown("Missing closing multipart boundary marker.");
      if (return_status.ok()) {
        return_status = status;
        LOG(ERROR) << status.error_message();
      } else {
        VLOG(1) << status.error_message();
      }
      break;
    }

    IndividualRequest* individual_request;
    StringPiece part_request_response_text;

    googleapis::util::Status status = ExtractPartResponse(
        this_multipart_block, &expected_requests,
        &part_request_response_text, &individual_request);
    if (!status.ok()) {
      if (return_status.ok()) {
        return_status = status;
        LOG(ERROR) << status.error_message();
      } else {
        VLOG(1) << status.error_message();
      }
      continue;
    }

    individual_request->ParseResponse(part_request_response_text);

    // TODO(user): 20130815
    // Call error handler here when appropriate and add to followup batch on
    // retry. Post retries and chain another call to handle those batches
    // posting async if this was async. Chain the batch requests within this
    // structure so perhaps the caller can see if needed.
    HttpRequestState* state = individual_request->mutable_state();
    state->AutoTransitionAndNotifyIfDone().IgnoreError();
  }

  if (!expected_requests.empty()) {
    googleapis::util::Status missing_error =
        StatusUnknown("Never received response for batched request");
    for (std::set<HttpRequest*>::iterator it = expected_requests.begin();
         it != expected_requests.end();
         ++it) {
      HttpRequestState* state = (*it)->mutable_state();
      state->set_transport_status(missing_error);
      state->AutoTransitionAndNotifyIfDone().IgnoreError();
      VLOG(1) << missing_error.error_message();
    }

    // Keep the existing error since it might be the cause.
    // but if everything looked ok up to here then report this.
    if (return_status.ok()) {
      LOG(ERROR) << missing_error.error_message();
      return_status = missing_error;
    }
  }

  return return_status;
}

const char DEFAULT_BATCH_REQUEST_URL[] = "https://www.googleapis.com/batch";

}  // anonymous namespace

HttpRequestBatch::HttpRequestBatch(HttpTransport* transport)
: HttpRequestBatch(transport, DEFAULT_BATCH_REQUEST_URL) {
}

HttpRequestBatch::HttpRequestBatch(HttpTransport* transport,
                                   const std::string& batch_url)
    : http_request_(transport->NewHttpRequest(HttpRequest::POST)),
      boundary_("bAtch bOundAry") {
  http_request_->set_url(batch_url);

  // If we are scribing a transcript then dont show the details of this
  // low level message because we'll already be showing the high level batch
  // message. This message wont be censored properly because of the multipart
  // nature to it. The HttpScribe interface knows about BatchHttpMessage so
  // can properly censor it as well as produce more readable transcripts for
  // it. We'll still leave the URL behind to help reconcile it.
  http_request_->set_scribe_restrictions(HttpScribe::MASK_NO_PAYLOADS);
}

HttpRequestBatch::~HttpRequestBatch() {
  Clear();

  // In async mode, http_request_ is self-destructing. So to avoid double-free
  // we must release the unique_ptr. But be careful not to do that if the
  // request hasn't executed.
  if (http_request_->state().done()) {
    http_request_->DestroyWhenDone();
    http_request_.release();
  }
}

void HttpRequestBatch::Clear() {
  // clear all the requests so they are notified, but then delete them all
  // so this batch request is empty again.
  for (std::vector<HttpRequest*>::iterator it = requests_.begin();
       it != requests_.end();
       ++it) {
    (*it)->Clear();
    delete *it;
  }
  requests_.clear();
}

HttpRequest* HttpRequestBatch::NewHttpRequest(
    HttpRequest::HttpMethod method, HttpRequestCallback* callback) {
  HttpRequest* request = new IndividualRequest(
      method, http_request_->transport(), callback);
  requests_.push_back(request);
  return request;
}

util::Status HttpRequestBatch::RemoveAndDestroyRequest(HttpRequest* request) {
  for (std::vector<HttpRequest*>::iterator it = requests_.begin();
       it != requests_.end();
       ++it) {
    if (*it == request) {
      request->WillNotExecute(StatusAborted("Removing from batch"));
      delete *it;
      requests_.erase(it);
      return StatusOk();
    }
  }
  return StatusInvalidArgument("Request not in batch");
}

util::Status HttpRequestBatch::Execute() {
  PrepareFinalHttpRequest();

  HttpScribe* scribe = http_request_->transport()->scribe();
  if (scribe) {
    scribe->AboutToSendRequestBatch(this);
  }

  googleapis::util::Status status = http_request_->Execute();
  ProcessHttpResponse(NULL, http_request_.get());
  return batch_processing_status_;
}

void HttpRequestBatch::ExecuteAsync(HttpRequestCallback* callback) {
  HttpRequestCallback* batch_callback =
      NewCallback(this, &HttpRequestBatch::ProcessHttpResponse, callback);
  PrepareFinalHttpRequest();

  HttpScribe* scribe = http_request_->transport()->scribe();
  if (scribe) {
    scribe->AboutToSendRequestBatch(this);
  }

  http_request_->ExecuteAsync(batch_callback);
}

void HttpRequestBatch::PrepareFinalHttpRequest() {
  // The actual request will be a private request that is a normal request
  // (i.e. created from the transport originally bound to this one).
  // We'll form a multipart payload with a part for each of the requests
  // aggregated in this batch and copy all the top-level headers in that
  // were set on this. Then we'll execute the request and do similar copying
  // from the private response back into the response bound to this one.
  std::vector<DataReader*> individual_readers;
  std::vector<DataReader*>* readers_to_destroy = new std::vector<DataReader*>;
  for (std::vector<HttpRequest*>::const_iterator it = requests_.begin();
       it != requests_.end();
       ++it) {
    IndividualRequest* part = static_cast<IndividualRequest*>(*it);
    string* preamble = new string;
    StrAppend(preamble,
              "--", boundary_, kCRLF,
              "Content-Type: application/http", kCRLF,
              "Content-Transfer-Encoding: binary", kCRLF);
    StrAppend(preamble, "Content-ID: <", PointerToHex(*it), ">", kCRLFCRLF);
    DataReader* preamble_reader = NewManagedInMemoryDataReader(preamble);
    individual_readers.push_back(preamble_reader);
    readers_to_destroy->push_back(preamble_reader);

    // Authorize the http request if it had a credential.
    // Normally this happens in the HTTP's Execute method
    // but we are bypassing that.
    if (part->credential()) {
      googleapis::util::Status auth_status = part->credential()->AuthorizeRequest(part);
      if (!auth_status.ok()) {
        LOG(ERROR) << "Failed to authorize batched request: "
                   << auth_status.error_message();

        // We wont bother sending this request. We'll have to use
        // the Content-ID field in the metadata to line up responses
        // since cardinalities wont match anymore.
        part->mutable_state()->set_transport_status(auth_status);
        continue;
      }
    }

    part->Encode(&individual_readers, readers_to_destroy);
  }

  DataReader* last_terminator =
      NewManagedInMemoryDataReader(StrCat("--", boundary_, "--", kCRLF));
  individual_readers.push_back(last_terminator);
  readers_to_destroy->push_back(last_terminator);

  http_request_->set_content_type(
      StrCat("multipart/mixed; boundary=\"", boundary_, "\""));
  http_request_->set_content_reader(
      NewManagedCompositeDataReader(
          individual_readers,
          NewCompositeReaderListAndContainerDeleter(readers_to_destroy)));
}

static void ScribeResponseAndFinishCallback(
    HttpRequestBatch* batch, HttpRequestCallback* callback) {
  HttpRequest* http_request = batch->mutable_http_request();
  HttpScribe* scribe = http_request->transport()->scribe();
  if (scribe) {
    HttpResponse* response = http_request->response();
    if (response->http_code()) {
      scribe->ReceivedResponseForRequestBatch(batch);
    } else {
      scribe->RequestBatchFailedWithTransportError(
          batch, response->transport_status());
    }
  }
  if (callback) {
    callback->Run(http_request);
  }
}

void HttpRequestBatch::ProcessHttpResponse(
    HttpRequestCallback* callback, HttpRequest* expected_request) {
  AutoClosureRunner runner(
      NewCallback(&ScribeResponseAndFinishCallback, this, callback));
  CHECK_EQ(http_request_.get(), expected_request);
  HttpResponse* response = expected_request->response();
  if (!response->transport_status().ok()) {
    batch_processing_status_ = response->transport_status();
    LOG(ERROR) << "Could not send batch request";
    return;
  }

  const char kBoundaryMarker[] = "boundary=";
  HttpHeaderMultiMap::const_iterator found_content_type =
      response->headers().find(HttpRequest::HttpHeader_CONTENT_TYPE);
  const string kEmpty;
  const string& content_type = found_content_type == response->headers().end()
      ? kEmpty
      : found_content_type->second;

  int pos = content_type.find(kBoundaryMarker);
  if (pos == string::npos) {
    batch_processing_status_ =
        StatusUnknown(
            StrCat("Expected multipart content type: ", content_type));
    return;
  }
  string kResponseBoundary =
      content_type.substr(pos + sizeof(kBoundaryMarker) - 1);
  batch_processing_status_ =
      ResolveResponses(
          kResponseBoundary, response->body_reader(), &requests_);
  if (!batch_processing_status_.ok()) {
    LOG(ERROR) << "Responses from server were not as expected: "
               << batch_processing_status_.error_message();
  }
}

HttpRequest* HttpRequestBatch::AddFromGenericRequestAndRetire(
    HttpRequest* original, HttpRequestCallback* callback) {
  HttpRequest* part = NewHttpRequest(original->http_method());
  original->SwapToRequestThenDestroy(part);
  if (callback) {
    part->set_callback(callback);
  }
  return part;
}

}  // namespace client

}  // namespace googleapis
