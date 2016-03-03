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


#include <limits.h>
#include <string>
using std::string;
#include "googleapis/client/data/base64_codec.h"
#include "googleapis/client/data/codec.h"
#include "googleapis/client/util/escaping.h"
#include "googleapis/client/util/status.h"
#include "googleapis/base/integral_types.h"
#include <glog/logging.h>
#include "googleapis/strings/stringpiece.h"

namespace googleapis {

namespace {
const int kDefaultChunkSize = 1 << 13;  // 8K
using client::CodecReader;
using client::DataReader;
using client::StatusOk;
using client::StatusInvalidArgument;

inline int64 round_down_divisible_by_3(int64 n) { return n - n % 3; }

// We'll assume the given chunk size was intended for plain text size.
// If we are decoding then the source chunk is base64 escaped.
int64 DetermineSourceChunkSize(bool encoding, int desired) {
  if (desired < 3) desired = kDefaultChunkSize;
  int divisible_by_3 = round_down_divisible_by_3(desired);
  if (encoding) return divisible_by_3;
  return googleapis_util::CalculateBase64EscapedLen(divisible_by_3, true);
}

int64 DetermineTargetBufferSize(bool encoding, int desired) {
  if (desired < 3) desired = kDefaultChunkSize;
  if (encoding) return googleapis_util::CalculateBase64EscapedLen(desired,
                                                                  true);
  return desired;  // ok if bigger than needed.
}

// Base64 encodes three bytes of input at a time. If the input is not
// divisible by three then it is padded as appropriate.
// Since the reader is a stream and does not know the length, we'll require
// reading chunks of multiples of 3 until we hit the eof so that we only pad
// at the end and not intermediate byte sequences.
class Base64Reader : public CodecReader {
 public:
  Base64Reader(
      DataReader* source, Closure* deleter, int chunk_size,
      bool websafe, bool encoding)
      : CodecReader(source, deleter,
                    DetermineSourceChunkSize(encoding, chunk_size),
                    DetermineTargetBufferSize(encoding, chunk_size),
                    encoding),
        websafe_(websafe) {
    CHECK_LE(3, chunk_size);
  }

  ~Base64Reader() {}

 protected:
  virtual googleapis::util::Status EncodeChunk(
      const StringPiece& chunk, bool is_final_chunk,
      char* to, int64* to_length) {
    if (chunk.size() > INT_MAX) {
      *to_length = 0;
      return StatusInvalidArgument("chunk too big");
    }
    if (*to_length > INT_MAX) {
      return StatusInvalidArgument("target size too big");
    }
    int szdest = *to_length;
    int len = chunk.size();
    const unsigned char* source =
        reinterpret_cast<const unsigned char*>(chunk.data());
    if (websafe_) {
      *to_length = googleapis_util::WebSafeBase64Escape(
          source, len, to, szdest, is_final_chunk);
    } else {
      *to_length = googleapis_util::Base64Escape(source, len, to, szdest);
    }
    return StatusOk();
  }

  virtual googleapis::util::Status DecodeChunk(
      const StringPiece& chunk, bool unused_is_final_chunk,
      char* to, int64* to_length) {
    if (chunk.size() > INT_MAX) {
      *to_length = 0;
      return StatusInvalidArgument("chunk too big");
    }
    if (*to_length > INT_MAX) {
      return StatusInvalidArgument("target size too big");
    }
    string decoded;
    bool success;
    if (websafe_) {
      success = googleapis_util::WebSafeBase64Unescape(
          chunk.data(), chunk.size(), &decoded);
    } else {
      success = googleapis_util::Base64Unescape(
          chunk.data(), chunk.size(), &decoded);
    }
    if (success && decoded.size() <= *to_length) {
      memcpy(to, decoded.data(), decoded.size());
      *to_length = decoded.size();
    } else {
      *to_length = -1;
    }
    return StatusOk();
  }

 private:
  bool websafe_;
  DISALLOW_COPY_AND_ASSIGN(Base64Reader);
};

}  // anonymous namespace

namespace client {

Base64Codec::Base64Codec(int chunk_size, bool websafe)
    : chunk_size_(chunk_size), websafe_(websafe) {}
Base64Codec::~Base64Codec() {}

DataReader* Base64Codec::NewManagedEncodingReader(
    DataReader* source, Closure* deleter, googleapis::util::Status* status) {
  CHECK(status != NULL);
  if (!source) {
    *status = StatusInvalidArgument("No source reader provided");
    return client::NewManagedInvalidDataReader(*status, deleter);
  }
  *status = StatusOk();
  return new Base64Reader(source, deleter, chunk_size_, websafe_, true);
}

DataReader* Base64Codec::NewManagedDecodingReader(
    DataReader* source, Closure* deleter, googleapis::util::Status* status) {
  CHECK(status != NULL);
  if (!source) {
    *status = StatusInvalidArgument("No source reader provided");
    return client::NewManagedInvalidDataReader(*status, deleter);
  }
  *status = StatusOk();
  return new Base64Reader(source, deleter, chunk_size_, websafe_, false);
}

Base64CodecFactory::Base64CodecFactory()
    : chunk_size_(kDefaultChunkSize), websafe_(false) {
}
Base64CodecFactory::~Base64CodecFactory() {}
Codec* Base64CodecFactory::New(googleapis::util::Status* status) {
  CHECK(status != NULL);
  *status = StatusOk();
  return new Base64Codec(chunk_size_, websafe_);
}

}  // namespace client

}  // namespace googleapis
