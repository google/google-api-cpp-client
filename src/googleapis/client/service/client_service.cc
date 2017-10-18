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


#include <iostream>
using std::cout;
using std::endl;
using std::ostream;  // NOLINT
#include <string>
using std::string;
#include <sstream>

#include <glog/logging.h>
#include "googleapis/client/data/serializable_json.h"
#include "googleapis/client/service/client_service.h"
#include "googleapis/client/service/media_uploader.h"
#include "googleapis/client/transport/http_request.h"
#include "googleapis/client/transport/http_request_batch.h"
#include "googleapis/client/transport/http_response.h"
#include "googleapis/client/transport/http_transport.h"
#include "googleapis/client/util/status.h"
#include "googleapis/client/util/uri_template.h"
#include "googleapis/client/util/uri_utils.h"
#include "googleapis/strings/stringpiece.h"

namespace googleapis {

namespace client {

ClientServiceRequest::ClientServiceRequest(
    const ClientService* service,
    AuthorizationCredential* credential,
    const HttpRequest::HttpMethod& method,
    const StringPiece& uri_template)
    : http_request_(nullptr), destroy_when_done_(false),
      use_media_download_(false) {
  if (service->in_shutdown()) {
    return;
  }
  uri_template_ = service->service_url();
  uri_template_.append(uri_template.as_string());
  http_request_.reset(service->transport()->NewHttpRequest(method));
  http_request_->set_credential(credential);  // can be NULL
  // We own the request so make sure it wont auto destroy
  http_request_->mutable_options()->set_destroy_when_done(false);
}

ClientServiceRequest::~ClientServiceRequest() {
}


HttpRequest* ClientServiceRequest::ConvertToHttpRequestAndDestroy() {
  if (http_request_ == nullptr) {
    delete this;
    return nullptr;
  }
  googleapis::util::Status status = PrepareHttpRequest();
  if (!status.ok()) {
    LOG(WARNING) << "Error preparing request: " << status.error_message();
    http_request_->mutable_state()->set_transport_status(status);
  }
  HttpRequest* result = http_request_.release();
  delete this;
  return result;
}

HttpRequest* ClientServiceRequest::ConvertIntoHttpRequestBatchAndDestroy(
    HttpRequestBatch* batch, HttpRequestCallback* callback) {
  HttpRequest* http_request = ConvertToHttpRequestAndDestroy();
  if (http_request == nullptr) return nullptr;
  return batch->AddFromGenericRequestAndRetire(http_request, callback);
}

void ClientServiceRequest::DestroyWhenDone() {
  if (http_request_ != nullptr && http_request_->state().done()) {
    // Avoid race condition if we're destroying this while the underlying
    // request isnt ready to be deleted.
    HttpRequest* request = http_request_.release();
    if (request) {
      request->DestroyWhenDone();
    }
    delete this;
  } else {
    destroy_when_done_ = true;
  }
}

util::Status ClientServiceRequest::PrepareHttpRequest() {
  if (http_request_ == nullptr) {
    return StatusCanceled("shutdown");
  }
  string url;
  googleapis::util::Status status = PrepareUrl(uri_template_, &url);
  http_request_->set_url(url);
  VLOG(1) << "Prepared url:" << url;
  return status;
}

util::Status ClientServiceRequest::PrepareUrl(
    const StringPiece& templated_url, string* prepared_url) {
  std::unique_ptr<UriTemplate::AppendVariableCallback> callback(
      NewPermanentCallback(this, &ClientServiceRequest::CallAppendVariable));

  // Attempt to expand everything for best effort.
  googleapis::util::Status expand_status = UriTemplate::Expand(
      templated_url.as_string(), callback.get(), prepared_url);
  googleapis::util::Status query_status = AppendOptionalQueryParameters(prepared_url);

  return expand_status.ok() ? query_status : expand_status;
}

util::Status ClientServiceRequest::Execute() {
  if (http_request_ == nullptr) {
    return StatusCanceled("shutdown");
  }
  if (uploader_.get()) {
    return this->ExecuteWithUploader();
  }
  googleapis::util::Status status = PrepareHttpRequest();
  if (!status.ok()) {
    http_request_->WillNotExecute(status);
    return status;
  }

  status = http_request_->Execute();
  if (destroy_when_done_) {
    VLOG(1)  << "Auto-destroying " << this;
    delete this;
  }
  return status;
}

util::Status ClientServiceRequest::ExecuteWithUploader() {
  if (http_request_ == nullptr) {
    return StatusCanceled("shutdown");
  }
  client::HttpRequest* request = mutable_http_request();
  googleapis::util::Status status =
    uploader_->BuildRequest(
        request,
        NewCallback(this, &ClientServiceRequest::PrepareUrl));
  if (!status.ok()) {
    return status;
  }
  return uploader_->Upload(request);
}

util::Status ClientServiceRequest::ExecuteAndParseResponse(
     SerializableJson* data) {
  bool destroy_when_done = destroy_when_done_;
  destroy_when_done_ = false;
  googleapis::util::Status result = Execute();
  if (result.ok()) {
    result = ParseResponse(http_response(), data);
  }
  if (destroy_when_done) {
    delete this;
  }
  return result;
}

void ClientServiceRequest::CallbackThenDestroy(
    HttpRequestCallback* callback, HttpRequest* request) {
  callback->Run(request);
  VLOG(1) << "Auto-deleting request because it is done.";
  if (http_request_ != nullptr) {
    // TODO(user): 20130318
    // This is called by HttpRequest::Execute via DoExecute which expects the
    // request to still be valid so it can auto-destroy. Maybe I should cache
    // that value before calling the callback to permit the callback to destroy
    // the request, as it does here. In the meantime we'll detach the
    // http request object so it can self destruct. Otherwise if we destroy it
    // here, the caller will implode from having the object deleted when it goes
    // to check whether it should destroy it or not.
    http_request_->mutable_options()->set_destroy_when_done(true);
    http_request_.release();
  }
  delete this;
}

void ClientServiceRequest::ExecuteAsync(HttpRequestCallback* callback) {
  if (destroy_when_done_) {
    // If we want to destroy the request when we're done then chain the
    // callback into one that will destroy this request instance.
    VLOG(1) << "Will intercept request callback to auto-delete";
    HttpRequestCallback* destroy_request =
        NewCallback(this, &ClientServiceRequest::CallbackThenDestroy,
                    callback);
    callback = destroy_request;
  }
  if (http_request_ == nullptr) {
    if (callback) {
      callback->Run(nullptr);
    }
    return;
  }
  if (callback) {
    // bind the callback here so if PrepareHttpRequest fails then we
    // can notify the callback.
    http_request_->set_callback(callback);
  }

  googleapis::util::Status status;
  if (uploader_.get()) {
    status = uploader_->BuildRequest(
        http_request_.get(),
        NewCallback(this, &ClientServiceRequest::PrepareUrl));
  } else {
    status = PrepareHttpRequest();
  }
  if (!status.ok()) {
    http_request_->WillNotExecute(status);
    return;
  }

  // We already bound the callback, so it does not have to be passed to the
  // executor.
  if (uploader_.get()) {
    uploader_->UploadAsync(http_request_.get(), NULL);
  } else {
    http_request_->ExecuteAsync(NULL);
  }
}

// static
util::Status ClientServiceRequest::ParseResponse(
    HttpResponse* response, SerializableJson* data) {
  data->Clear();
  if (!response->status().ok()) return response->status();

  DataReader* reader = response->body_reader();
  if (!reader) {
    return StatusInternalError("Response has no body to parse.");
  }
  return data->LoadFromJsonReader(reader);
}

util::Status ClientServiceRequest::AppendVariable(
    const string& variable_name, const UriTemplateConfig& config,
    string* target) {
  LOG(FATAL) << "Either override AppendVariable or PrepareHttpRequest";
  return StatusUnimplemented("Internal error");
}

util::Status ClientServiceRequest::AppendOptionalQueryParameters(
     string* target) {
  const char* sep = "?";
  if (use_media_download_) {
    const char kAlt[] = "alt=";
    bool have_alt = false;
    int begin_params = target->find('?');
    if (begin_params != string::npos) {
      sep = "&";
      for (int offset = target->find(kAlt, begin_params + 1);
           offset != string::npos;
           offset = target->find(kAlt, offset + 1)) {
        char prev = target->c_str()[offset - 1];
        if (prev != '?' && prev != '&') continue;

        StringPiece value(StringPiece(*target).substr(offset + sizeof(kAlt)));
        if (value == "media" || value.starts_with("media&")) {
          // That second check means that the value was 'media' and then
          // another parameter is being declared within the url.
          have_alt = true;
        } else {
          LOG(WARNING)
              << "alt parameter was already specified in url="
              << *target
              << " which is inconsistent with 'media' for media-download";
          have_alt = true;
        }
      }
    }
    if (!have_alt) {
      target->append(sep);
      target->append("alt=media");
      sep = "&";
    }
  }

  return StatusOk();
}

util::Status ClientServiceRequest::CallAppendVariable(
    const string& variable_name, const UriTemplateConfig& config,
    string* target) {
  googleapis::util::Status status = AppendVariable(variable_name, config, target);
  if (!status.ok()) {
    VLOG(1) << "Failed appending variable_name='" << variable_name << "'";
  }
  return status;
}

void ClientServiceRequest::ResetMediaUploader(MediaUploader* uploader) {
  return uploader_.reset(uploader);
}


ClientService::ClientService(
    const StringPiece& url_root,
    const StringPiece& url_path,
    HttpTransport* transport)
    : transport_(transport), in_shutdown_(false) {
  ChangeServiceUrl(url_root, url_path);
}

ClientService::~ClientService() {
}

void ClientService::Shutdown() {
  in_shutdown_ = true;
  transport_->Shutdown();
}

void ClientService::ChangeServiceUrl(
    const StringPiece& url_root, const StringPiece& url_path) {
  // We're going to standardize so that:
  //   url root always ends with '/'
  //   url path never begins with '/'
  // But we're not necessarily going to document it this way yet.
  int url_root_extra = url_root.ends_with("/") ? 0 : 1;
  int url_path_trim = url_path.starts_with("/") ? 1 : 0;

  service_url_ = JoinPath(url_root.as_string(), url_path.as_string());
  url_root_ =
      StringPiece(service_url_).substr(0, url_root.size() + url_root_extra);
  url_path_ = StringPiece(service_url_)
                  .substr(url_root_.size(), url_path.size() - url_path_trim);
}

std::string ClientService::batch_url() const {
  return JoinPath(url_root_, string(batch_path_));
}

}  // namespace client

}  // namespace googleapis
