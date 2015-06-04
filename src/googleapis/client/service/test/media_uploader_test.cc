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


#include "googleapis/client/service/media_uploader.h"

#include <string>
using std::string;
#include "googleapis/client/data/data_reader.h"
#include "googleapis/client/data/data_writer.h"
#include "googleapis/client/transport/http_types.h"
#include "googleapis/client/transport/http_request.h"
#include "googleapis/client/transport/http_response.h"
#include "googleapis/client/transport/http_transport.h"
#include "googleapis/client/transport/test/mock_http_transport.h"
#include "googleapis/client/util/status.h"
#include "googleapis/client/util/uri_utils.h"
#include "googleapis/strings/numbers.h"
#include "googleapis/strings/strcat.h"
#include <gtest/gtest.h>

namespace googleapis {

using client::DataReader;
using client::HttpResponse;
using client::HttpRequest;
using client::HttpRequestState;
using client::MediaUploadSpec;
using client::MediaUploader;
using client::MockHttpRequest;
using client::MockHttpTransport;
using client::NewUnmanagedInMemoryDataReader;
using client::StatusFromHttp;
using client::StatusOk;
using client::StatusUnknown;
using testing::_;
using testing::DoAll;
using testing::InvokeWithoutArgs;

const char kPrepareFailed[] = "TEST_PREPARE_FAILED";
const char kBoundary[] = "XtestX";
const char kFieldNotPresent[] = "*** Field not present ***";  // sentinal
const char kSomeBinaryData[] = {
  0x00, 0x01, 0x20, 0x03, 0x04, 0x05, 0x06, 0x07,
  0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
  0xff, 0xfe, 0xfd, 0xfc, 0xfb, 0xfa, 0xf9, 0xf8,
  0xf7, 0xf6, 0xf5, 0xf4, 0xf3, 0xf2, 0xf1, 0xf0
};

const char kEndBigData[] = "Last few bigdata bytes.";

class MediaUploaderFixture : public testing::Test {
 public:
  MediaUploaderFixture()
      : request_(HttpRequest::POST, &transport_),
        spec_("simple", "SIMPLE/PATH", true),
        uploader_(&spec_, "BASE", "Default/Path") {
    uploader_.set_multipart_boundary(kBoundary);
  }

  ~MediaUploaderFixture() {}

  // We'll append the decorator to verify that this got called.
  // The decorator can be NULL to indicate a failure.
  googleapis::util::Status PrepareUrl(
      const char* decorator, const StringPiece& from, string* to) {
    *to = from.as_string();
    if (decorator == NULL) {
      to->append(kPrepareFailed);
      return StatusUnknown("Test Failure");
    } else {
      to->append(decorator);
      return StatusOk();
    }
  }

  MediaUploader::UrlPreparer* MakePreparer() {
    return NewCallback(this, &MediaUploaderFixture::PrepareUrl, "+RESOLVER");
  }

