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
#include <sstream>
#include <vector>

#include "googleapis/base/macros.h"
#include <glog/logging.h>
#include "googleapis/client/data/data_reader.h"
#include "googleapis/client/data/serializable_json.h"
#include "googleapis/client/service/media_uploader.h"
#include "googleapis/client/transport/http_request.h"
#include "googleapis/client/util/status.h"
#include "googleapis/client/util/uri_utils.h"
#include "googleapis/strings/strcat.h"
#include "googleapis/strings/stringpiece.h"

namespace googleapis {

static const char kMediaUploadType[] = "media";
static const char kMultipartUploadType[] = "multipart";

namespace client {
MediaUploadSpec::MediaUploadSpec() : multipart_(false) {
}

MediaUploadSpec::MediaUploadSpec(
    const StringPiece& protocol, const StringPiece& path_template,
    bool multipart)
    : protocol_(protocol),
      path_template_(path_template),
      multipart_(multipart) {
}

MediaUploadSpec::~MediaUploadSpec() {
}

MediaUploader::MediaUploader(
    const MediaUploadSpec* spec,
    const StringPiece& base_url,
    const StringPiece& non_media_upload_path)
    : spec_(spec),
      multipart_boundary_("_-client_part"),
      base_url_(base_url.as_string()),
      non_media_upload_path_(non_media_upload_path.as_string()),
      ready_(false) {
  CHECK_NOTNULL(spec);
}

MediaUploader::~MediaUploader() {
}

void MediaUploader::set_media_content_reader(
    const string& content_type, DataReader* content_reader) {
  media_content_type_ = content_type;
  media_content_reader_.reset(content_reader);
  ready_ = false;
}

void MediaUploader::set_metadata(
    const string& content_type, const string& from_text) {
  metadata_content_type_ = content_type;
  metadata_content_ = from_text;
  ready_ = false;
}

void MediaUploader::set_metadata(const SerializableJson& from_json) {
  std::ostringstream stream;
  // TODO(user): make this a reader.
  from_json.StoreToJsonStream(&stream).IgnoreError();
  metadata_content_ = stream.str();  // TODO(user): can we swap?
  metadata_content_type_ = "application/json";
  ready_ = false;
}

util::Status MediaUploader::BuildRequest(
    HttpRequest* request, UrlPreparer* preparer) {
  if (ready_) {
    const char error[] = "BuildRequest already called";
    LOG(ERROR) << error;
    return StatusInternalError(error);
  }

  string content_type;
  std::unique_ptr<DataReader> payload_reader;
  StringPiece upload_type;
  googleapis::util::Status status;

  // If there is no metadata, then this is just the media content.
  if (!media_content_reader_.get() && media_content_type_.empty()) {
    if (metadata_content_type_.empty()) {
      const char error[] = "Neither content nor metadata provided";
      LOG(ERROR) << error;
      status = StatusInvalidArgument(error);
    } else {
      payload_reader.reset(
          NewUnmanagedInMemoryDataReader(StringPiece(metadata_content_)));
      content_type = metadata_content_type_;
      upload_type = "";
    }
  } else if (metadata_content_.empty()) {
    // If for some reason we arent uploading anything then just use the
    // default case when sending the request. Otherwise if we have content
    // or are declaring a type of empty content then direct media upload it.
    if (!media_content_type_.empty()
        || (media_content_reader_.get()
            && media_content_reader_->TotalLengthIfKnown() != 0)) {
      upload_type = kMediaUploadType;
    }

    // Consume media content -- the HttpRequest will eventually own it.
    payload_reader.reset(media_content_reader_.release());
    content_type = media_content_type_;
  } else if (!spec_->is_multipart()
             && (!media_content_reader_.get()
                 || media_content_reader_->done())) {
    if (media_content_reader_.get() && media_content_reader_->error()) {
      status = StatusDataLoss("Error reading media content");
    } else {
      payload_reader.reset(
          NewUnmanagedInMemoryDataReader(StringPiece(metadata_content_)));
      content_type = metadata_content_type_;
    }
    upload_type = "";
  } else if (!spec_->is_multipart()) {
    // TODO(user): 20130122
    // Need to sequence calls and pass created id from first into second
    // (and is the second an update or insert?). Also need to handle partial
    // failure where first part succeeds but second part fails.
    status = StatusUnimplemented(
        "Media spec does not support multipart uploads");
    upload_type = "";
  } else {
    if (!media_content_reader_.get()) {
      // Treat NULL as empty in this case, but will keep it NULL when
      // we didnt specify a content_type_ either.
      CHECK(!media_content_type_.empty());
      media_content_reader_.reset(NewUnmanagedInMemoryDataReader(""));
    }
    payload_reader.reset(CreateMultipartPayloadReader(&content_type));
    upload_type = kMultipartUploadType;
  }

  if (!status.ok()) {
    // Run the preparer before we return early in case it is defined
    // as a one-shot callback. Otherwise we'll leak it.
    if (preparer) {
      string ignore;
      preparer->Run("", &ignore).IgnoreError();
    }
    return status;
  }

  string template_url;
  if (upload_type.empty()) {
    template_url = JoinPath(base_url_, non_media_upload_path_);
  } else {
    template_url =
        StrCat(JoinPath(base_url_, spec_->path_template().as_string()),
               "?uploadType=", upload_type);
  }

  string prepared_url;
  if (!preparer) {
    prepared_url.swap(template_url);
  } else {
    status = preparer->Run(template_url, &prepared_url);
  }

  if (status.ok()) {
    request->set_url(prepared_url);
    request->set_content_type(content_type);
    request->set_content_reader(payload_reader.release());
    ready_ = true;
  }

  return status;
}

DataReader* MediaUploader::CreateMultipartPayloadReader(string* content_type) {
  const StringPiece kDash("--");
  const StringPiece kEoln("\n");
  std::vector<DataReader*>* list(new std::vector<DataReader*>);

  string boundary = multipart_boundary_;

  // TODO(user)
  // Add boundary.empty() clause for auto-finding a boundary.
  // This is going to be tricky given fragmented readers.
  // This also requires rewindable readers to use it.

  // Create a multi part message putting metadata first, then media second.
  const StringPiece kBoundary(boundary);

  // We're going to pass ownership of this into the reader below.
  string* payload = new string;
  StrAppend(payload, kDash, kBoundary, kEoln);
  if (!metadata_content_type_.empty()) {
    StrAppend(payload,
              "Content-Type: ", metadata_content_type_, kEoln,
              kEoln);
  }
  StrAppend(payload, metadata_content_, kEoln);

  StrAppend(payload, kDash, kBoundary, kEoln);
  if (!media_content_type_.empty()) {
    StrAppend(payload,
              "Content-Type: ", media_content_type_, kEoln,
              kEoln);
  }

  // Reader[0] is the opening boundary through the end of the first part
  // metadata and into the content-type declaration of the second part.

  // Get the string piece first to ensure order of operations with the release
  // when building the closure.
  list->push_back(
      NewManagedInMemoryDataReader(
          StringPiece(*payload), DeletePointerClosure(payload)));
  payload = NULL;

  // Reader[1] is the body of the second part (the media)
  CHECK(media_content_reader_.get());
  list->push_back(media_content_reader_.release());

  payload = new string(
      StrCat(kEoln,  // end media section
             kDash, kBoundary, kDash, kEoln));  // multipart boundary footer

  // Reader[2] is the footer closing out the payload stream and multipart
  // message as a whole.
  list->push_back(
      NewManagedInMemoryDataReader(
          StringPiece(*payload), DeletePointerClosure(payload)));

  *content_type = StrCat(HttpRequest::ContentType_MULTIPART_RELATED,
                         "; boundary=", kBoundary);

  return NewManagedCompositeDataReader(
      *list, NewCompositeReaderListAndContainerDeleter(list));
}

util::Status MediaUploader::Upload(HttpRequest* request) {
  if (!is_ready()) {
    googleapis::util::Status status = StatusInternalError("Uploader was not prepared");
    LOG(ERROR) << status.error_message();
    request->WillNotExecute(status);
    return status;
  }
  return request->Execute();
}

void MediaUploader::UploadAsync(HttpRequest* request,
                                HttpRequestCallback* callback) {
  if (!is_ready()) {
    googleapis::util::Status status = StatusInternalError("Uploader was not prepared");
    LOG(ERROR) << status.error_message();
    request->WillNotExecute(status);
  }
  request->ExecuteAsync(callback);
}

}  // namespace client

}  // namespace googleapis
