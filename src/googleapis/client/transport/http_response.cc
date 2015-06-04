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
#include "googleapis/client/data/data_reader.h"
#include "googleapis/client/data/data_writer.h"
#include "googleapis/client/transport/http_response.h"
#include "googleapis/client/transport/http_transport.h"
#include "googleapis/client/util/status.h"
#include <glog/logging.h>
#include "googleapis/strings/strcat.h"

namespace googleapis {

namespace client {

HttpResponse::HttpResponse()
    : request_state_(new HttpRequestState),
      body_writer_(NewStringDataWriter()) {
}

HttpResponse::~HttpResponse() {
  // TODO(user): 20130525
  // Need to clean this up.
  //
  // There might be a subtle race condition here in that the state allows us to
  // wait on a request to complete. If the request owning this response
  // is destroyed while the state is waiting then we'll destroy the mutex
  // out from underneath the wait.
  //
  // So destach the state and have it destroy itself when that wait completes.
  request_state_.release()->DestroyWhenDone();
}

void HttpResponse::set_body_reader(DataReader* reader) {
  body_reader_.reset(reader);
}

void HttpResponse::set_body_writer(DataWriter* writer) {
  body_writer_.reset(writer);
}

util::Status HttpResponse::GetBodyString(string* body) {
  body->clear();
  if (!body_reader_.get()) {
    return StatusOk();
  }

  if (body_reader_->offset() != 0 && !body_reader_->Reset()) {
    LOG(WARNING) << "Could not reset HTTP response reader";
    return body_reader_->status();
  }

  *body = body_reader_->RemainderToString();
  googleapis::util::Status status = body_reader_->status();

  // Attempt to reset the reader, but dont worry about errors.
  // The reset is just to be friendly to subsequent reads.
  body_reader_->Reset();

  return status;
}

void HttpResponse::Clear() {
  body_reader_.reset(NULL);
  body_writer_->Clear();
  headers_.clear();
}

const string* HttpResponse::FindHeaderValue(const string& name) const {
  HttpHeaderMultiMap::const_iterator found = headers_.find(name);
  return (found == headers_.end()) ? NULL : &found->second;
}

}  // namespace client

}  // namespace googleapis