 protected:
  MockHttpTransport transport_;
  MockHttpRequest request_;
  MediaUploadSpec spec_;
  MediaUploader uploader_;
};


TEST_F(MediaUploaderFixture, TestDefault) {
  EXPECT_FALSE(uploader_.is_ready());

  // Setting nothing fails
  googleapis::util::Status got_status =
        uploader_.BuildRequest(&request_, MakePreparer());
  EXPECT_FALSE(got_status.ok());
  EXPECT_EQ("", request_.url());
  EXPECT_EQ("", request_.content_as_string());
  EXPECT_FALSE(uploader_.is_ready());

  // A failed constructed uploader wont upload nor attempt to execute.
  got_status = uploader_.Upload(&request_);
  EXPECT_FALSE(got_status.ok());
  EXPECT_FALSE(request_.state().ok());
}

TEST_F(MediaUploaderFixture, TestJustMetadataNoMultipart) {
  MediaUploadSpec non_multipart_spec("simple", "SIMPLE/PATH", false);
  MediaUploader non_multipart_uploader(
      &non_multipart_spec, "BASE", "Default/Path");
  non_multipart_uploader.set_multipart_boundary(kBoundary);

  const string kMetadata = "METADATA";
  non_multipart_uploader.set_metadata("metadata_type", kMetadata);
  googleapis::util::Status got_status =
        non_multipart_uploader.BuildRequest(&request_, MakePreparer());
  EXPECT_TRUE(got_status.ok()) << got_status.ToString();
  EXPECT_TRUE(non_multipart_uploader.is_ready());

  EXPECT_EQ("BASE/Default/Path+RESOLVER", request_.url());
  EXPECT_EQ("METADATA", request_.content_as_string());
  EXPECT_EQ(1, request_.headers().size());
  request_.CheckHeader(HttpRequest::HttpHeader_CONTENT_TYPE, "metadata_type");

  const string kResponseBody = "Upload Response";
  std::unique_ptr<Closure> poke_set_http(
      NewPermanentCallback(
          &request_, &MockHttpRequest::poke_http_code, 200));
  std::unique_ptr<Closure> poke_set_response_body(
      NewPermanentCallback(
          &request_, &MockHttpRequest::poke_response_body, kResponseBody));
  EXPECT_CALL(request_, DoExecute(request_.response())).WillOnce(
      DoAll(
          InvokeWithoutArgs(poke_set_http.get(), &Closure::Run),
          InvokeWithoutArgs(poke_set_response_body.get(), &Closure::Run)));

  got_status = non_multipart_uploader.Upload(&request_);
  EXPECT_TRUE(got_status.ok()) << got_status.ToString();
  EXPECT_EQ(kResponseBody, request_.response_body_as_string());
  EXPECT_TRUE(request_.state().ok());
}

TEST_F(MediaUploaderFixture, TestNullContentWithMimeType) {
  const string kMimeType = "test/mime-type";
  uploader_.set_media_content_reader(kMimeType, NULL);
  uploader_.set_metadata("metadata_type", "METADATA");
  googleapis::util::Status got_status =
        uploader_.BuildRequest(&request_, MakePreparer());
  EXPECT_TRUE(got_status.ok()) << got_status.ToString();
  EXPECT_TRUE(uploader_.is_ready());

  EXPECT_EQ(
      "BASE/SIMPLE/PATH?uploadType=multipart+RESOLVER", request_.url());

  EXPECT_EQ(
      StrCat("--", kBoundary, "\n"
             "Content-Type: metadata_type\n"
             "\n"
             "METADATA\n",
             "--", kBoundary, "\n"
             "Content-Type: ", kMimeType, "\n"
             "\n"
             "\n"
             "--", kBoundary, "--\n"),
      request_.content_as_string());
  request_.CheckHeader(HttpRequest::HttpHeader_CONTENT_TYPE,
                       StrCat("multipart/related; boundary=", kBoundary));
}

TEST_F(MediaUploaderFixture, TestJustMetadata) {
  uploader_.set_metadata("metadata_type", "METADATA");
  googleapis::util::Status got_status =
        uploader_.BuildRequest(&request_, MakePreparer());
  EXPECT_TRUE(got_status.ok()) << got_status.ToString();
  EXPECT_TRUE(uploader_.is_ready());

  EXPECT_EQ("BASE/Default/Path+RESOLVER", request_.url());
  EXPECT_EQ("METADATA", request_.content_as_string());
  request_.CheckHeader(HttpRequest::HttpHeader_CONTENT_TYPE, "metadata_type");

  std::unique_ptr<Closure> poke_set_http(
      NewPermanentCallback(
          &request_, &MockHttpRequest::poke_http_code, 200));
  EXPECT_CALL(request_, DoExecute(request_.response())).WillOnce(
      InvokeWithoutArgs(poke_set_http.get(), &Closure::Run));

  got_status = uploader_.Upload(&request_);
  EXPECT_TRUE(got_status.ok()) << got_status.ToString();
  EXPECT_TRUE(request_.state().ok());
}

TEST_F(MediaUploaderFixture, TestJustMedia) {
  uploader_.set_media_content_reader(
      "media_type", NewUnmanagedInMemoryDataReader("MEDIA"));
  googleapis::util::Status got_status =
        uploader_.BuildRequest(&request_, MakePreparer());
  EXPECT_TRUE(got_status.ok()) << got_status.ToString();
  EXPECT_TRUE(uploader_.is_ready());

  EXPECT_EQ("BASE/SIMPLE/PATH?uploadType=media+RESOLVER", request_.url());

  EXPECT_EQ("MEDIA", request_.content_as_string());
  request_.CheckHeader(HttpRequest::HttpHeader_CONTENT_TYPE, "media_type");

  std::unique_ptr<Closure> poke_set_http(
      NewPermanentCallback(
          &request_, &MockHttpRequest::poke_http_code, 200));
  EXPECT_CALL(request_, DoExecute(request_.response())).WillOnce(
      InvokeWithoutArgs(poke_set_http.get(), &Closure::Run));

  got_status = uploader_.Upload(&request_);
  EXPECT_TRUE(got_status.ok()) << got_status.ToString();
}

TEST_F(MediaUploaderFixture, TestBinaryMedia) {
  const string binary_data(kSomeBinaryData, sizeof(kSomeBinaryData));
  uploader_.set_media_content_reader(
      "media_type", NewUnmanagedInMemoryDataReader(binary_data));
  googleapis::util::Status got_status =
        uploader_.BuildRequest(&request_, MakePreparer());
  EXPECT_TRUE(got_status.ok()) << got_status.ToString();
  EXPECT_TRUE(uploader_.is_ready());

  EXPECT_EQ("BASE/SIMPLE/PATH?uploadType=media+RESOLVER", request_.url());

  EXPECT_EQ(binary_data, request_.content_as_string());
  request_.CheckHeader(HttpRequest::HttpHeader_CONTENT_TYPE, "media_type");

  std::unique_ptr<Closure> poke_set_http(
      NewPermanentCallback(
          &request_, &MockHttpRequest::poke_http_code, 200));
  EXPECT_CALL(request_, DoExecute(request_.response())).WillOnce(
      InvokeWithoutArgs(poke_set_http.get(), &Closure::Run));

  got_status = uploader_.Upload(&request_);
  EXPECT_TRUE(got_status.ok()) << got_status.ToString();
  EXPECT_TRUE(request_.state().ok());
}

TEST_F(MediaUploaderFixture, TestMultipart) {
  uploader_.set_media_content_reader(
      "media_type", NewUnmanagedInMemoryDataReader("MEDIA"));
  uploader_.set_metadata("metadata_type", "METADATA");
  googleapis::util::Status got_status = uploader_.BuildRequest(
       &request_, MakePreparer());
  EXPECT_TRUE(got_status.ok()) << got_status.ToString();
  EXPECT_TRUE(uploader_.is_ready());

  EXPECT_EQ(
      "BASE/SIMPLE/PATH?uploadType=multipart+RESOLVER", request_.url());

  EXPECT_EQ(
      StrCat("--", kBoundary, "\n"
             "Content-Type: metadata_type\n"
             "\n"
             "METADATA\n",
             "--", kBoundary, "\n"
             "Content-Type: media_type\n"
             "\n"
             "MEDIA\n"
             "--", kBoundary, "--\n"),
      request_.content_as_string());
  request_.CheckHeader(HttpRequest::HttpHeader_CONTENT_TYPE,
                       StrCat("multipart/related; boundary=", kBoundary));

  got_status = uploader_.Upload(&request_);
  EXPECT_TRUE(got_status.ok()) << got_status.ToString();
  EXPECT_TRUE(request_.state().ok());
}

TEST_F(MediaUploaderFixture, TestBinaryMultipart) {
  const string binary_data(kSomeBinaryData, sizeof(kSomeBinaryData));
  uploader_.set_media_content_reader(
      "media_type", NewUnmanagedInMemoryDataReader(binary_data));
  uploader_.set_metadata("metadata_type", "METADATA");
  googleapis::util::Status got_status =
        uploader_.BuildRequest(&request_, MakePreparer());
  EXPECT_TRUE(got_status.ok()) << got_status.ToString();
  EXPECT_TRUE(uploader_.is_ready());

  EXPECT_EQ(
      "BASE/SIMPLE/PATH?uploadType=multipart+RESOLVER", request_.url());

  EXPECT_EQ(
      StrCat("--", kBoundary, "\n"
             "Content-Type: metadata_type\n"
             "\n"
             "METADATA\n",
             "--", kBoundary, "\n"
             "Content-Type: media_type\n"
             "\n",
             binary_data,
             "\n"
             "--", kBoundary, "--\n"),
      request_.content_as_string());
  request_.CheckHeader(HttpRequest::HttpHeader_CONTENT_TYPE,
                       StrCat("multipart/related; boundary=", kBoundary));

  std::unique_ptr<Closure> poke_set_http(
      NewPermanentCallback(
          &request_, &MockHttpRequest::poke_http_code, 200));
  EXPECT_CALL(request_, DoExecute(request_.response())).WillOnce(
      InvokeWithoutArgs(poke_set_http.get(), &Closure::Run));

  got_status = uploader_.Upload(&request_);
  EXPECT_TRUE(got_status.ok()) << got_status.ToString();
  EXPECT_EQ(HttpRequestState::COMPLETED, request_.state().state_code());
  EXPECT_TRUE(request_.state().ok());
}

TEST_F(MediaUploaderFixture, TestPrepareFailure) {
  uploader_.set_metadata("metadata_type", "METADATA");
  googleapis::util::Status got_status =
      uploader_.BuildRequest(
          &request_,
          NewCallback(this, &MediaUploaderFixture::PrepareUrl, NULL));
  EXPECT_FALSE(got_status.ok());
  EXPECT_FALSE(got_status.error_message().empty());
  EXPECT_FALSE(uploader_.is_ready());

  EXPECT_TRUE(request_.content_reader() != NULL);
  EXPECT_EQ(0, request_.content_reader()->TotalLengthIfKnown());
  EXPECT_EQ("", request_.url());
  EXPECT_EQ(0, request_.headers().size());

  EXPECT_FALSE(uploader_.Upload(&request_).ok());
  EXPECT_EQ(HttpRequestState::COULD_NOT_SEND,
            request_.state().state_code());
  EXPECT_TRUE(request_.response() != NULL);
  EXPECT_EQ(0, request_.state().http_code());
  EXPECT_FALSE(request_.state().transport_status().ok());
}

}  // namespace googleapis
